#!/bin/bash
# you can either set the environment variables AUTOCONF, AUTOHEADER, AUTOMAKE,
# ACLOCAL, AUTOPOINT and/or LIBTOOLIZE to the right versions, or leave them
# unset and get the defaults

srcdir=`dirname $0`
(test -d $srcdir/m4) || mkdir $srcdir/m4

pushd $srcdir > /dev/null
gtkdocize && \
autoreconf --verbose --force --install --make || {
 echo 'autogen.sh failed';
 exit 1;
}

popd > /dev/null

while test "x$@" != "x" ; do
optarg=`expr "x$@" : 'x[^=]*=\(.*\)'`
case "$@" in
  --noconfigure)
      NOCONFIGURE=defined
  AUTOGEN_EXT_OPT="$AUTOGEN_EXT_OPT --noconfigure"
      echo "+ configure run disabled"
      shift
      ;;
esac
done

for arg do CONFIGURE_EXT_OPT="$CONFIGURE_EXT_OPT $arg"; done
if test ! -z "$CONFIGURE_EXT_OPT"
then
echo "+ options passed to configure: $CONFIGURE_EXT_OPT"
fi

test -n "$NOCONFIGURE" && {
 echo 'Skipping configure stage';
 exit 0
}

$srcdir/configure $* || {
 echo 'configure failed';
 exit 1;
}

echo
echo "Now type 'make' to compile this module."
echo
