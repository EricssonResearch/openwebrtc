dnl Process this file with autoconf to produce a configure script.

AC_INIT(openwebrtc, 0.1)

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR(m4)

AM_INIT_AUTOMAKE([1.11 -Wno-portability foreign])

AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_OBJC
AC_CANONICAL_SYSTEM

LT_INIT

LIBOPENWEBRTC_CFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_32 -DGLIB_DISABLE_DEPRECATION_WARNINGS"
AC_SUBST(LIBOPENWEBRTC_CFLAGS)

GST_REQUIRED=1.4
PKG_CHECK_MODULES(GLIB, [glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0])
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-1.0 >= $GST_REQUIRED gstreamer-rtp-1.0 >= $GST_REQUIRED gstreamer-video-1.0 >= $GST_REQUIRED gstreamer-app-1.0 >= $GST_REQUIRED])
PKG_CHECK_MODULES(NICE, [nice >= 0.1.7.1])
PKG_CHECK_MODULES(GSTREAMER_SCTP, [gstreamer-sctp-1.0])
PKG_CHECK_MODULES(ORC, [orc-0.4])
PKG_CHECK_MODULES(SEED, [seed])
PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
PKG_CHECK_MODULES(LIBSOUP, [libsoup-2.4])

# check for gobject-introspection
m4_ifdef([GOBJECT_INTROSPECTION_CHECK], [
GOBJECT_INTROSPECTION_CHECK([1.30.0])
],[
AM_CONDITIONAL([ENABLE_GOBJECT_INTROSPECTION], false)
])
AC_CONFIG_MACRO_DIR(m4)

m4_ifdef([GTK_DOC_CHECK], [
GTK_DOC_CHECK([1.18],[--flavour no-tmpl])
],[
AM_CONDITIONAL([ENABLE_GTK_DOC], false)
])

dnl build debug or not
AC_MSG_CHECKING([whether to build debug or not])
AC_ARG_ENABLE(debug,
AC_HELP_STRING(
  [--enable-debug],
  [Enable debug @<:@default=yes@:>@]),
[case "${enableval}" in
  yes) enable_debug=yes ;;
  no)  enable_debug=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac],[enable_debug=yes])
AC_MSG_RESULT([$enable_debug])
if test "x$enable_debug" = xyes; then
  AC_DEFINE(OWR_DEBUG, 1,
  [Define if building debug])
fi
AM_CONDITIONAL(OWR_DEBUG, test x$enable_debug = xyes)

dnl build static or not
AC_MSG_CHECKING([whether to build static owr or not])
AC_ARG_ENABLE(owr-static,
AC_HELP_STRING(
  [--enable-owr-static],
  [Enable static owr @<:@default=no@:>@]),
[case "${enableval}" in
  yes) enable_owr_static=yes ;;
  no)  enable_owr_static=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-owr-static) ;;
esac],[enable_owr_static=no])
AC_MSG_RESULT([$enable_owr_static])
if test "x$enable_owr_static" = xyes; then
  AC_DEFINE(OWR_STATIC, 1,
  [Define if building static])
fi
AC_SUBST(GST_STATIC_PLUGINS_DIR)
AM_CONDITIONAL(OWR_STATIC, test x$enable_owr_static = xyes)

dnl pick dependencies and build flags based on OS
case "$host_os" in
  darwin*)
    is_apple=yes
    OWR_DEVICE_LIST_EXT_LIBS="-framework AVFoundation"
    ;;
  linux-android*)
    ;;
  linux*)
    PKG_CHECK_MODULES(PULSE, [libpulse libpulse-mainloop-glib])
    PKG_CHECK_MODULES(V4L2, [libv4l2])
    OWR_DEVICE_LIST_EXT_CFLAGS="\$(PULSE_CFLAGS) \$(V4L2_CFLAGS)"
    OWR_DEVICE_LIST_EXT_LIBS="\$(PULSE_LIBS) \$(V4L2_LIBS)"
    ;;
esac
AC_SUBST(OWR_DEVICE_LIST_EXT_LIBS)
AM_CONDITIONAL(TARGET_APPLE, test x$is_apple = xyes)

dnl generate java bindings or not
AC_MSG_CHECKING([whether to generate java bindings or not])
AC_ARG_ENABLE(owr-java,
AC_HELP_STRING(
  [--enable-owr-java],
  [Enable java owr @<:@default=no@:>@]),
[case "${enableval}" in
  yes) enable_owr_java=yes ;;
  no)  enable_owr_java=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-owr-java) ;;
