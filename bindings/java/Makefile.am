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

lib_LTLIBRARIES = libopenwebrtc_jni.la

if OWR_BRIDGE
lib_LTLIBRARIES += libopenwebrtc_bridge_jni.la
endif

libopenwebrtc_jni_la_SOURCES = owr_jni.c
libopenwebrtc_jni_la_LIBADD = \
	-L$(ANDROID_NDK_PREFIX)/lib -landroid \
	$(top_builddir)/owr/libopenwebrtc.la

if OWR_BRIDGE
libopenwebrtc_bridge_jni_la_SOURCES = owr_bridge_jni.c
libopenwebrtc_bridge_jni_la_LIBADD = \
	$(top_builddir)/bindings/java/libopenwebrtc_jni.la \
	$(top_builddir)/bridge/libopenwebrtc_bridge.la
endif

BUILT_SOURCES = owr_jni.c
noinst_DATA = \
	gen/owr/com/ericsson/research \
	gen/owr/com/ericsson/research/owr \
	gen/bridge/com/ericsson/research/owr \
	javadoc
if OWR_BRIDGE
noinst_DATA += javadoc-bridge
endif

CLEANFILES = owr_jni.c owr gen javadoc javadoc-bridge

if HAVE_INTROSPECTION
OWR_GIR = $(top_builddir)/owr/Owr-@OWR_API_VERSION@.gir
else
# You need to provide this externally
OWR_GIR = Owr-@OWR_API_VERSION@.gir
endif

owr_jni.c: $(OWR_GIR) $(top_srcdir)/bindings/java/
	PYTHONDONTWRITEBYTECODE=1 $(top_srcdir)/bindings/java/gen_jni.py \
	--gir=$< \
	--c-out=$(top_builddir)/bindings/java/owr_jni.c \
	--j-out=$(top_builddir)/bindings/java/owr

Owr-@OWR_API_VERSION@.gir:
	cat `find $(top_srcdir)/out -name Owr-@OWR_API_VERSION@.gir || echo "Owr-@OWR_API_VERSION@.gir"` | sed 's/shared-library="[^"]*"/shared-library="libopenwebrtc_jni.so"/' > Owr-@OWR_API_VERSION@.gir

gen/bridge/%: bridge/%
	mkdir -p gen/bridge
	$(JAVAC) -source 1.7 -target 1.7 -d gen/bridge $</*.java

gen/owr/%: owr/%
	mkdir -p gen/owr
	$(JAVAC) -source 1.7 -target 1.7 -classpath '$(ANDROID_CLASSPATH):gen/owr/' -d gen/owr $+/*.java

JAVADOC_HEADER="OpenWebRTC $(VERSION) JavaDoc"

javadoc: owr
	$(JAVADOC) -d javadoc -sourcepath owr -subpackages com.ericsson.research.owr -classpath '$(ANDROID_CLASSPATH)' -windowtitle $(JAVADOC_HEADER)

if OWR_BRIDGE
javadoc-bridge: bridge
	$(JAVADOC) -d javadoc-bridge -sourcepath bridge -subpackages com.ericsson.research.owr -windowtitle $(JAVADOC_HEADER)
endif

# This must be done in install because we need the installed version of
# the shared library created by libtool, not the "uninstalled" version
install-data-local: install-exec
	mkdir -p '$(DESTDIR)/$(javadocdir)'
	cp -r javadoc/* '$(DESTDIR)/$(javadocdir)'
	mkdir -p '$(DESTDIR)/$(jardir)'
	rm -rf jar-build && mkdir -p jar-build/lib/armeabi
	for eachlib in '$(DESTDIR)/$(libdir)'/libopenwebrtc{,_jni}.so; do \
		cp -L $$eachlib jar-build/lib/armeabi; \
	done
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc.jar' \
		-C gen/owr com/ericsson/research/owr \
		-C gen/owr com/ericsson/research \
		-C jar-build lib/armeabi/libopenwebrtc.so \
		-C jar-build lib/armeabi/libopenwebrtc_jni.so
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc-sources.jar' \
		-C owr com/ericsson/research/owr \
		-C owr com/ericsson/research
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc-javadoc.jar' \
		-C javadoc/ .
if OWR_BRIDGE
	for eachlib in '$(DESTDIR)/$(libdir)'/libopenwebrtc_bridge{,_jni}.so; do \
		cp -L $$eachlib jar-build/lib/armeabi; \
	done
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc_bridge.jar' \
		-C gen/bridge com/ericsson/research/owr \
		-C jar-build lib/armeabi/libopenwebrtc_bridge.so \
		-C jar-build lib/armeabi/libopenwebrtc_bridge_jni.so
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc_bridge-sources.jar' \
		-C bridge com/ericsson/research/owr
	$(JAR) cvf '$(DESTDIR)/$(jardir)/openwebrtc_bridge-javadoc.jar' \
		-C javadoc-bridge/ .
endif
	rm -rf jar-build

-include $(top_srcdir)/git.mk
