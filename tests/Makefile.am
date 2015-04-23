AM_CPPFLAGS = \
    -DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
    -DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    $(GLIB_CFLAGS) \
    $(NICE_CFLAGS) \
    $(GSTREAMER_CFLAGS) \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/transport \
    -I$(top_srcdir)/owr

AM_CFLAGS = \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic

bin_PROGRAMS = \
    list-devices \
    test-self-view \
    test-send-receive \
    test-data-channel \
    test-init \
    test-client

if OWR_GST
AM_CPPFLAGS += \
    -I$(top_srcdir)/gst
bin_PROGRAMS += \
    test-gst-io

test_gst_io_SOURCES = test_gst_io.c

test_gst_io_CFLAGS = \
    -I$(top_srcdir)/gst \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/owr

test_gst_io_LDADD = \
    $(GSTREAMER_LIBS) \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la \
    $(top_builddir)/gst/libopenwebrtc_gst.la
endif

list_devices_SOURCES = list_devices.c

list_devices_CFLAGS = \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/owr

list_devices_LDADD = \
    $(GSTREAMER_LIBS) \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

test_self_view_SOURCES = test_self_view.c test_utils.c

test_self_view_CFLAGS = \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/owr

test_self_view_LDADD = \
    $(GSTREAMER_LIBS) \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

test_send_receive_SOURCES = test_send_receive.c test_utils.c

test_send_receive_CFLAGS = \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/transport \
    -I$(top_srcdir)/owr

test_send_receive_LDADD = \
    $(GSTREAMER_LIBS) \
    $(NICE_LIBS) \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

test_data_channel_SOURCES = test_data_channel.c test_utils.c

test_data_channel_CFLAGS = \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/transport \
    -I$(top_srcdir)/owr

test_data_channel_LDADD = \
    $(GSTREAMER_LIBS) \
    $(NICE_LIBS) \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

test_init_SOURCES = test_init.c

test_init_CFLAGS = \
    $(AM_CFLAGS) \
    -I$(top_srcdir)/owr

test_init_LDADD = \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

test_client_SOURCES = test_client.c

test_client_CFLAGS = \
    $(AM_CFLAGS) \
    $(JSON_GLIB_CFLAGS) \
    $(LIBSOUP_CFLAGS) \
    -I$(top_srcdir)/local \
    -I$(top_srcdir)/transport \
    -I$(top_srcdir)/owr

test_client_LDADD = \
    $(JSON_GLIB_LIBS) \
    $(LIBSOUP_LIBS) \
    $(GLIB_LIBS) \
    $(top_builddir)/owr/libopenwebrtc.la

-include $(top_srcdir)/git.mk
