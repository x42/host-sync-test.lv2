#!/usr/bin/make -f

PREFIX ?= /usr/local
CFLAGS ?= -Wall -O2
LIBDIR ?= lib

PKG_CONFIG ?= pkg-config
STRIP ?= strip

###############################################################################
hst_VERSION?=$(shell git describe --tags HEAD 2>/dev/null | sed 's/-g.*$$//;s/^v//' || echo "LV2")
LIB_EXT=.so

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
LV2NAME=host_sync_test
BUNDLE=host_sync_test.lv2
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
  STRIPFLAGS=-s
  EXTENDED_RE=-r
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  STRIP=$(XWIN)-strip
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed
  LIB_EXT=.dll
  override LDFLAGS += -static-libgcc -static-libstdc++
else
  override CFLAGS += -fPIC
endif

targets+=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

###############################################################################
# extract versions
LV2VERSION=$(hst_VERSION)
include git2lv2.mk

# check for build-dependencies
ifeq ($(shell $(PKG_CONFIG) --atleast-version=1.17.2 lv2 || echo no), no)
  $(error "LV2 SDK 1.17.2 or later is required")
endif

override CFLAGS += `pkg-config --cflags lv2` -std=c99

# build target definitions
default: all

all: $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

lv2syms:
	echo "_lv2_descriptor" > lv2syms

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" \
	  lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl

$(BUILDDIR)$(LV2NAME).ttl: lv2ttl/$(LV2NAME).ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
		lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): src/$(LV2NAME).c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) src/$(LV2NAME).c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(BUILDDIR)$(LV2NAME)$(LIB_EXT) lv2syms
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean
