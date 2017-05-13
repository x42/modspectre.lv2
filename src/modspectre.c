/* MOD spectrum analyzer
 *
 * Copyright (C) 2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define BACKGROUND_FFT // use a background thread

#define _GNU_SOURCE

#define MODSPECTRE_URI "http://gareus.org/oss/lv2/modspectre"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

#ifdef BACKGROUND_FFT
#include <pthread.h>
#include "ringbuf.h"
#endif

#include "fft.c"

enum {
	P_AIN = 0,
	P_RESPONSE,
	P_NOTIFY,
	P_SPECT,
	P_LAST = P_SPECT + N_BINS
};

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_Sequence;
	LV2_URID atom_Float;
	LV2_URID atom_Int;

	LV2_URID spectrum;
	LV2_URID bin_count;
	LV2_URID bin_data;
} MsrURIs;


typedef struct {
	/* ports */
	float* ports[P_LAST];

	/* message ports */
	LV2_Atom_Sequence*       ctrl_out;
	LV2_Atom_Forge           forge;
	LV2_Atom_Forge_Frame     frame;
	MsrURIs                  uris;

	/* FFT */
	struct FFTAnalysis *fftx;

#ifdef BACKGROUND_FFT
	pthread_mutex_t lock;
	pthread_cond_t  signal;
	pthread_t       thread;
	bool            keep_running;

	ringbuf*        to_fft;
	ringbuf*        result;
#endif

	/* config & state */
	double rate;
	float bins[N_BINS];
	float last[N_BINS];
	float resp;
	float tc;
} ModSpectre;


/* *****************************************************************************
 * FFT
 */

static const float log1k = 6.907755279f; // logf (1000);
static const float guipx = 0.0057142857; // 1.f / 175.f; (gui px granularity)

static int x_at_freq (float f)
{
	return N_BINS * logf (f / 20.0) / log1k; // 20..20k
}

static void
assign_bins (ModSpectre* self)
{
	const float tc = self->tc;
	for (uint32_t b = 0; b < N_BINS; ++b) {
		self->bins[b] *= tc;
		if (self->bins[b] < guipx) {
			self->bins[b] = 0;
		}
	}
	for (uint32_t i = 1; i < self->fftx->data_size - 1; ++i) {
		const float pab = fftx_power_at_bin(self->fftx, i);
		const float frq = fftx_freq_at_bin (self->fftx, i);
		if (pab <= -96.f) {
			continue;
		}
		int b = x_at_freq (frq);
		if (b >= N_BINS) {
			continue;
		}
		if (b < 2) {
			b = 1;
		}
		float pwr = 1.f - pab / -96.f;
		if (pwr > self->bins[b]) {
			self->bins[b] = pwr;
		}
	}
}

#ifdef BACKGROUND_FFT
static void*
worker (void* arg)
{
	ModSpectre* self = (ModSpectre*)arg;

	pthread_mutex_lock (&self->lock);
	while (self->keep_running) {
		// wait for signal
		pthread_cond_wait (&self->signal, &self->lock);

		if (!self->keep_running) {
			break;
		}

		size_t n_samples = rb_read_space (self->to_fft);
		do {
			if (n_samples < 1) {
				break;
			}
			if (n_samples > 8192) {
				n_samples = 8192;
			}

			float a_in[8192];
			rb_read (self->to_fft, a_in, n_samples);

			if (0 == fftx_run (self->fftx, n_samples, a_in)) {
				assign_bins (self);
				float ignore = 1;
				rb_write (self->result, &ignore, 1);
#if 0 // defined __GNUC__ || defined __clang___
				__sync_synchronize ();
#endif
			}

			n_samples = rb_read_space (self->to_fft);
		} while (n_samples > 0);
	}
	pthread_mutex_unlock (&self->lock);
	return NULL;
}

static void
feed_fft (ModSpectre* self, const float* data, size_t n_samples)
{
	rb_write (self->to_fft, data, n_samples);

	if (pthread_mutex_trylock (&self->lock) == 0) {
		pthread_cond_signal (&self->signal);
		pthread_mutex_unlock (&self->lock);
	}
}
#endif

/* *****************************************************************************
 * Atom Port
 */

#ifdef HAVE_LV2_1_8
#define x_forge_object lv2_atom_forge_object
#else
#define x_forge_object lv2_atom_forge_blank
#endif

/** map uris */
static void
map_uris (LV2_URID_Map* map, MsrURIs* uris)
{
	uris->atom_Blank          = map->map (map->handle, LV2_ATOM__Blank);
	uris->atom_Object         = map->map (map->handle, LV2_ATOM__Object);
	uris->atom_Sequence       = map->map (map->handle, LV2_ATOM__Sequence);
	uris->atom_Int            = map->map (map->handle, LV2_ATOM__Int);
	uris->atom_Float          = map->map (map->handle, LV2_ATOM__Float);

	uris->spectrum            = map->map (map->handle, MODSPECTRE_URI "#spectrum");
	uris->bin_count           = map->map (map->handle, MODSPECTRE_URI "#bin_count");
	uris->bin_data            = map->map (map->handle, MODSPECTRE_URI "#bin_data");
}

