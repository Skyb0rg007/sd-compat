# SPDX-FileCopyrightText: 2026 Skye Soss <skye@soss.website>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

.DEFAULT_GOAL := all

# Which libc to build against: glibc or musl. Selects the shim headers under
# src/include/<libc>/ and, for musl, the extra wrappers in src/libc/musl/.
LIBC = glibc

CC = cc
CPP = cpp
AWK = awk
GPERF = gperf
PYTHON = python3

BUILDDIR = _build
GENDIR = $(BUILDDIR)/gen

# ---------------------------------------------------------------------------
# Installation paths baked into the library
# ---------------------------------------------------------------------------

prefix = /usr
sysconfdir = /etc
includedir = $(prefix)/include
libdir = $(prefix)/lib
libexecdir = $(prefix)/lib/systemd
pkgsysconfdir = $(sysconfdir)/systemd
pkgconfigdir = $(libdir)/pkgconfig

DESTDIR =

INSTALL = install

# ---------------------------------------------------------------------------
# Include paths
#
# src/include/override must come before src/include/uapi, and both are -isystem
# so that #include_next reaches the real libc and kernel headers.
# ---------------------------------------------------------------------------

SYSTEM_INCLUDES = -isystem src/include/override \
				  -isystem src/include/uapi \
				  -isystem src/include/$(LIBC)

# src/libsystemd must precede src/basic: both contain a forward.h, and the
# libsystemd sources need the one that declares the sd_* handle types.
LIBSYSTEMD_INCLUDES = -Isrc/libsystemd \
					  -Isrc/libsystemd/sd-bus \
					  -Isrc/libsystemd/sd-daemon \
					  -Isrc/libsystemd/sd-event \
					  -Isrc/libsystemd/sd-future \
					  -Isrc/libsystemd/sd-id128 \
					  -Isrc/libsystemd/sd-json \
					  -Isrc/libsystemd/sd-network \
					  -Isrc/libsystemd/sd-path \
					  -Isrc/libsystemd/sd-varlink

BASIC_INCLUDES = -I$(GENDIR) -Iinclude -Isrc/basic -Isrc/fundamental

# ---------------------------------------------------------------------------
# Configuration macros
#
# These stand in for the entries meson writes into config.h.
# ---------------------------------------------------------------------------

CONFIG_DEFINES = -D_GNU_SOURCE \
				 -D_FILE_OFFSET_BITS=64 \
				 -D_LARGEFILE64_SOURCE \
				 -DRELATIVE_SOURCE_PATH='"src"' \
				 -DTTY_MODE=0600 \
				 -DBUILD_MODE_DEVELOPER=1 \
				 -DSIZEOF_DEV_T=8 \
				 -DSIZEOF_INO_T=8 \
				 -DSIZEOF_RLIM_T=8 \
				 -DSIZEOF_TIME_T=8 \
				 -DSIZEOF_TIMEX_MEMBER=8 \
				 -DHAVE_ATTRIBUTE_ALLOC_SIZE \
				 -DHAVE_ATTRIBUTE_FALLTHROUGH \
				 -DHAVE_ATTRIBUTE_RETAIN \
				 -DHAVE_ATTRIBUTE_NO_REORDER \
				 -DHAVE_WARNING_ZERO_AS_NULL_POINTER_CONSTANT \
				 -DHAVE_WARNING_ZERO_LENGTH_BOUNDS \
				 -DGPERF_LEN_TYPE=size_t \
				 -DDEFAULT_TIMEOUT_SEC=90

USER_DEFINES = -DNOBODY_USER_NAME='"nobody"'

PATH_DEFINES = -DLIBDIR='"$(libdir)"' \
			   -DPREFIX_NOSLASH='"$(prefix)"' \
			   -DSYSTEM_CONFIG_UNIT_DIR='"$(pkgsysconfdir)/system"' \
			   -DSYSTEM_DATA_UNIT_DIR='"$(libexecdir)/system"' \
			   -DSYSTEM_ENV_GENERATOR_DIR='"$(libexecdir)/system-environment-generators"' \
			   -DSYSTEM_GENERATOR_DIR='"$(libexecdir)/system-generators"' \
			   -DUSER_CONFIG_UNIT_DIR='"$(pkgsysconfdir)/user"' \
			   -DUSER_DATA_UNIT_DIR='"$(libexecdir)/user"' \
			   -DUSER_ENV_GENERATOR_DIR='"$(libexecdir)/user-environment-generators"' \
			   -DUSER_GENERATOR_DIR='"$(libexecdir)/user-generators"' \
			   -DVARLINK_BRIDGES_DIR='"$(libexecdir)/varlink-bridges"'

WARNFLAGS = -Wall -Wextra \
			-Wno-missing-field-initializers -Wno-unknown-warning-option \
			-Wno-unused-parameter -Wno-nonnull-compare

DEPFLAGS = -MD -MP

CFLAGS = $(WARNFLAGS) -fPIC -fvisibility=default $(DEPFLAGS) \
		 $(CONFIG_DEFINES) $(USER_DEFINES) $(PATH_DEFINES)

# The upstream libsystemd release this compatibility library tracks.
VERSION = 262

VERSION_SCRIPT = src/libsystemd/libsystemd.sym

LDFLAGS =
LDLIBS =

