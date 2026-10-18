#!/bin/bash
#
# Build the OrcaSlicer slicing core for iOS.
#
# Two stages, mirroring build_release_macos.sh: the dependency superbuild in deps/, then
# the core (libslic3r + the mobile façade) against that prefix with ORCA_MOBILE=ON. One
# slice (device or simulator) per stage; -x merges the slices into an XCFramework.
#
# Requires macOS with Xcode and its command line tools, CMake >= 3.20 and Ninja.
# Output layout:
#   deps/build/ios-<slice>/OrcaSlicer_dep/usr/local   dependency prefix
#   build/ios-<slice>/                                 core build tree
#   build/ios-xcframework/OrcaCore.xcframework         merged output of -x

set -e
set -o pipefail
SECONDS=0

SLICES="device"
BUILD_TARGET="all"
BUILD_CONFIG="Release"
IOS_MIN_VERSION="16.0"
BUILD_ONLY=""
MAKE_XCFRAMEWORK=""
RESUME=""

usage() {
    echo "Usage: ./scripts/build_ios_core.sh [options]"
    echo "   -d: Build deps only"
    echo "   -s: Build the core only (deps must exist)"
    echo "   -a: Slice: device, simulator or all (default: device)"
    echo "   -t: Minimum iOS version (default: ${IOS_MIN_VERSION})"
    echo "   -c: CMake build configuration (default: ${BUILD_CONFIG})"
    echo "   -b: Build without reconfiguring CMake"
    echo "   -j: Parallel build jobs (CMAKE_BUILD_PARALLEL_LEVEL)"
    echo "   -r: Resume deps from a restored prefix and stamps without sources: every"
    echo "       dependency with a pending step is rebuilt from scratch"
    echo "   -x: After building, merge the slices into build/ios-xcframework/OrcaCore.xcframework"
    echo "   -h: This help"
}

while getopts ":dsa:t:c:bj:rxh" opt; do
    case "${opt}" in
        d ) BUILD_TARGET="deps" ;;
        s ) BUILD_TARGET="core" ;;
        a ) SLICES="$OPTARG" ;;
        t ) IOS_MIN_VERSION="$OPTARG" ;;
        c ) BUILD_CONFIG="$OPTARG" ;;
        b ) BUILD_ONLY="1" ;;
        j ) export CMAKE_BUILD_PARALLEL_LEVEL="$OPTARG" ;;
        r ) RESUME="1" ;;
        x ) MAKE_XCFRAMEWORK="1" ;;
        h ) usage; exit 0 ;;
        * ) usage; exit 1 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "This script needs macOS with Xcode installed." >&2
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode command line tools are not configured (xcode-select -p failed)." >&2
    exit 1
fi
if ! command -v ninja >/dev/null 2>&1; then
    echo "Ninja is required (brew install ninja)." >&2
    exit 1
fi

case "$SLICES" in
    device )    SLICE_LIST="device" ;;
    simulator ) SLICE_LIST="simulator" ;;
    all )       SLICE_LIST="device simulator" ;;
    * ) echo "Unknown slice '$SLICES' (device, simulator or all)" >&2; exit 1 ;;
esac

HOST_ARCH="$(uname -m)"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$PROJECT_DIR/deps"
# Keep host package managers out of every find_package, as the macOS build does.
CMAKE_IGNORE_PREFIX_PATH="${CMAKE_IGNORE_PREFIX_PATH:-/opt/local:/usr/local:/opt/homebrew}"

# CMake 4 dropped compatibility with the pre-3.5 minimum versions some deps declare.
CMAKE_MAJOR=$(cmake --version | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/')
CMAKE_POLICY_COMPAT=""
if [ "$CMAKE_MAJOR" -ge 4 ] 2>/dev/null; then
    export CMAKE_POLICY_VERSION_MINIMUM=3.5
    CMAKE_POLICY_COMPAT="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
fi

# slice_settings <slice> sets SDK, ARCH and the per-slice directories.
slice_settings() {
    case "$1" in
        device )
            SDK="iphoneos"
            ARCH="arm64"
            ;;
        simulator )
            SDK="iphonesimulator"
            ARCH="$HOST_ARCH"
            ;;
    esac
    DEPS_BUILD_DIR="$DEPS_DIR/build/ios-$1"
    DEPS_PREFIX="$DEPS_BUILD_DIR/OrcaSlicer_dep/usr/local"
    CORE_BUILD_DIR="$PROJECT_DIR/build/ios-$1"
}