esac],[enable_owr_java=no])
AC_MSG_RESULT([$enable_owr_java])
if test "x$enable_owr_java" = xyes; then
  if test "x$enable_shared" != xyes; then
    AC_MSG_ERROR([--enable-owr-java needs --enabled-shared])
  fi
  AC_PATH_PROG(JAVAC, javac)
  if test -z "$JAVAC"; then
    AC_MSG_ERROR([You need 'javac' to build the Android bindings])
  fi
  AC_PATH_PROG(JAR, jar)
  if test -z "$JAR"; then
    AC_MSG_ERROR([You need 'jar' to build the Android bindings])
  fi
  AC_PATH_PROG(JAVADOC, javadoc)
  if test -z "$JAVADOC"; then
    AC_MSG_ERROR([You need 'javadoc' to build the Android bindings])
  fi
  AC_DEFINE(OWR_JAVA, 1,
  [Define if generating bindings for java])
fi
AM_CONDITIONAL(OWR_JAVA, test x$enable_owr_java = xyes)

dnl The Jar file installation path is needed as well
AC_MSG_CHECKING([for jar installation path])
AC_ARG_WITH(jardir,
            AS_HELP_STRING([--with-jardir],
                           [jar file installation path [LIBDIR/jni]]),
            jardir=${withval}, jardir=${libdir}/jni)
AC_MSG_RESULT([$jardir])
AC_SUBST(jardir)

dnl The Javadoc installation path is needed as well
AC_MSG_CHECKING([for javadoc installation path])
AC_ARG_WITH(javadocdir,
            AS_HELP_STRING([--with-javadocdir],
                           [javadoc file installation path [LIBDIR/share/javadoc]]),
            javadocdir=${withval}, javadocdir=${libdir}/share/javadoc/openwebrtc)
AC_MSG_RESULT([$javadocdir])
AC_SUBST(javadocdir)

dnl Android SDK path is needed when Java bindings are enabled
AC_ARG_WITH(android-sdk,
            AS_HELP_STRING([--with-android-sdk],
                           [path to the Android SDK]),
            androidsdkdir=${withval}, androidsdkdir=)
if test "x$enable_owr_java" = xyes; then
  AC_MSG_CHECKING([for android.jar])
  if test -a "x$androidsdkdir" = x; then
    AC_MSG_ERROR([Need path to the Android SDK when building Android bindings])
  fi
  ANDROID_CLASSPATH="$androidsdkdir/platforms/android-20/android.jar"
  if ! test -f "$ANDROID_CLASSPATH"; then
    ANDROID_CLASSPATH="$androidsdkdir/platforms/android-19/android.jar"
    if ! test -f "$ANDROID_CLASSPATH"; then
      AC_MSG_ERROR([Android SDK API level 19 or 20 not found])
    fi
  fi
  AC_MSG_RESULT([$ANDROID_CLASSPATH])
fi
AC_SUBST(ANDROID_CLASSPATH)

dnl Android NDK path is needed when Java bindings are enabled
AC_ARG_WITH(android-ndk,
            AS_HELP_STRING([--with-android-ndk],
                           [path to the Android NDK]),
            androidndkdir=${withval}, androidndkdir=)
if test "x$enable_owr_java" = xyes; then
  AC_MSG_CHECKING([for the Android NDK prefix])
  if test -a "x$androidndkdir" = x; then
    AC_MSG_ERROR([Need path to the Android NDK when building Android bindings])
  fi
  case "$target" in
    i?86-*|x86_64-*)
      ndk_arch=x86;;
    arm-*)
      ndk_arch=arm;;
    mips-*)
      ndk_arch=mips;;
    *)
      AC_MSG_ERROR([Unsupported Android target: $target]);;
  esac
  if ! test -d "$androidndkdir"; then
    AC_MSG_ERROR([Android NDK path $androidndkdir is invalid])
  fi
  ANDROID_NDK_PREFIX="$androidndkdir/platforms/android-9/arch-$ndk_arch/usr"
  if ! test -d "$ANDROID_NDK_PREFIX"; then
    AC_MSG_ERROR([Android NDK level 9 not found for arch $ndk_arch])
  fi
  AC_MSG_RESULT([$ANDROID_NDK_PREFIX])
fi
AC_SUBST(ANDROID_NDK_PREFIX)

AC_OUTPUT([
Makefile
bridge/Makefile
bridge/openwebrtc-bridge-0.1.pc
bridge/client/Makefile
bridge/seed/Makefile
bridge/shared/Makefile
bridge/worker/Makefile
owr/Makefile
owr/openwebrtc-0.1.pc
transport/Makefile
local/Makefile
tests/Makefile
bindings/Makefile
bindings/java/Makefile
docs/Makefile
docs/reference/Makefile
docs/reference/owr/Makefile
])
