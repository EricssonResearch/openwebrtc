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
    -ansi \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic

bin_PROGRAMS = \
    list-devices \
    test-self-view \
    test-send-receive

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

-include $(top_srcdir)/git.mk
