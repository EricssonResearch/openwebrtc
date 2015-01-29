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
	$(top_builddir)/owr/libopenwebrtc.la

libopenwebrtc_bridge_jni_la_SOURCES = owr_bridge_jni.c
libopenwebrtc_bridge_jni_la_LIBADD = \
	$(top_builddir)/bindings/java/libopenwebrtc_jni.la \
	$(top_builddir)/bridge/libopenwebrtc_bridge.la

BUILT_SOURCES = owr_jni.c
noinst_DATA = \
	gen/owr/com/ericsson/research \
	gen/owr/com/ericsson/research/owr \
	gen/bridge/com/ericsson/research/owr \
	javadoc
CLEANFILES = owr_jni.c owr gen javadoc

if HAVE_INTROSPECTION
OWR_GIR = $(top_builddir)/owr/Owr-0.1.gir
else
# You need to provide this externally
OWR_GIR = Owr-0.1.gir
endif

owr_jni.c: $(OWR_GIR) $(top_srcdir)/bindings/java/
	PYTHONDONTWRITEBYTECODE=x $(top_srcdir)/bindings/java/gen_jni.py \
	--gir=$< \
	--c-out=$(top_builddir)/bindings/java/owr_jni.c \
	--j-out=$(top_builddir)/bindings/java/owr

Owr-0.1.gir:
	cat `find $(top_srcdir)/out -name Owr-0.1.gir || echo "Owr-0.1.gir"` | sed 's/shared-library="[^"]*"/shared-library="libopenwebrtc_jni.so"/' > Owr-0.1.gir

gen/bridge/%: bridge/%
	mkdir -p gen/bridge
	$(JAVAC) -d gen/bridge $</*.java

gen/owr/%: owr/%
	mkdir -p gen/owr
	$(JAVAC) -classpath '$(ANDROID_CLASSPATH):gen/owr/' -d gen/owr $+/*.java

javadoc: owr
	$(JAVADOC) -d javadoc -sourcepath owr -subpackages com.ericsson.research.owr -classpath '$(ANDROID_CLASSPATH)'

# This must be done in install because we need the installed version of
# the shared library created by libtool, not the "uninstalled" version
install-data-local: install-exec
	mkdir -p '$(DESTDIR)/$(javadocdir)'
	cp -r javadoc/* '$(DESTDIR)/$(javadocdir)'
	mkdir -p '$(DESTDIR)/$(jardir)'
	rm -rf jar-build && mkdir -p jar-build/lib/armeabi
	for eachlib in '$(DESTDIR)/$(libdir)'/libopenwebrtc{,_bridge}{,_jni}.so; do \
		cp -L $$eachlib jar-build/lib/armeabi; \
	done
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc.jar' \
		-C gen/owr com/ericsson/research/owr \
		-C gen/owr com/ericsson/research \
		-C jar-build lib/armeabi/libopenwebrtc.so \
		-C jar-build lib/armeabi/libopenwebrtc_jni.so
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc-source.jar' \
		-C owr com/ericsson/research/owr \
		-C owr com/ericsson/research
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc_bridge.jar' \
		-C gen/bridge com/ericsson/research/owr \
		-C jar-build lib/armeabi/libopenwebrtc_bridge.so \
		-C jar-build lib/armeabi/libopenwebrtc_bridge_jni.so
	rm -rf jar-build

-include $(top_srcdir)/git.mk
