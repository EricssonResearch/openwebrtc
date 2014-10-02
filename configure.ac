dnl Process this file with autoconf to produce a configure script.

AC_INIT(openwebrtc, 0.1)

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR(m4)

AM_INIT_AUTOMAKE([1.11 -Wno-portability])

AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CC_STDC

LT_INIT

LIBOPENWEBRTC_CFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_32 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_32 -DGLIB_DISABLE_DEPRECATION_WARNINGS"
AC_SUBST(LIBOPENWEBRTC_CFLAGS)

PKG_CHECK_MODULES(GLIB, [glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0])
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-1.0 >= 1.0.10 gstreamer-rtp-1.0 >= 1.0.10 gstreamer-video-1.0 >= 1.0.10 gstreamer-app-1.0 >= 1.0.10])
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
[  --enable-debug    Enable debug],
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
docs/Makefile
docs/reference/Makefile
docs/reference/owr/Makefile
])
