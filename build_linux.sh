#!/usr/bin/env bash

SCRIPT_NAME=$(basename "$0")
SCRIPT_PATH=$(dirname $(readlink -f ${0}))

pushd ${SCRIPT_PATH} > /dev/null

set -e # Exit immediately if a command exits with a non-zero status.

function check_available_memory_and_disk() {
    FREE_MEM_GB=$(free --gibi --total | grep 'Mem' | rev | cut --delimiter=" " --fields=1 | rev)
    MIN_MEM_GB=10

    FREE_DISK_KB=$(df --block-size=1K . | tail -1 | awk '{print $4}')
    MIN_DISK_KB=$((10 * 1024 * 1024))

    if [[ ${FREE_MEM_GB} -le ${MIN_MEM_GB} ]] ; then
        echo -e "\nERROR: Orca Slicer Builder requires at least ${MIN_MEM_GB}G of 'available' mem (systen has only ${FREE_MEM_GB}G available)"
        echo && free --human && echo
        exit 2
    fi

    if [[ ${FREE_DISK_KB} -le ${MIN_DISK_KB} ]] ; then
        echo -e "\nERROR: Orca Slicer Builder requires at least $(echo ${MIN_DISK_KB} |awk '{ printf "%.1fG\n", $1/1024/1024; }') (systen has only $(echo ${FREE_DISK_KB} | awk '{ printf "%.1fG\n", $1/1024/1024; }') disk free)"
        echo && df --human-readable . && echo
        exit 1
    fi
}

function usage() {
    echo "Usage: ./${SCRIPT_NAME} [-1][-b][-c][-d][-h][-i][-r][-s][-u]"
    echo "   -1: limit builds to one core (where possible)"
    echo "   -b: build in debug mode"
    echo "   -c: force a clean build"
    echo "   -d: download and build dependencies in ./deps/ (build prerequisite)"
    echo "   -h: prints this help text"
    echo "   -i: build the Orca Slicer AppImage (optional)"
    echo "   -r: skip RAM and disk checks (low RAM compiling)"
    echo "   -s: build the Orca Slicer (optional)"
    echo "   -u: install system dependencies (asks for sudo password; build prerequisite)"
    echo "For a first use, you want to './${SCRIPT_NAME} -u'"
    echo "   and then './${SCRIPT_NAME} -dsi'"
}

unset name
while getopts ":1bcdghirsu" opt ; do
  case ${opt} in
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    b )
        BUILD_DEBUG="1"
        ;;
    c )
        CLEAN_BUILD=1
        ;;
    d )
        BUILD_DEPS="1"
        ;;
    h ) usage
        exit 0
        ;;
    i )
        BUILD_IMAGE="1"
        ;;
    r )
	    SKIP_RAM_CHECK="1"
	;;
    s )
        BUILD_ORCA="1"
        ;;
    u )
        UPDATE_LIB="1"
        ;;
  esac
done

if [ ${OPTIND} -eq 1 ] ; then
    usage
    exit 0
fi

DISTRIBUTION=$(awk -F= '/^ID=/ {print $2}' /etc/os-release)
# treat ubuntu as debian
if [ "${DISTRIBUTION}" == "ubuntu" ] || [ "${DISTRIBUTION}" == "linuxmint" ] ; then
    DISTRIBUTION="debian"
fi

if [ ! -f ./linux.d/${DISTRIBUTION} ] ; then
    echo "Your distribution \"${DISTRIBUTION}\" is not supported by system-dependency scripts in ./linux.d/"
    echo "Please resolve dependencies manually and contribute a script for your distribution to upstream."
    exit 1
else
    echo "resolving sysetem dependeinces for distribution \"${DISTRIBUTION}\" ..."
    source ./linux.d/${DISTRIBUTION}
fi

echo "FOUND_GTK3=${FOUND_GTK3}"
if [[ -z "${FOUND_GTK3_DEV}" ]] ; then
    echo "Error, you must install the dependencies before."
    echo "Use option -u with sudo"
    exit 1
fi

echo "Changing date in version..."
{
    # change date in version
    sed --in-place "s/+UNKNOWN/_$(date '+%F')/" version.inc
}
echo "done"


if ! [[ -n "${SKIP_RAM_CHECK}" ]] ; then
    check_available_memory_and_disk
fi

if [[ -n "${BUILD_DEPS}" ]] ; then
    echo "Configuring dependencies..."
    BUILD_ARGS="-DDEP_WX_GTK3=ON"
    if [[ -n "${CLEAN_BUILD}" ]]
    then
        rm -fr deps/build
    fi
    if [ ! -d "deps/build" ]
    then
        mkdir deps/build
    fi
    if [[ -n "${BUILD_DEBUG}" ]] ; then
        # build deps with debug and release else cmake will not find required sources
        if [ ! -d "deps/build/release" ] ; then
            mkdir deps/build/release
        fi
        cmake -S deps -B deps/build/release -G Ninja -DDESTDIR="${SCRIPT_PATH}/deps/build/destdir" -DDEP_DOWNLOAD_DIR="${SCRIPT_PATH}/deps/DL_CACHE" ${BUILD_ARGS}
        cmake --build deps/build/release
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    fi

    echo "cmake -S deps -B deps/build -G Ninja ${BUILD_ARGS}"
    cmake -S deps -B deps/build -G Ninja ${BUILD_ARGS}
    cmake --build deps/build
fi

if [[ -n "${BUILD_ORCA}" ]] ; then
    echo "Configuring OrcaSlicer..."
    if [[ -n "${CLEAN_BUILD}" ]] ; then
        rm --force --recursive build
    fi
    BUILD_ARGS=""
    if [[ -n "${FOUND_GTK3_DEV}" ]] ; then
        BUILD_ARGS="-DSLIC3R_GTK=3"
    fi
    if [[ -n "${BUILD_DEBUG}" ]] ; then
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug -DBBL_INTERNAL_TESTING=1"
    else
        BUILD_ARGS="${BUILD_ARGS} -DBBL_RELEASE_TO_PUBLIC=1 -DBBL_INTERNAL_TESTING=0"
    fi
    echo -e "cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="${SCRIPT_PATH}/deps/build/destdir/usr/local" -DSLIC3R_STATIC=1 ${BUILD_ARGS}"
    cmake -S . -B build -G Ninja \
        -DCMAKE_PREFIX_PATH="${SCRIPT_PATH}/deps/build/destdir/usr/local" \
        -DSLIC3R_STATIC=1 \
        -DORCA_TOOLS=ON \
        ${BUILD_ARGS}
    echo "done"
    echo "Building OrcaSlicer ..."
    cmake --build build --target OrcaSlicer
    echo "Building OrcaSlicer_profile_validator .."
    cmake --build build --target OrcaSlicer_profile_validator
    ./run_gettext.sh
    echo "done"
fi

if [[ -n "${BUILD_IMAGE}" || -n "${BUILD_ORCA}" ]] ; then
    pushd build > /dev/null
    echo "[9/9] Generating Linux app..."
    build_linux_image="./src/build_linux_image.sh"
    if [[ -e ${build_linux_image} ]] ; then
        extra_script_args=""
        if [[ -n "${BUILD_IMAGE}" ]] ; then
            extra_script_args="-i"
        fi
        ${build_linux_image} ${extra_script_args}

        echo "done"
    fi
    popd > /dev/null # build
fi

popd > /dev/null # ${SCRIPT_PATH}
