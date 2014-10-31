## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir)/owr \
	-I$(top_srcdir)/bridge \
	-I$(top_srcdir)/transport \
	-I$(top_srcdir)/local

AM_CFLAGS = \
	-Wall \
	-Wextra \
	$(DEBUG_CFLAGS)

noinst_LTLIBRARIES = libopenwebrtc_jni.la libopenwebrtc_bridge_jni.la

libopenwebrtc_jni_la_SOURCES = owr_jni.c

BUILT_SOURCES = owr_jni.c

CLEANFILES = owr_jni.c

owr_jni.c: Owr-0.1.gir
	$(top_srcdir)/gen_jni/gen_jni.py \
	--gir=Owr-0.1.gir \
	--c-out=$(top_builddir)/gen_jni \
	--j-out=$(top_builddir)/gen_jni/java

Owr-0.1.gir:
	cp -f `find $(top_srcdir)/out -name Owr-0.1.gir || echo "Owr-0.1.gir"` .


libopenwebrtc_bridge_jni_la_SOURCES = owr_bridge_jni.c

-include $(top_srcdir)/git.mk
