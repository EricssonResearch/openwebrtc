#!/bin/bash

BUILD_DIR="."
REPO_ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="$REPO_ROOT/scripts/engine"

export XML_CATALOG_FILES=~/.openwebrtc/etc/xml/catalog

if [[ ! -d $SCRIPT_DIR ]]; then
    git submodule init
    git submodule update
fi

#
# Check preconditions:
# 1. Check if file releases are missing and download in that case.
#
check_preconditions() {
    if [[ ! -d ~/.openwebrtc ]] ; then
        read -p "OpenWebRTC tools is missing from ~/.openwebrtc, press Enter to build and Ctrl-C to abort" dummy
        pushd scripts/bootstrap >/dev/null
        if [[ `uname` == "Darwin" ]]; then
            local_platform="osx"
        else
            local_platform="linux"
        fi
        ./bootstrap.sh -r $local_platform || die "Could not install OpenWebRTC tools"
        popd >/dev/null
    fi

    if [[ ! (-e ./openwebrtc-deps-armv7-ios || -e ./openwebrtc-deps-armv7-android \
        || -e ./openwebrtc-deps-x86_64-osx || -e ./openwebrtc-deps-x86_64-linux \
        || -e ./openwebrtc-deps-x86_64-ios-simulator ) ]]; then
        read -p "OpenWebRTC dependencies is missing, press Enter to build and Ctrl-C to abort" dummy
        pushd scripts/dependencies >/dev/null
        ./build-all.sh $@ || die "Could not build OpenWebRTC dependencies"
        ./deploy_deps.sh || die "Could not install OpenWebRTC dependencies"
        popd >/dev/null
    fi

    $REPO_ROOT/git_hooks/install_hooks.sh
}

local_sources_installed(){
    true
}

patch_sources(){
    true
}

local_clean_source(){
    true
}

