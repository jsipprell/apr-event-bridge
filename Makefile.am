AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I build
AM_CPPFLAGS = @EXTRA_CPPFLAGS@
AM_CFLAGS = $(DEBUG_CFLAGS) -Wall
LIBAEB_ABI_MAJOR = @LIBAEB_ABI_MAJOR@
LIBAEB_ABI_COMPAT = @LIBAEB_ABI_COMPAT@
LIBAEB_API_VERSION = @LIBAEB_API_VERSION@
LIBAEB_REVISION = @LIBAEB_REVISION@

AEBINC = $(top_srcdir)/include
AEBSRC = $(top_srcdir)/src

LIBEVENT_PATCHES = libs/libevent-2.0.21-stable.automake.patch
DIST_COMMON = $(top_srcdir)/libs/.dist-stamp $(DIST_SOURCES) $(TEXINFOS) $(EXTRA_DIST)
EXTRA_BUILD_DIRS =

EXTRA_DIST = $(LIBEVENT_PATCHES) \
             LICENSE README.threads.md \
             build/find_apu.m4 build/find_apr.m4 \
             build/attribute_may_alias.m4 \
             build/visibility_hidden.m4 \
             build/aeb_event_check_threads.m4 \
             build/aeb_has_threads.m4 \
             build/acx_pthread.m4 \
             build/m4/aeb_args.m4

if BUILD_INCLUDED_LIBEVENT
EXTRA_BUILD_DIRS += libs/libevent
endif

DIST_SUBDIRS = . libs/libevent
SUBDIRS = $(EXTRA_BUILD_DIRS) .

if INSTALL_PKGCONFIG
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = src/apr_event_bridge.pc
endif

lib_LTLIBRARIES = libaeb.la
noinst_LTLIBRARIES = libaeb-static.la libcompat.la
noinst_PROGRAMS = test-aeb

libcompat_la_SOURCES = lib/stub.c include/compat.h
libcompat_la_LIBADD = @LTLIBOBJS@

libaeb_la_includedir = $(includedir)/apr_event_bridge
libaeb_la_include_HEADERS = $(AEBINC)/libaeb.h \
                            $(AEBINC)/libaeb_version.h \
                            $(AEBINC)/libaeb_config.h \
                            $(AEBINC)/libaeb_assert.h \
                            $(AEBINC)/libaeb_event_types.h \
                            $(AEBINC)/libaeb_event_info.h

libaeb_la_CPPFLAGS = @LIBAEB_EXTRA_CPPFLAGS@ $(AM_CPPFLAGS)
libaeb_la_CFLAGS = @LIBAEB_EXTRA_CFLAGS@ $(AM_CPPFLAGS)
libaeb_la_LDFLAGS = -version-info $(LIBAEB_ABI_MAJOR):$(LIBAEB_REVISION):$(LIBAEB_ABI_COMPAT)

libaeb_la_SOURCES = $(libaeb_la_include_HEADERS) \
                    include/internal.h include/compat.h include/event_types.h \
                    include/util.h src/util.c \
                    include/weakref.h src/weakref.c \
                    src/base.c \
                    src/event.c src/event_info.c \
                    src/timers.c \
                    src/loop.c

libaeb_la_LIBADD = libcompat.la @LIBAEB_EXTRA_LIBS@

libaeb_static_la_CPPFLAGS = @LIBAEB_EXTRA_CPPFLAGS@ $(AM_CPPFLAGS)
libaeb_static_la_CFLAGS = @LIBAEB_EXTRA_CFLAGS@ $(AM_CFLAGS)
libaeb_static_la_SOURCES = $(libaeb_la_SOURCES)
libaeb_static_la_LIBADD = libcompat.la @LIBAEB_EXTRA_LIBS@

test_aeb_SOURCES = src/test.c
test_aeb_LDADD = @LIBAEB_LIBS@ libaeb-static.la

clean-local:
	@rm -f $(top_srcdir)/confdefs.h $(top_srcdir)/conftest.*
	@rm -f $(top_srcdir)/libs/.dist-stamp

distclean-local:
	@rm -f $(top_srcdir)/confdefs.h $(top_srcdir)/conftest.*

$(top_srcdir)/libs/.dist-stamp:
	-test -f $(top_srcdir)/libs/libevent/configure || $(am__cd) $(top_srcdir)/libs/libevent && ./autogen.sh
	-test -f $(top_srcdir)/libs/libevent/Makefile || $(am__cd) $(top_srcdir)/libs/libevent && ./configure
	-touch $(top_srcdir)/libs/.dist-stamp

distclean: distclean-am
	@test -f $(top_srcdir)/libs/libevent/Makefile && $(MAKE) $(AM_MAKEFLAGS) distclean-recursive ||:
	rm -f $(am__CONFIG_DISCLEAN_FILES)
	rm -rf lib/$(DEPDIR) src/$(DEPDIR)
	rm -f Makefile
