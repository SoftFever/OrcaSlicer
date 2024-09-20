#!/bin/bash

set -e
set -o pipefail

while getopts ":dpa:snt:xbc:h1" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64)"
        echo "   -s: Build slicer only"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 11.3"
        echo "   -x: Use Ninja CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -1: Use single job for building"
        exit 0
        ;;
    * )
        ;;
  esac
done

# Set defaults

if [ -z "$ARCH" ]; then
  ARCH="$(uname -m)"
  export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="11.3"
fi

case ${BUILD_CONFIG} in
Release )
  export BUILD_DIR_NAME="build"
  ;;
RelWithDebInfo )
  export BUILD_DIR_NAME="build-dbginfo"
  ;;
Debug )
  export BUILD_DIR_NAME="build-debug"
  ;;
* )
  echo Invalid build config \""${BUILD_CONFIG}"\". Valid options are Release, RelWithDebInfo, and Debug
  exit 1
  ;;
esac
BUILD_DIR_NAME=$BUILD_DIR_NAME"_"$ARCH

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo

# if which -s brew; then
# 	brew --prefix libiconv
# 	brew --prefix zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/
# elif which -s port; then
# 	port install libiconv
# 	port install zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:/opt/local/lib
# else
# 	echo "Need either brew or macports to successfully build deps"
# 	exit 1
# fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESTDIR="deps/$BUILD_DIR_NAME/OrcaSlicer_dep_$ARCH"

# Fix for Multi-config generators
if [ "$SLICER_CMAKE_GENERATOR" == "Xcode" ]; then
    export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"
else
    export BUILD_DIR_CONFIG_SUBDIR=""
fi

function build_deps() {
    echo "Building deps..."
    (
        set -x
        if [ -z "$BUILD_ONLY" ]; then
            cmake -S deps -B "deps/$BUILD_DIR_NAME" \
                -G "${DEPS_CMAKE_GENERATOR}" \
                -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                -DCMAKE_OSX_ARCHITECTURES:STRING="${ARCH}" \
                -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
        fi
        cmake --build "deps/$BUILD_DIR_NAME" --config "$BUILD_CONFIG" --target deps
    )
}

function pack_deps() {
    echo "Packing deps..."
    (
        if [ -d "$DESTDIR" ]; then
            set -x
            cd "$DESTDIR/.."
            tar -zcvf "OrcaSlicer_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "OrcaSlicer_dep_$ARCH"
        else
            echo "The deps destination directory does not exist"
            echo "Targeted destination directory: $(pwd)/$DESTDIR"
        fi
    )
}

function build_slicer() {
    echo "Building slicer..."
    (
        set -x
        if [ -z "$BUILD_ONLY" ]; then
            cmake . -B "${BUILD_DIR_NAME}" \
                -G "${SLICER_CMAKE_GENERATOR}" \
                -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                -DCMAKE_MACOSX_RPATH=ON \
                -DCMAKE_MACOSX_BUNDLE=ON \
                -DCMAKE_OSX_ARCHITECTURES="${ARCH}" \
                -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
        fi
        cmake --build "${BUILD_DIR_NAME}" --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
    )

    echo "Verify localization with gettext..."
    (
        cd "$PROJECT_DIR"
        ./run_gettext.sh
    )

    echo "Fix macOS app package..."
    (
        cd "$BUILD_DIR_NAME"
        mkdir -p OrcaSlicer
        cd OrcaSlicer
        # remove previously built app
        rm -rf ./OrcaSlicer.app
        # fully copy newly built app
        cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer.app" ./OrcaSlicer.app
        # fix resources
        resources_path=$(readlink ./OrcaSlicer.app/Contents/Resources)
        rm ./OrcaSlicer.app/Contents/Resources
        cp -R "$resources_path" ./OrcaSlicer.app/Contents/Resources
        # delete .DS_Store file
        find ./OrcaSlicer.app/ -name '.DS_Store' -delete
    )

    # extract version
    # export ver=$(grep '^#define SoftFever_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
    # ver="_V${ver//\"}"
    # echo $PWD
    # if [ "1." != "$NIGHTLY_BUILD". ];
    # then
    #     ver=${ver}_dev
    # fi

    # zip -FSr OrcaSlicer${ver}_Mac_${ARCH}.zip OrcaSlicer.app
}

cd "$PROJECT_DIR"

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, all"
        exit 1
        ;;
esac

if [ -n "$PACK_DEPS" ]; then
    pack_deps
fi
