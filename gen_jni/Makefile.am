## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-I$(ANDROID_NDK_PREFIX)/include \
	-I$(top_srcdir)/owr \
	-I$(top_srcdir)/bridge \
	-I$(top_srcdir)/transport \
	-I$(top_srcdir)/local

AM_CFLAGS = \
	-Wall \
	-Wextra \
	$(DEBUG_CFLAGS)

lib_LTLIBRARIES = libopenwebrtc_jni.la libopenwebrtc_bridge_jni.la

libopenwebrtc_jni_la_SOURCES = owr_jni.c
libopenwebrtc_jni_la_LIBADD = \
	$(OPENWEBRTC_GST_PLUGINS_LIBS) \
	$(top_builddir)/owr/libopenwebrtc_static.la

libopenwebrtc_bridge_jni_la_SOURCES = owr_bridge_jni.c
libopenwebrtc_bridge_jni_la_LIBADD = \
	$(SEED_LIBS) \
	$(top_builddir)/bridge/libopenwebrtc_bridge_static.la

BUILT_SOURCES = owr_jni.c
noinst_DATA = \
	gen/owr/com/ericsson/research/owr \
	gen/bridge/com/ericsson/research/owr
CLEANFILES = owr_jni.c owr gen

if HAVE_INTROSPECTION
OWR_GIR = $(top_builddir)/owr/Owr-0.1.gir
else
# You need to provide this externally
OWR_GIR = $(top_builddir)/Owr-0.1.gir
endif

owr_jni.c: $(OWR_GIR)
	$(top_srcdir)/gen_jni/gen_jni.py \
	--gir=$< \
	--c-out=$(top_builddir)/gen_jni \
	--j-out=$(top_builddir)/gen_jni/owr

gen/bridge/%: bridge/%
	mkdir -p gen/bridge
	$(JAVAC) -d gen/bridge $</*.java

gen/owr/%: owr/%
	mkdir -p gen/owr
	$(JAVAC) -classpath '$(ANDROID_CLASSPATH)' -d gen/owr $</*.java

# This must be done in install because we need the installed version of
# the shared library created by libtool, not the "uninstalled" version
install-data-local: install-exec
	mkdir -p '$(DESTDIR)/$(jardir)'
	rm -rf jar-build && mkdir -p jar-build/lib/armeabi
	for eachlib in '$(DESTDIR)/$(libdir)'/libopenwebrtc{,_bridge}_jni.so; do \
		cp -L $$eachlib jar-build/lib/armeabi; \
	done
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc.jar' \
		-C gen/owr com/ericsson/research/owr \
		-C jar-build lib/armeabi/libopenwebrtc_jni.so
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc_bridge.jar' \
		-C gen/bridge com/ericsson/research/owr \
		-C jar-build lib/armeabi/libopenwebrtc_bridge_jni.so
	rm -rf jar-build

-include $(top_srcdir)/git.mk
