## Process this file with automake to produce Makefile.in

CLEANFILES =

AM_CPPFLAGS = \
    -DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
    -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    $(GLIB_CFLAGS) \
    $(GSTREAMER_CFLAGS) \
    $(OWR_DEVICE_LIST_EXT_CFLAGS)

AM_CFLAGS = \
    -Wall \
    -Wextra \
    -Werror \
    -Wno-parentheses-equality \
    $(DEBUG_CFLAGS)

lib_LTLIBRARIES = libopenwebrtc.la

libopenwebrtc_la_SOURCES = \
    owr.c \
    owr_types.c \
    owr_media_source.c \
    owr_bus.c \
    owr_message_origin.c \
    owr_utils.c \
    owr_inter_src.c \
    owr_inter_sink.c

libopenwebrtc_la_LIBADD = \
    $(top_builddir)/local/libopenwebrtc_local.la \
    $(top_builddir)/transport/libopenwebrtc_transport.la \
    $(OPENWEBRTC_GST_PLUGINS_LIBS) \
    $(GSTREAMER_LIBS) \
    $(GLIB_LIBS) \
    $(ORC_LIBS) \
    -lm

libopenwebrtc_la_DEPENDENCIES = \
    $(top_builddir)/local/libopenwebrtc_local.la \
    $(top_builddir)/transport/libopenwebrtc_transport.la

# 4201 is the glib version info
libopenwebrtc_la_LDFLAGS = -export-dynamic -version-info 4201

if OWR_STATIC
libopenwebrtc.sym: $(top_srcdir)/libopenwebrtc.exp
	python ${srcdir}/symbols_to_sym.py $(top_srcdir)/libopenwebrtc.exp $@ ${host}
BUILT_SOURCES = libopenwebrtc.sym
libopenwebrtc_la_LDFLAGS += -export-symbols libopenwebrtc.sym
libopenwebrtc_la_SOURCES += \
    owr_require_symbols.c
CLEANFILES += owr_require_symbols.c
owr_require_symbols.c: symbols_to_source.py
	python ${srcdir}/symbols_to_source.py $(top_srcdir)/libopenwebrtc.exp $@ ${host}
else
libopenwebrtc_la_LDFLAGS += -export-symbols-regex '^owr_'
endif

includedir = $(prefix)/include/owr
include_HEADERS = \
    owr.h \
    owr_media_source.h \
    owr_bus.h \
    owr_message_origin.h \
    owr_types.h

noinst_HEADERS = \
    owr_private.h \
    owr_bus_private.h \
    owr_media_source_private.h \
    owr_message_origin_private.h \
    owr_utils.h \
    owr_inter_src.h \
    owr_inter_sink.h

if OWR_STATIC
noinst_HEADERS += \
    Owr-@OWR_API_VERSION@.gir.h
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = openwebrtc-@OWR_API_VERSION@.pc

EXTRA_DIST = \
    openwebrtc-@OWR_API_VERSION@.pc.in

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all

if !OWR_STATIC
INTROSPECTION_SCANNER_ARGS += --pkg gstreamer-1.0 --pkg gstreamer-video-1.0 --pkg gstreamer-app-1.0
endif

INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)
if OWR_STATIC
INTROSPECTION_COMPILER_ARGS += --shared-library=""
endif

if HAVE_INTROSPECTION
introspection_sources = $(libopenwebrtc_la_SOURCES)

Owr-@OWR_API_VERSION@.gir: libopenwebrtc.la
Owr_@OWR_API_VERSION_UNDERSCORE@_gir_INCLUDES = GObject-2.0
Owr_@OWR_API_VERSION_UNDERSCORE@_gir_CFLAGS = \
    $(INCLUDES) \
    -I$(top_srcdir)/owr \
    -I$(top_srcdir)/transport \
    -I$(top_srcdir)/local
Owr_@OWR_API_VERSION_UNDERSCORE@_gir_LIBS = libopenwebrtc.la
Owr_@OWR_API_VERSION_UNDERSCORE@_gir_FILES = \
    owr.h \
    owr.c \
    owr_types.h \
    owr_types.c \
    owr_media_source.h \
    owr_media_source.c \
    owr_bus.h \
    owr_bus.c \
    owr_message_origin.h \
    owr_message_origin.c \
    ../transport/owr_candidate.h \
    ../transport/owr_candidate.c \
    ../transport/owr_session.h \
    ../transport/owr_session.c \
    ../transport/owr_data_channel.h \
    ../transport/owr_data_channel.c \
    ../transport/owr_data_session.h \
    ../transport/owr_data_session.c \
    ../transport/owr_media_session.h \
    ../transport/owr_media_session.c \
    ../transport/owr_payload.h \
    ../transport/owr_payload.c \
    ../transport/owr_audio_payload.h \
    ../transport/owr_audio_payload.c \
    ../transport/owr_video_payload.h \
    ../transport/owr_video_payload.c \
    ../transport/owr_transport_agent.h \
    ../transport/owr_transport_agent.c \
    ../transport/owr_remote_media_source.h \
    ../transport/owr_remote_media_source.c \
    ../transport/owr_crypto_utils.h \
    ../transport/owr_crypto_utils.c \
    ../local/owr_local.h \
    ../local/owr_local.c \
    ../local/owr_local_media_source.h \
    ../local/owr_local_media_source.c \
    ../local/owr_media_renderer.h \
    ../local/owr_media_renderer.c \
    ../local/owr_audio_renderer.h \
    ../local/owr_audio_renderer.c \
    ../local/owr_video_renderer.h \
    ../local/owr_video_renderer.c \
    ../local/owr_image_renderer.h \
    ../local/owr_image_renderer.c \
    ../local/owr_image_server.h \
    ../local/owr_image_server.c \
    ../local/owr_window_registry.h \
    ../local/owr_window_registry.c

INTROSPECTION_GIRS += Owr-@OWR_API_VERSION@.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

Owr-@OWR_API_VERSION@.gir.h: Owr-@OWR_API_VERSION@.gir
	xxd -i $< > $@
	shasum -a 1 -b < $< | head -c 40 > $<.sha1
	xxd -i $<.sha1 >> $@
	rm -f $<.sha1

CLEANFILES += $(gir_DATA) $(typelib_DATA) Owr-@OWR_API_VERSION@.gir.h
else
Owr-@OWR_API_VERSION@.gir.h:
	cp -f `find $(top_srcdir) -name Owr-@OWR_API_VERSION@.gir.h || echo "Owr-@OWR_API_VERSION@.gir.h"` .
endif

-include $(top_srcdir)/git.mk