build_deps() {
    for slice in $SLICE_LIST; do
        slice_settings "$slice"
        echo "Building deps for $slice ($SDK, $ARCH)..."
        (
            set -x
            mkdir -p "$DEPS_BUILD_DIR"
            cd "$DEPS_BUILD_DIR"
            if [ "1" != "$BUILD_ONLY" ]; then
                cmake "$DEPS_DIR" \
                    -G Ninja \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_SYSTEM_NAME=iOS \
                    -DCMAKE_OSX_SYSROOT="$SDK" \
                    -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
                    -DCMAKE_SYSTEM_PROCESSOR="$ARCH" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN_VERSION" \
                    -DCMAKE_IGNORE_PREFIX_PATH="$CMAKE_IGNORE_PREFIX_PATH" \
                    $CMAKE_POLICY_COMPAT
            fi
            if [ "1" == "$RESUME" ]; then
                resume_deps
            fi
            # Keep going past a failing dependency so one run reports every failure.
            cmake --build . --target deps -- -k 0
        )
    done
}

# resume_deps: the CI cache holds the installed prefix and the ExternalProject stamps
# of every dependency, not their source or build trees. A dependency that still has a
# step to run (it failed, its recipe changed, or something it depends on was rebuilt)
# cannot resume from that; it is removed so that it downloads and builds from scratch,
# while the untouched ones stay installed. Runs in the configured deps build directory.
resume_deps() {
    local pending
    pending=$(cmake --build . --target deps -- -n 2>/dev/null | grep -oE 'dep_[A-Za-z0-9]+-prefix' | sort -u || true)
    if [ -z "$pending" ]; then
        echo "Every dependency is up to date."
        return
    fi
    for prefix in $pending; do
        echo "Rebuilding ${prefix%-prefix} from scratch"
        rm -rf "$prefix"
    done
    # The superbuild's configure step writes the per-dependency command files that the
    # build reads from the removed directories; generate them again.
    cmake .
}

build_core() {
    for slice in $SLICE_LIST; do
        slice_settings "$slice"
        if [ ! -d "$DEPS_PREFIX" ]; then
            echo "Deps prefix $DEPS_PREFIX does not exist; run without -s first." >&2
            exit 1
        fi
        echo "Building core for $slice ($SDK, $ARCH)..."
        (
            # zlib and iconv come from the SDK (see "Link the system libz on iOS"). Left to
            # itself FindZLIB / FindIconv resolve their include dir to <SDK>/usr/include and
            # CMake then passes that as -isystem, ahead of libc++'s own headers; Xcode 27's
            # libc++ refuses that ordering (<cstddef> ... didn't find libc++'s <stddef.h>).
            # The headers are on the default search path anyway, so point the variables at
            # the deps prefix, which is already an include dir, and let the SDK supply them.
            set -x
            mkdir -p "$CORE_BUILD_DIR"
            cd "$CORE_BUILD_DIR"
            if [ "1" != "$BUILD_ONLY" ]; then
                cmake "$PROJECT_DIR" \
                    -G Ninja \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_SYSTEM_NAME=iOS \
                    -DCMAKE_OSX_SYSROOT="$SDK" \
                    -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
                    -DCMAKE_SYSTEM_PROCESSOR="$ARCH" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN_VERSION" \
                    -DCMAKE_PREFIX_PATH="$DEPS_PREFIX" \
                    -DCMAKE_FIND_ROOT_PATH="$DEPS_PREFIX" \
                    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
                    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
                    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
                    -DCMAKE_IGNORE_PREFIX_PATH="$CMAKE_IGNORE_PREFIX_PATH" \
                    -DORCA_MOBILE=ON \
                    -DSLIC3R_GUI=0 \
                    -DSLIC3R_PCH=0 \
                    -DZLIB_INCLUDE_DIR="$DEPS_PREFIX/include" \
                    -DIconv_INCLUDE_DIR="$DEPS_PREFIX/include" \
                    $CMAKE_POLICY_COMPAT
            fi
            cmake --build . --target libslic3r_mobile
        )
    done
}

