#!/usr/bin/make -f

# these can be overridden using make variables. e.g.
#   make CFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/modspectre.lv2 PREFIX=/usr
#
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
PREFIX ?= /usr/local
CFLAGS ?= -g -Wall
LIBDIR ?= lib

N_BINS=256

STRIP?=strip
STRIPFLAGS?=-s

modspectre_VERSION?=$(shell git describe --tags HEAD 2>/dev/null | sed 's/-g.*$$//;s/^v//' || echo "LV2")
###############################################################################
LIB_EXT=.so

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
LOADLIBES=-lm
LV2NAME=modspectre
BUNDLE=modspectre.lv2
BUILDDIR=build/
targets=

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXTENDED_RE=-E
  STRIPFLAGS=-u -r -arch all -s lv2syms
  targets+=lv2syms
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
  EXTENDED_RE=-r
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  STRIP=$(XWIN)-strip
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed
  LIB_EXT=.dll
  override LDFLAGS += -static-libgcc -static-libstdc++
endif

targets+=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

ifneq ($(MOD),)
  targets+=$(BUILDDIR)modgui
  MODLABEL=mod:label \"SpectrumAnalyzer\";
  MODBRAND=mod:brand \"x42\";
else
  MODLABEL=
  MODBRAND=
endif

###############################################################################
# extract versions
LV2VERSION=$(modspectre_VERSION)
include git2lv2.mk

# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

ifeq ($(shell pkg-config --exists fftw3f || echo no), no)
  $(error "fftw3f library was not found")
endif

# add library dependent flags and libs
override CFLAGS += -std=c99 $(OPTIMIZATIONS) `pkg-config --cflags lv2 fftw3f` -Wno-unused-function
ifeq ($(XWIN),)
override CFLAGS += -fPIC -fvisibility=hidden
else
override CFLAGS += -fPIC
endif
override LOADLIBES += `pkg-config --libs lv2 fftw3f`

# build target definitions
default: all

all: $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

lv2syms:
	echo "_lv2_descriptor" > lv2syms

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in lv2ttl/manifest.modgui.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" \
	  lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl
ifneq ($(MOD),)
	sed "s/@LV2NAME@/$(LV2NAME)/" \
		lv2ttl/manifest.modgui.in >> $(BUILDDIR)manifest.ttl
endif

$(BUILDDIR)$(LV2NAME).ttl: lv2ttl/$(LV2NAME).ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@SIGNATURE@//;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@MODBRAND@/$(MODBRAND)/;s/@MODLABEL@/$(MODLABEL)/" \
		lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): src/$(LV2NAME).c src/fft.c src/ringbuf.h Makefile
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	  -DN_BINS=$(N_BINS) \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) src/$(LV2NAME).c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

$(BUILDDIR)modgui: modgui/
	@mkdir -p $(BUILDDIR)/modgui
	cp -r modgui/* $(BUILDDIR)modgui/

# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)
ifneq ($(MOD),)
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui
	install -t $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui $(BUILDDIR)modgui/*
endif

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -rf $(DESTDIR)$(LV2DIR)/$(BUNDLE)/modgui
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(BUILDDIR)$(LV2NAME)$(LIB_EXT) lv2syms
	rm -rf $(BUILDDIR)modgui
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean
