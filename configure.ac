dnl Process this file with autoconf to produce a configure script.

AC_INIT(openwebrtc, 0.1)

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR(m4)

AM_INIT_AUTOMAKE([1.11 -Wno-portability foreign])

AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CC_STDC
dnl only needed for Apple, but can't be conditional
AC_PROG_OBJC

LT_INIT

LIBOPENWEBRTC_CFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_32 -DGLIB_DISABLE_DEPRECATION_WARNINGS"
AC_SUBST(LIBOPENWEBRTC_CFLAGS)

GST_REQUIRED=1.4
PKG_CHECK_MODULES(GLIB, [glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0])
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-1.0 >= $GST_REQUIRED gstreamer-rtp-1.0 >= $GST_REQUIRED gstreamer-video-1.0 >= $GST_REQUIRED gstreamer-app-1.0 >= $GST_REQUIRED])
PKG_CHECK_MODULES(NICE, [nice >= 0.1.7.1])
PKG_CHECK_MODULES(ORC, [orc-0.4])
PKG_CHECK_MODULES(SEED, [seed])

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
  AC_DEFINE(OWR_JAVA, 1,
  [Define if generating bindings for java])
fi
AM_CONDITIONAL(OWR_JAVA, test x$enable_owr_java = xyes)

AC_OUTPUT([
Makefile
bridge/Makefile
bridge/client/Makefile
bridge/seed/Makefile
bridge/shared/Makefile
bridge/worker/Makefile
owr/Makefile
owr/openwebrtc-0.1.pc
transport/Makefile
local/Makefile
tests/Makefile
gen_jni/Makefile
docs/Makefile
docs/reference/Makefile
docs/reference/owr/Makefile
])
