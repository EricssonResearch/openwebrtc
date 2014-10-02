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

$srcdir/configure $* || {
 echo 'configure failed';
 exit 1;
}

echo
echo "Now type 'make' to compile this module."
echo
