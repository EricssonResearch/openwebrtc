#!/bin/bash
# you can either set the environment variables AUTOCONF, AUTOHEADER, AUTOMAKE,
# ACLOCAL, AUTOPOINT and/or LIBTOOLIZE to the right versions, or leave them
# unset and get the defaults

srcdir=`dirname $0`
(test -d $srcdir/m4) || mkdir $srcdir/m4

for ag_option in $@
do
case $ag_option in
  --noconfigure)
      NOCONFIGURE=defined
  AUTOGEN_EXT_OPT="$AUTOGEN_EXT_OPT --noconfigure"
      echo "+ configure run disabled"
      ;;
  --disable-gtk-doc)
      enable_gtk_doc=no
      echo "+ gtk-doc disabled"
      ;;
esac
done

pushd $srcdir > /dev/null

if test x$enable_gtk_doc = xno; then
    if test -f gtk-doc.make; then :; else
       echo "EXTRA_DIST = missing-gtk-doc" > gtk-doc.make
    fi
    echo "WARNING: You have disabled gtk-doc."
    echo "         As a result, you will not be able to generate the API"
    echo "         documentation and 'make dist' will not work."
    echo
else
    gtkdocize || exit $?
fi

autoreconf --verbose --force --install --make || {
 echo 'autogen.sh failed';
 exit 1;
}

popd > /dev/null

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