static void
tx_to_gui (ModSpectre* self)
{
	float tbl[N_BINS];
	memcpy (tbl, self->bins, sizeof (float) * N_BINS);

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time (&self->forge, 0);
	x_forge_object (&self->forge, &frame, 0, self->uris.spectrum);

	/* add integer attribute 'N_BINS' */
	lv2_atom_forge_property_head (&self->forge, self->uris.bin_count, 0);
	lv2_atom_forge_int (&self->forge, N_BINS);

	/* add vector of floats raw 'osc_data' */
	lv2_atom_forge_property_head (&self->forge, self->uris.bin_data, 0);
	lv2_atom_forge_vector (&self->forge, sizeof (float), self->uris.atom_Float, N_BINS, tbl);

	/* close off atom-object */
	lv2_atom_forge_pop (&self->forge, &frame);
}

/* *****************************************************************************
 * LV2 Plugin
 */

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	ModSpectre* self = (ModSpectre*)calloc (1, sizeof (ModSpectre));

	LV2_URID_Map* map = NULL;
	for (int i = 0; features[i]; ++i) {
		if (!strcmp (features[i]->URI, LV2_URID__map)) {
			map = (LV2_URID_Map*)features[i]->data;
		}
	}

	if (!map) {
		free (self);
		return NULL;
	}

	lv2_atom_forge_init (&self->forge, map);
	map_uris (map, &self->uris);

	int fft_size = N_BINS * 16;

	self->rate = rate;
	self->fftx = (struct FFTAnalysis*) calloc(1, sizeof(struct FFTAnalysis));
	fftx_init(self->fftx, fft_size, rate, 30 /*fps*/);

	self->resp = 0.f;
	self->tc = 1.f;

	for (uint32_t b = 0; b < N_BINS; ++b) {
		self->last[b] = -1;
	}

#ifdef BACKGROUND_FFT
	pthread_mutex_init (&self->lock, NULL);
	pthread_cond_init (&self->signal, NULL);

	self->to_fft = rb_alloc (fft_size * 8);
	self->result = rb_alloc (32);
	self->keep_running = true;
	if (pthread_create (&self->thread, NULL, worker, self)) {
		pthread_mutex_destroy (&self->lock);
		pthread_cond_destroy (&self->signal);
		rb_free (self->to_fft);
		rb_free (self->result);
		fftx_free(self->fftx);
		free (self);
		return NULL;
	}
#endif
	return (LV2_Handle)self;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	ModSpectre* self = (ModSpectre*)instance;
	if (port == P_NOTIFY) {
		self->ctrl_out = (LV2_Atom_Sequence*) data;
	}
	else if (port < P_LAST) {
		self->ports[port] = (float*)data;
	}
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	ModSpectre* self = (ModSpectre*)instance;
	if (n_samples == 0) {
		return;
	}
	if (self->ctrl_out) {
		/* prepare forge buffer and initialize atom-sequence */
		const uint32_t capacity = self->ctrl_out->atom.size;
		lv2_atom_forge_set_buffer (&self->forge, (uint8_t*)self->ctrl_out, capacity);
		lv2_atom_forge_sequence_head (&self->forge, &self->frame, 0);
	}

	float const* const a_in = self->ports[P_AIN];
	bool fft_ran_this_cycle = false;

	if (self->resp != *self->ports[P_RESPONSE]) {
		self->resp = *self->ports[P_RESPONSE];
		float v = self->resp;
		if (v < 0.01) v = 0.01;
		if (v > 10.0) v = 10.0;
		self->tc = expf (-2.0 * M_PI * v / 30);
		//memset (self->bins, 0, sizeof(float) * N_BINS);
	}

#ifdef BACKGROUND_FFT
	float tbl[N_BINS];
	feed_fft (self, a_in, n_samples);
	fft_ran_this_cycle = rb_read_space (self->result) > 0;
	if (fft_ran_this_cycle) {
		float ignore = 0;
		while (0 == rb_read_one (self->result, &ignore)) ;
	}
	memcpy (tbl, self->bins, sizeof (float) * N_BINS);
#else
	fft_ran_this_cycle = 0 == fftx_run(self->fftx, n_samples, a_in);
	if (fft_ran_this_cycle) {
		assign_bins (self);
	}
	float *tbl = self->bins;
#endif

	for (uint32_t b = 0; b < N_BINS; ++b) {
		if (fabsf (self->last[b] - tbl[b]) >= guipx) {
			*self->ports[P_SPECT + b] = tbl[b];
			self->last[b] = tbl[b];
		} else {
			*self->ports[P_SPECT + b] = self->last[b];
		}
	}
	if (self->ctrl_out) {
		if (fft_ran_this_cycle) {
			tx_to_gui (self);
		}
		/* close off atom-sequence */
		lv2_atom_forge_pop (&self->forge, &self->frame);
	}
}

static void
cleanup (LV2_Handle instance)
{
	ModSpectre* self = (ModSpectre*)instance;
#ifdef BACKGROUND_FFT
  pthread_mutex_lock (&self->lock);
	self->keep_running = false;
	pthread_cond_signal (&self->signal);
	pthread_mutex_unlock (&self->lock);
	pthread_join (self->thread, NULL);

	pthread_mutex_destroy (&self->lock);
	pthread_cond_destroy (&self->signal);
	rb_free (self->to_fft);
	rb_free (self->result);
#endif
	fftx_free(self->fftx);
	free (instance);
}

static const void*
extension_data (const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	MODSPECTRE_URI,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}
