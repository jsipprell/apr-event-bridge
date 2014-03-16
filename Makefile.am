ACLOCAL_AMFLAGS = -I build
AM_CPPFLAGS = @LIBAEB_EXTRA_CPPFLAGS@
AM_CFLAGS = $(DEBUG_CFLAGS) @PLATFORM_CFLAGS@ -Wall
LIBAEB_ABI_MAJOR = @LIBAEB_ABI_MAJOR@
LIBAEB_ABI_COMPAT = @LIBAEB_ABI_COMPAT@
LIBAEB_API_VERSION = @LIBAEB_API_VERSION@
LIBAEB_REVISION = @LIBAEB_REVISION@

AEBINC = $(top_srcdir)/include
AEBSRC = $(top_srcdir)/src

EXTRA_DIST = LICENSE build/find_apu.m4 build/find_apr.m4 \
                     build/attribute_may_alias.m4 \
                     build/visibility_hidden.m4

SUBDIRS = .

if INSTALL_PKGCONFIG
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = src/apr_event_bridge.pc
endif

lib_LTLIBRARIES = libaeb.la
noinst_LTLIBRARIES = libaeb-static.la libcompat.la

libaeb_la_includedir = $(includedir)/apr_event_bridge
libaeb_la_include_HEADERS = $(AEBINC)/libaeb.h \
                            $(AEBINC)/libaeb_version.h \
                            $(AEBINC)/libaeb_config.h \
                            $(AEBINC)/libaeb_assert.h

libaeb_la_LDFLAGS = -version-info $(LIBAEB_ABI_MAJOR):$(LIBAEB_REVISION):$(LIBAEB_ABI_COMPAT)

libcompat_la_SOURCES = lib/stub.c include/compat.h
libcompat_la_LIBADD = @LTLIBOBJS@

libaeb_la_SOURCES = include/internal.h include/compat.h include/util.h \
                    src/util.c \
                    src/event.c
libaeb_la_LIBADD = libcompat.la

libaeb_static_la_SOURCES = $(libaeb_la_SOURCES)
libaeb_static_la_LIBADD = libcompat.la
