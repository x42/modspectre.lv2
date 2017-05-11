/*
 *  Copyright (C) 2016,2017 Robin Gareus <robin@gareus.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#if defined __ATOMIC_SEQ_CST

#define _atomic_int_get(P)    __atomic_load_4 (&(P), __ATOMIC_SEQ_CST)
#define _atomic_int_set(P, V) __atomic_store_4 (&(P), (V), __ATOMIC_SEQ_CST)

#define adef uint32_t
#define avar uint32_t

#elif defined __GNUC__

#define _atomic_int_set(P,V) __sync_lock_test_and_set (&(P), (V))
#define _atomic_int_get(P)   __sync_add_and_fetch (&(P), 0)
#define adef volatile uint32_t
#define avar uint32_t

#else

#warning non-atomic ringbuffer

#define _atomic_int_set(P,V) P = (V)
#define _atomic_int_get(P) P
#define adef size_t
#define avar size_t

#endif

typedef struct {
	float* data;
	adef rp;
	adef wp;
	size_t len;
	size_t mask;
} ringbuf;

static ringbuf* rb_alloc (size_t siz) {
	ringbuf* rb = (ringbuf*) malloc (sizeof (ringbuf));
	size_t power_of_two;
	for (power_of_two = 1; 1U << power_of_two < siz; ++power_of_two);
	rb->len = 1 << power_of_two;
	rb->mask = rb->len -1;
	_atomic_int_set (rb->rp, 0);
	_atomic_int_set (rb->wp, 0);
	rb->data = (float*) malloc (rb->len * sizeof(float));
	return rb;
}

static void rb_free(ringbuf *rb) {
	free(rb->data);
	free(rb);
}

static size_t rb_write_space (ringbuf* rb) {
	avar w, r;
	w = _atomic_int_get (rb->wp);
	r = _atomic_int_get (rb->rp);

	if (r == w) {
		return (rb->len - 1);
	}
	return ((rb->len + r - w) & rb->mask) - 1;
}

static size_t rb_read_space (ringbuf* rb) {
	avar w = _atomic_int_get (rb->wp);
	avar r = _atomic_int_get (rb->rp);
	return ((rb->len + w - r) & rb->mask);
}

static int rb_read_one (ringbuf* rb, float* data) {
	if (rb_read_space (rb) < 1) {
		return -1;
	}
	size_t rp = _atomic_int_get (rb->rp);
	*data = rb->data[rp];
	rp = (rp + 1) & rb->mask;
	_atomic_int_set (rb->rp, rp);
	return 0;
}

static ssize_t rb_read (ringbuf* rb, float* data, size_t len) {
	if (rb_read_space(rb) < len) {
		return -1;
	}
	size_t rp = _atomic_int_get (rb->rp);
	if (rp + len <= rb->len) {
		memcpy ((void*)data, (void*)&rb->data[rp], len * sizeof (float));
	} else {
		const size_t part = rb->len - rp;
		const size_t remn = len - part;
		memcpy ((void*) data,        (void*) &rb->data[rp], part * sizeof (float));
		memcpy ((void*) &data[part], (void*) rb->data,      remn * sizeof (float));
	}
	rp = (rp + len) & rb->mask;
	_atomic_int_set (rb->rp, rp);
	return 0;
}

static int rb_write (ringbuf* rb, const float* data, size_t len) {
	if (rb_write_space(rb) < len) {
		return -1;
	}
	size_t wp = _atomic_int_get (rb->wp);
	if (wp + len <= rb->len) {
		memcpy ((void*) &rb->data[wp], (void*) data, len * sizeof (float));
	} else {
		int part = rb->len - wp;
		int remn = len - part;
		memcpy ((void*) &rb->data[wp], (void*) data,        part * sizeof (float));
		memcpy ((void*) rb->data,      (void*) &data[part], remn * sizeof (float));
	}
	wp = (wp + len) & rb->mask;
	_atomic_int_set (rb->wp, wp);
	return 0;
}

static void rb_read_clear(ringbuf *rb) {
	avar wp = _atomic_int_get (rb->wp);
	_atomic_int_set (rb->rp, wp);
}
