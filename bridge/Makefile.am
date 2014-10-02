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

libopenwebrtc_bridge_la_CFLAGS = $(AM_CFLAGS) -ansi
libopenwebrtc_bridge_la_LDFLAGS = -export-dynamic

bin_PROGRAMS = daemon
daemon_SOURCES = daemon.c
daemon_LDFLAGS = -static -export-dynamic $(SEED_LIBS)
daemon_LDADD = libopenwebrtc_bridge.la

includedir = $(prefix)/include/owr
include_HEADERS = \
    owr_bridge.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = openwebrtc-bridge-0.1.pc

EXTRA_DIST = \
    openwebrtc-bridge-0.1.pc.in

-include $(top_srcdir)/git.mk
