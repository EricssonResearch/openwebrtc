## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
    -DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
    -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    $(GLIB_CFLAGS) \
    $(GSTREAMER_CFLAGS) \
    -I$(top_srcdir)/owr \
    -I$(top_srcdir)/local

AM_CFLAGS = \
    -Wall \
    -Wextra \
    -Werror \
    -Wno-parentheses-equality \
    $(DEBUG_CFLAGS)

lib_LTLIBRARIES = libopenwebrtc_gst.la

libopenwebrtc_gst_la_SOURCES = \
    owr_gst_audio_renderer.c \
    owr_gst_media_source.c \
    owr_gst_video_renderer.c

libopenwebrtc_gst_la_LIBADD = \
    $(top_builddir)/owr/libopenwebrtc.la \
    $(GSTREAMER_LIBS) \
    $(GLIB_LIBS)

libopenwebrtc_gst_la_DEPENDENCIES = \
    $(top_builddir)/owr/libopenwebrtc.la

includedir = $(prefix)/include/owr
include_HEADERS = \
    owr_gst_audio_renderer.h \
    owr_gst_media_source.h \
    owr_gst_video_renderer.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = openwebrtc-gst-@OWR_API_VERSION@.pc

EXTRA_DIST = \
    openwebrtc-gst-@OWR_API_VERSION@.pc.in

-include $(top_srcdir)/git.mk
