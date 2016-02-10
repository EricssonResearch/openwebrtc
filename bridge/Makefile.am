## Process this file with automake to produce Makefile.in

SUBDIRS = client seed shared worker

AM_CPPFLAGS = \
    -DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
    -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    $(GLIB_CFLAGS) \
    $(SEED_CFLAGS) \
    -I$(top_srcdir)/owr \
    -I$(top_builddir)/owr \
    -I$(top_builddir)/bridge

AM_CFLAGS = \
    -Wall \
    -Wextra \
    -Werror \
    $(DEBUG_CFLAGS)

lib_LTLIBRARIES = libopenwebrtc_bridge.la

libopenwebrtc_bridge_la_SOURCES = \
    owr_bridge.c

libopenwebrtc_bridge_la_LIBADD = \
    $(GLIB_LIBS) \
    $(SEED_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

libopenwebrtc_bridge_la_DEPENDENCIES = \
    $(top_builddir)/owr/libopenwebrtc.la

libopenwebrtc_bridge_la_CFLAGS = $(AM_CFLAGS)
if OWR_STATIC
libopenwebrtc_bridge_la_CFLAGS += -DOWR_GIR_FILE="\"Owr-@OWR_API_VERSION@.gir.h\"" -DOWR_GIR_VAR_PREFIX="Owr_@OWR_API_VERSION_UNDERSCORE@"
endif
libopenwebrtc_bridge_la_LDFLAGS = -export-dynamic

bin_PROGRAMS = openwebrtc-daemon
openwebrtc_daemon_SOURCES = daemon.c
openwebrtc_daemon_LDFLAGS =
if OWR_STATIC
openwebrtc_daemon_LDFLAGS += -static-libtool-libs
endif
openwebrtc_daemon_LDFLAGS += -static -export-dynamic
openwebrtc_daemon_LDADD = \
    libopenwebrtc_bridge.la \
    $(top_builddir)/owr/libopenwebrtc.la \
    $(OPENWEBRTC_GST_PLUGINS_LIBS) \
    $(GSTREAMER_LIBS) \
    $(ORC_LIBS) \
    $(SEED_LIBS) \
    $(GLIB_LIBS)

includedir = $(prefix)/include/owr
include_HEADERS = \
    owr_bridge.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = openwebrtc-bridge-@OWR_API_VERSION@.pc

EXTRA_DIST = \
    openwebrtc-bridge-@OWR_API_VERSION@.pc.in

-include $(top_srcdir)/git.mk
