## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
    -DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
    -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    $(GLIB_CFLAGS) \
    $(GSTREAMER_CFLAGS) \
    $(OWR_DEVICE_LIST_EXT_CFLAGS) \
    -I$(top_srcdir)/owr

AM_CFLAGS = \
    -Wall \
    -Wextra \
    -Werror \
    -Wno-parentheses-equality \
    $(DEBUG_CFLAGS)

noinst_LTLIBRARIES = libopenwebrtc_local.la

libopenwebrtc_local_la_SOURCES = \
    owr_local.c \
    owr_local_media_source.c \
    owr_media_renderer.c \
    owr_audio_renderer.c \
    owr_video_renderer.c \
    owr_image_renderer.c \
    owr_image_server.c \
    owr_uri_source.c \
    owr_uri_source_agent.c \
    owr_window_registry.c \
    owr_device_list.c

if TARGET_APPLE
libopenwebrtc_local_la_SOURCES += owr_device_list_avf.m
endif

libopenwebrtc_local_la_LIBADD = \
    $(GSTREAMER_LIBS) \
    $(GLIB_LIBS) \
    $(OWR_DEVICE_LIST_EXT_LIBS)

includedir = $(prefix)/include/owr
include_HEADERS = \
    owr_local.h \
    owr_local_media_source.h \
    owr_media_renderer.h \
    owr_audio_renderer.h \
    owr_video_renderer.h \
    owr_image_renderer.h \
    owr_image_server.h \
    owr_uri_source.h \
    owr_uri_source_agent.h \
    owr_window_registry.h

noinst_HEADERS = \
    owr_local_media_source_private.h \
    owr_image_renderer_private.h \
    owr_uri_source_private.h \
    owr_window_registry_private.h

if TARGET_APPLE
noinst_HEADERS += owr_device_list_avf_private.h
endif

-include $(top_srcdir)/git.mk