# merge_slice <slice> <out.a>: one static archive with every object the façade needs.
merge_slice() {
    slice_settings "$1"
    local out="$2"

    # libjpeg-turbo installs libturbojpeg.a alongside libjpeg.a with the same
    # standard-libjpeg symbols; Qhull installs both a non-reentrant and a
    # reentrant static variant, but libslic3r only reaches for the reentrant
    # one through libqhullcpp. Skip the duplicates so libtool -static does not
    # collide on the merge.
    local -a skip=( libturbojpeg.a libqhullstatic.a )

    # libqhullstatic_r.a and libqhullcpp.a both ship a default qh_fprintf
    # symbol (as a user-overridable stub); the C++ wrapper's copy is the one
    # libslic3r's callers use. Strip the C-side default so libtool does not
    # see duplicates.
    local qhull_r_slim=""
    if [ -f "$DEPS_PREFIX/lib/libqhullstatic_r.a" ]; then
        qhull_r_slim="$CORE_BUILD_DIR/libqhullstatic_r-slim.a"
        cp "$DEPS_PREFIX/lib/libqhullstatic_r.a" "$qhull_r_slim"
        xcrun ar d "$qhull_r_slim" userprintf_r.c.o userprintf_rbox_r.c.o 2>/dev/null || true
        skip+=( libqhullstatic_r.a )
    fi

    # In-tree libraries live under src/ and deps_src/; a few — semver, today — land
    # in the build tree's top-level lib/ instead.
    local libs=()
    local candidate name skipthis
    while IFS= read -r candidate; do
        name=$(basename "$candidate")
        skipthis=""
        for s in "${skip[@]}"; do [ "$name" = "$s" ] && skipthis=1 && break; done
        [ -z "$skipthis" ] && libs+=("$candidate")
    done < <(
        find "$CORE_BUILD_DIR/src" "$CORE_BUILD_DIR/deps_src" "$CORE_BUILD_DIR/lib" \
            -name '*.a' -type f 2>/dev/null
        find "$DEPS_PREFIX/lib" -maxdepth 1 -name '*.a' -type f 2>/dev/null
    )
    [ -n "$qhull_r_slim" ] && libs+=("$qhull_r_slim")
    if [ "${#libs[@]}" -eq 0 ]; then
        echo "No static libraries found for slice $1" >&2
        exit 1
    fi
    echo "Merging ${#libs[@]} archives for $1 into $out"
    xcrun libtool -static -no_warning_for_no_symbols -o "$out" "${libs[@]}"
}

make_xcframework() {
    local out_dir="$PROJECT_DIR/build/ios-xcframework"
    local headers="$out_dir/include"
    rm -rf "$out_dir"
    mkdir -p "$headers/OrcaCore"
    cp "$PROJECT_DIR/src/mobile/core/"*.hpp "$headers/OrcaCore/"

    # CocoaPods vendored_xcframeworks refuses slices whose static archives have
    # different filenames, so each slice's libOrcaCore.a lives in its own
    # directory. xcodebuild -create-xcframework accepts identical basenames.
    local args=()
    for slice in $SLICE_LIST; do
        mkdir -p "$out_dir/$slice"
        merge_slice "$slice" "$out_dir/$slice/libOrcaCore.a"
        args+=(-library "$out_dir/$slice/libOrcaCore.a" -headers "$headers")
    done
    (
        set -x
        xcodebuild -create-xcframework "${args[@]}" -output "$out_dir/OrcaCore.xcframework"
    )
}

echo "Build params:"
echo " - SLICES: $SLICE_LIST"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - IOS_MIN_VERSION: $IOS_MIN_VERSION"
echo " - CMAKE_IGNORE_PREFIX_PATH: $CMAKE_IGNORE_PREFIX_PATH"
echo

case "$BUILD_TARGET" in
    deps ) build_deps ;;
    core ) build_core ;;
    all )  build_deps; build_core ;;
esac

if [ "1" == "$MAKE_XCFRAMEWORK" ]; then
    make_xcframework
fi

echo "Done in $((SECONDS / 60))m $((SECONDS % 60))s"
