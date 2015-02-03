## Process this file with automake to produce Makefile.in

CLEANFILES =

AM_CPPFLAGS = \
    -DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
    -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    $(GLIB_CFLAGS) \
    $(GSTREAMER_CFLAGS)

AM_CFLAGS = \
    -Wall \
    -Wextra \
    -Werror \
    $(DEBUG_CFLAGS)

lib_LTLIBRARIES = libopenwebrtc.la

libopenwebrtc_la_SOURCES = \
    owr.c \
    owr_types.c \
    owr_media_source.c \
    owr_utils.c

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

libopenwebrtc_la_LDFLAGS = -export-dynamic -version-info 3601

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
endif

includedir = $(prefix)/include/owr
include_HEADERS = \
    owr.h \
    owr_media_source.h \
    owr_types.h

noinst_HEADERS = \
    Owr-0.1.gir.h \
    owr_private.h \
    owr_media_source_private.h \
    owr_utils.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = openwebrtc-0.1.pc

EXTRA_DIST = \
    openwebrtc-0.1.pc.in

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

Owr-0.1.gir: libopenwebrtc.la
Owr_0_1_gir_INCLUDES = GObject-2.0
Owr_0_1_gir_CFLAGS = \
    $(INCLUDES) \
    -I$(top_srcdir)/owr \
    -I$(top_srcdir)/transport \
    -I$(top_srcdir)/local
Owr_0_1_gir_LIBS = libopenwebrtc.la
Owr_0_1_gir_FILES = \
    owr.h \
    owr.c \
    owr_types.h \
    owr_types.c \
    owr_media_source.h \
    owr_media_source.c \
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

INTROSPECTION_GIRS += Owr-0.1.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

Owr-0.1.gir.h: Owr-0.1.gir
	xxd -i $< > $@
	shasum -a 1 -b < $< | head -c 40 > $<.sha1
	xxd -i $<.sha1 >> $@
	rm -f $<.sha1

CLEANFILES += $(gir_DATA) $(typelib_DATA) Owr-0.1.gir.h
else
Owr-0.1.gir.h:
	cp -f `find $(top_srcdir) -name Owr-0.1.gir.h || echo "Owr-0.1.gir.h"` .
endif

-include $(top_srcdir)/git.mk