# musl declares the ucontext.h functions but does not implement them; the fiber
# bootstrap in src/libsystemd/sd-future/fiber.c needs a real implementation.
ifeq ($(LIBC),musl)
LDLIBS += -lucontext
endif

# ---------------------------------------------------------------------------
# Generated sources
#
# src/basic/{capability-list,errno-list,stat-util}.c #include tables that
# upstream generates at build time. The *-to-name.inc tables only need cpp +
# awk; the *-from-name.inc perfect hashes additionally need gperf, which only
# errno-list.c still needs.
# ---------------------------------------------------------------------------

GPERF_NAMES = errno
LIST_NAMES = $(GPERF_NAMES) capability statx-mask statx-attribute

GEN_TO_NAME = $(patsubst %,$(GENDIR)/%-to-name.inc,$(LIST_NAMES))
GEN_FROM_NAME = $(patsubst %,$(GENDIR)/%-from-name.inc,$(GPERF_NAMES))
GEN_HEADERS = $(GEN_TO_NAME) $(GEN_FROM_NAME)

$(GENDIR)/%-list.txt: src/basic/generate-%-list.sh | $(GENDIR)
	bash $< $(CPP) $(SYSTEM_INCLUDES) > $@

$(GENDIR)/%-to-name.inc: src/basic/%-to-name.awk $(GENDIR)/%-list.txt
	$(AWK) -f $< $(GENDIR)/$*-list.txt > $@

$(GENDIR)/errno-from-name.gperf: $(GENDIR)/errno-list.txt
	$(PYTHON) tools/generate-gperfs.py errno '' $< '<errno.h>' > $@

$(GENDIR)/%-from-name.inc: $(GENDIR)/%-from-name.gperf
	$(GPERF) -L ANSI-C -t --ignore-case -N lookup_$* -H hash_$*_name -p -C $< > $@

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

BASIC_SOURCES = $(wildcard src/basic/*.c)
FUNDAMENTAL_SOURCES = $(wildcard src/fundamental/*.c)
LIBC_SOURCES = $(wildcard src/libc/*.c)
ifeq ($(LIBC),musl)
LIBC_SOURCES += $(wildcard src/libc/musl/*.c)
endif
LIBSYSTEMD_SOURCES = $(wildcard src/libsystemd/*/*.c)

BASIC_OBJECTS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(BASIC_SOURCES) $(FUNDAMENTAL_SOURCES))
LIBC_OBJECTS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(LIBC_SOURCES))
LIBSYSTEMD_OBJECTS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(LIBSYSTEMD_SOURCES))
OBJECTS = $(BASIC_OBJECTS) $(LIBC_OBJECTS) $(LIBSYSTEMD_OBJECTS)

# ---------------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------------

$(BUILDDIR)/libsystemd/%.o: src/libsystemd/%.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SYSTEM_INCLUDES) $(LIBSYSTEMD_INCLUDES) $(BASIC_INCLUDES) -c $< -o $@

$(BUILDDIR)/%.o: src/%.c $(GEN_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SYSTEM_INCLUDES) $(BASIC_INCLUDES) -c $< -o $@

SONAME = libsystemd.so.0

$(BUILDDIR)/$(SONAME): $(OBJECTS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) -shared -Wl,--no-undefined \
		-Wl,--version-script=$(VERSION_SCRIPT) \
		-Wl,-soname,$(SONAME) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILDDIR)/libsystemd.so: $(BUILDDIR)/$(SONAME)
	ln -sf $(SONAME) $@

$(GENDIR):
	mkdir -p $@

# ---------------------------------------------------------------------------
# Installation
#
# ---------------------------------------------------------------------------

PUBLIC_HEADERS = $(filter-out include/sd-future.h,$(wildcard include/*.h))

PKGCONFIG_FILE = $(BUILDDIR)/libsystemd.pc

$(PKGCONFIG_FILE): Makefile | $(BUILDDIR)
	printf '%s\n' \
		'prefix=$(prefix)' \
		'exec_prefix=$${prefix}' \
		'libdir=$(libdir)' \
		'includedir=$(includedir)' \
		'' \
		'Name: libsystemd' \
		'Description: systemd Library' \
		'Version: $(VERSION)' \
		'Libs: -L$${libdir} -lsystemd' \
		'Cflags: -I$${includedir}' \
		> $@

$(BUILDDIR):
	mkdir -p $@

.PHONY: all clean install
all: $(BUILDDIR)/libsystemd.so $(BUILDDIR)/$(SONAME)

install: all $(PKGCONFIG_FILE)
	$(INSTALL) -d $(DESTDIR)$(includedir)/systemd
	$(INSTALL) -m 644 $(PUBLIC_HEADERS) $(DESTDIR)$(includedir)/systemd
	$(INSTALL) -d $(DESTDIR)$(libdir)
	$(INSTALL) -m 755 $(BUILDDIR)/$(SONAME) $(DESTDIR)$(libdir)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(libdir)/libsystemd.so
	$(INSTALL) -d $(DESTDIR)$(pkgconfigdir)
	$(INSTALL) -m 644 $(PKGCONFIG_FILE) $(DESTDIR)$(pkgconfigdir)/libsystemd.pc

clean:
	rm -rf $(BUILDDIR)

-include $(OBJECTS:.o=.d)

.PRECIOUS: $(GENDIR)/%-list.txt $(GENDIR)/%-from-name.gperf