build(){
    local arch=$1
    local target_triple=$2
    local target
    target=$(target_from_target_triple $target_triple) || die "Could not get target from $target_triple"
    local home=$(pwd)
    local src_dir=$(pwd)
    local debug=yes
    if [[ $opt_r == yes ]]; then
        debug=no
    fi
    if [[ ${target_triple} =~ (i386|arm)"-apple-darwin10" || $target_triple == "arm-linux-androideabi" ]]; then
        if [[ -z $(find ${REPO_ROOT}/build -name Owr-0.1.gir.h) ]]; then
            die "Missing Owr-0.1.gir.h, build for OS X or Linux first"
        fi
    fi
    (
        cd $builddir

        export CFLAGS="$PLATFORM_CFLAGS -I${src_dir}/openwebrtc-deps-${target}/include -I${src_dir}/framework"
        if [[ $target_triple == "arm-apple-darwin10" ]]; then
            local configure_flags="--enable-static --enable-owr-static --disable-shared --disable-introspection"
            local platform_ldflags="${home}/openwebrtc-deps-armv7-ios/lib/my_environ.o ${home}/openwebrtc-deps-${target}/lib/my_stat.o "
            local seed_platform_libs="-framework JavaScriptCore"
            export PLATFORM_GLIB_LIBS="-lffi -lresolv -liconv -lintl "
            export PLATFORM_GSTREAMER_LIBS="-lgstosxaudio -lgstapplemedia -framework AssetsLibrary -framework CoreMedia -framework CoreVideo -framework AVFoundation -framework Foundation -framework OpenGLES -framework CoreAudio -framework AudioToolbox -weak_framework VideoToolbox"
            export PLATFORM_OWR_GST_PLUGINS_LIBS="-lgstercolorspace"
            export PLATFORM_CXX_LIBS="-lc++"
        elif [[ $target_triple == "i386-apple-darwin10" ]]; then
            local configure_flags="--enable-static --enable-owr-static --disable-shared --disable-introspection"
            local seed_platform_libs="-framework JavaScriptCore"
            export PLATFORM_GLIB_LIBS="-lffi -lresolv -liconv -lintl "
            export PLATFORM_GSTREAMER_LIBS="-framework CoreMedia -framework CoreVideo -framework AVFoundation -framework Foundation -framework OpenGLES -lgstosxaudio -lgstapplemedia -framework AssetsLibrary -framework CoreAudio -framework AudioToolbox -weak_framework VideoToolbox"
            export PLATFORM_CXX_LIBS="-lc++"
        elif [[ $target_triple == "arm-linux-androideabi" ]]; then
            local android_sdk="$(dirname $(which adb))/.."
            local platform_ldflags="-llog -Wl,--allow-multiple-definition -landroid "
            local configure_flags="--disable-static --enable-owr-static --enable-shared --disable-introspection --enable-owr-java --with-jardir=${installdir}/jar --with-javadocdir=${installdir}/share/javadoc/openwebrtc --with-android-ndk=$ANDROID_NDK --with-android-sdk=${android_sdk}"
            local seed_platform_libs="-ljavascriptcoregtk-3.0 -licui18n -licuuc -licudata"
            export PLATFORM_GLIB_LIBS="-lffi -liconv -lintl "
            export PLATFORM_GSTREAMER_LIBS="-lgstopensles -lOpenSLES -lGLESv2 -lEGL"
            export PLATFORM_OWR_GST_PLUGINS_LIBS="-lgstandroidvideosrc -lgstercolorspace"
            export PLATFORM_CXX_LIBS="-L$ANDROID_NDK/sources/cxx-stl/gnu-libstdc++/4.8/libs/armeabi-v7a -lgnustl_static -lstdc++"
        elif [[ $target_triple == "x86_64-apple-darwin" ]]; then
            local configure_flags="--enable-static --enable-owr-static --enable-shared --enable-gtk-doc"
            local seed_platform_libs="-framework JavaScriptCore"
            export PLATFORM_GLIB_LIBS="-lffi -lresolv -liconv -lintl -framework Carbon"
            export PLATFORM_GSTREAMER_LIBS="-lgstvideoconvert -lgstapplemedia -lgstosxaudio"
            export PLATFORM_GSTREAMER_LIBS="$PLATFORM_GSTREAMER_LIBS -framework AudioUnit -framework AudioToolbox -framework CoreAudio -framework CoreMedia -framework CoreVideo -framework QTKit -framework AVFoundation -framework Foundation -framework VideoToolbox -framework AppKit -framework OpenGL"
            export PLATFORM_CXX_LIBS="-lc++"
        elif [[ $target_triple == "x86_64-unknown-linux" ]] ; then
            local configure_flags="--enable-static --enable-owr-static --enable-shared"
            local seed_platform_libs="-ljavascriptcoregtk-3.0 -licui18n -licuuc -licudata"
            export PULSE_CFLAGS="$(/usr/bin/pkg-config --cflags libpulse-mainloop-glib) $(/usr/bin/pkg-config --cflags libpulse) "
            export PULSE_LIBS="$(/usr/bin/pkg-config --libs libpulse-mainloop-glib) $(/usr/bin/pkg-config --libs libpulse) "
            export V4L2_CFLAGS="$(/usr/bin/pkg-config --cflags libv4l2) "
            export V4L2_LIBS="$(/usr/bin/pkg-config --libs libv4l2) "
            export PLATFORM_GLIB_LIBS="-lpthread -lffi -lrt -ldl -lresolv"
            export PLATFORM_GSTREAMER_LIBS="-lgstvideoconvert -lgstpulse -lgstvideo4linux2 -lX11 -lXv -lgstallocators-1.0 -lGLU -lGL"
            export PLATFORM_CXX_LIBS="-lstdc++"
        fi

        # Common exports.
        # add -DPRINT_RTCP_REPORTS to CFLAGS to see printout of RTCP statistics.

        export CPPFLAGS=$CFLAGS
        export LDFLAGS="-L${src_dir}/openwebrtc-deps-${target}/lib -L${src_dir}/openwebrtc-deps-${target}/lib/gstreamer-1.0 -L${src_dir}/openwebrtc-deps-${target}/lib $platform_ldflags"
        export GLIB_CFLAGS="-isystem ${src_dir}/openwebrtc-deps-${target}/include/glib-2.0"
        export GLIB_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lgio-2.0 -lgmodule-2.0 -lgthread-2.0 -lgobject-2.0 -lglib-2.0 -lz $PLATFORM_GLIB_LIBS"
        export NICE_CFLAGS="-isystem ${src_dir}/openwebrtc-deps-${target}/include/nice"
        export NICE_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lnice"
        export ORC_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/orc-0.4"
        export ORC_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lorc-test-0.4 -lorc-0.4"
        export XML_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/libxml2"
        export XML_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lxml2 -lm"
        export GSTREAMER_CFLAGS="-isystem ${src_dir}/openwebrtc-deps-${target}/include/gstreamer-1.0 -isystem ${src_dir}/openwebrtc-deps-${target}/include/private-gstreamer -isystem ${src_dir}/openwebrtc-deps-${target}/lib/gstreamer-1.0/include"
        export GSTREAMER_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -L${src_dir}/openwebrtc-deps-${target}/lib/gstreamer-1.0 -lgstaudiotestsrc -lgstvideotestsrc -lgstcoreelements -lgstalaw -lgstmulaw -lgstopus -lopus -lgstapp -lgstaudioconvert -lgstaudioresample -lgstvolume -lgstvideoconvert -lgstvpx -lvpx -lgstopengl -lgstinter -lgstgl-1.0"
        export GSTREAMER_LIBS="$GSTREAMER_LIBS $PLATFORM_GSTREAMER_LIBS -lgstrtpmanager -lgstrtp -lgstsrtp -lsrtp -lgstvideocrop -lgstvideofilter -lgstvideoparsersbad -lgstvideorate -lgstvideoscale -lgstnice -lgstcontroller-1.0 -lgstpbutils-1.0 -lgstnet-1.0 -lgstrtp-1.0 -lgstbadvideo-1.0 -lgstbadbase-1.0"
        export GSTREAMER_LIBS="$GSTREAMER_LIBS -lgsttag-1.0 -lgstapp-1.0 -lgstaudio-1.0 -lgstvideo-1.0 -lgstcodecparsers-1.0 -lgstbase-1.0 -lgstreamer-1.0 -lm"
        export GSTREAMER_SCTP_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/gstreamer-1.0"
        export GSTREAMER_SCTP_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lgstsctp-1.0"
        export SEED_CFLAGS="-isystem ${src_dir}/openwebrtc-deps-${target}/include/seed-gtk3 -isystem ${src_dir}/openwebrtc-deps-${target}/include/gobject-introspection-1.0"
        export SEED_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib/seed-gtk3 -lseed-gtk3 -lseed_sandbox $seed_platform_libs -lgirepository-1.0 -lgirepository-internals $PLATFORM_CXX_LIBS"
        export JSON_GLIB_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/json-glib-1.0"
        export JSON_GLIB_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -ljson-glib-1.0"
        export LIBSOUP_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/libsoup-2.4"
        export LIBSOUP_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lsoup-2.4"
        export ELIB_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/elib"
        export ELIB_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lelib"
        export ESDP_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/esdp"
        export ESDP_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib -lesdp"
        export OPENWEBRTC_GST_PLUGINS_CFLAGS="-I${src_dir}/openwebrtc-deps-${target}/include/plugins -I${src_dir}/openwebrtc-deps-${target}/include"
        export OPENWEBRTC_GST_PLUGINS_LIBS="-L${src_dir}/openwebrtc-deps-${target}/lib/gstreamer-1.0 -lgsterdtls -lssl -lcrypto -lgstvideorepair -lgstopenh264 -lopenh264 -lgstsctp -lusrsctp $PLATFORM_OWR_GST_PLUGINS_LIBS $PLATFORM_CXX_LIBS"
        $REPO_ROOT/autogen.sh \
            --prefix=${installdir} \
            --host=${target_triple} \
            --enable-debug=$debug ${configure_flags} &&
        patch -p0 < $REPO_ROOT/libtool.diff &&
        make && make install
    )
}

. $SCRIPT_DIR/engine.sh
