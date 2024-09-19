#!/bin/bash

export ROOT=$(dirname $(readlink -f ${0}))

set -e # exit on first error

function check_available_memory_and_disk() {
    FREE_MEM_GB=$(free -g -t | grep 'Mem' | rev | cut -d" " -f1 | rev)
    MIN_MEM_GB=10

    FREE_DISK_KB=$(df -k . | tail -1 | awk '{print $4}')
    MIN_DISK_KB=$((10 * 1024 * 1024))

    if [ ${FREE_MEM_GB} -le ${MIN_MEM_GB} ]; then
        echo -e "\nERROR: Orca Slicer Builder requires at least ${MIN_MEM_GB}G of 'available' mem (systen has only ${FREE_MEM_GB}G available)"
        echo && free -h && echo
        exit 2
    fi

    if [[ ${FREE_DISK_KB} -le ${MIN_DISK_KB} ]]; then
        echo -e "\nERROR: Orca Slicer Builder requires at least $(echo ${MIN_DISK_KB} |awk '{ printf "%.1fG\n", $1/1024/1024; }') (systen has only $(echo ${FREE_DISK_KB} | awk '{ printf "%.1fG\n", $1/1024/1024; }') disk free)"
        echo && df -h . && echo
        exit 1
    fi
}

function usage() {
    echo "Usage: ./BuildLinux.sh [-1][-b][-c][-d][-i][-r][-s][-u]"
    echo "   -1: limit builds to 1 core (where possible)"
    echo "   -b: build in Debug mode"
    echo "   -B: build in RelWithDebInfo mode"
    echo "   -c: force a clean build"
    echo "   -d: build deps (optional)"
    echo "   -h: this help output"
    echo "   -i: Generate appimage (optional)"
    echo "   -p: Pack dependencies (optional)"
    echo "   -r: skip ram and disk checks (low ram compiling)"
    echo "   -s: build orca-slicer (optional)"
    echo "   -u: update and build dependencies (optional and need sudo)"
    echo "For a first use, you want to 'sudo ./BuildLinux.sh -u'"
    echo "   and then './BuildLinux.sh -dsi'"
}

BUILD_DIR="build"
BUILD_TYPE="Release"
unset name
while getopts ":1bBcdhiprsu" opt; do
  case ${opt} in
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    b )
        BUILD_DIR="build-debug"
        BUILD_TYPE="Debug"
        ;;
    B )
        BUILD_DIR="build-debinfo"
        BUILD_TYPE="RelWithDebInfo"
        ;;
    c )
        CLEAN_BUILD="1"
        ;;
    d )
        BUILD_DEPS="1"
        ;;
    h )
        usage
        exit 0
        ;;
    i )
        BUILD_IMAGE="1"
        ;;
    p )
        PACK_DEPS="1"
        ;;
    r )
	    SKIP_RAM_CHECK="1"
	    ;;
    s )
        BUILD_ORCA="1"
        ;;
    u )
        export UPDATE_LIB="1"
        ;;
    * )
        ;;
  esac
done

if [ ${OPTIND} -eq 1 ]
then
    usage
    exit 0
fi

DISTRIBUTION=$(awk -F= '/^ID=/ {print $2}' /etc/os-release)
# treat ubuntu as debian
if [ "${DISTRIBUTION}" == "ubuntu" ] || [ "${DISTRIBUTION}" == "linuxmint" ]
then
    DISTRIBUTION="debian"
fi
if [ ! -f ./linux.d/${DISTRIBUTION} ]
then
    echo "Your distribution does not appear to be currently supported by these build scripts"
    exit 1
fi
source ./linux.d/${DISTRIBUTION}

echo "FOUND_GTK3=${FOUND_GTK3}"
if [[ -z "${FOUND_GTK3_DEV}" ]]
then
    echo "Error, you must install the dependencies before."
    echo "Use option -u with sudo"
    exit 1
fi

echo "Changing date in version..."
{
    # change date in version
    sed -i "s/+UNKNOWN/_$(date '+%F')/" version.inc
}


if ! [[ -n "${SKIP_RAM_CHECK}" ]]
then
    check_available_memory_and_disk
fi

if [[ -n "${BUILD_DEPS}" ]]
then
    echo "Configuring dependencies..."
    if [[ -n "${CLEAN_BUILD}" ]]
    then
        rm -fr deps/${BUILD_DIR}
    fi

    echo "cmake -S deps -B deps/${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    cmake -S deps -B deps/${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
    cmake --build deps/${BUILD_DIR}
fi


if [[ -n "${BUILD_ORCA}" ]]
then
    echo "Configuring OrcaSlicer..."
    if [[ -n "${CLEAN_BUILD}" ]]
    then
        rm -fr ${BUILD_DIR}
    fi
    echo -e "cmake -S . -B ${BUILD_DIR} -G Ninja -DORCA_TOOLS=ON -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    cmake -S . -B ${BUILD_DIR} -G Ninja -DORCA_TOOLS=ON -DCMAKE_BUILD_TYPE=${BUILD_TYPE}

    echo "Building OrcaSlicer ..."
    cmake --build ${BUILD_DIR} --target OrcaSlicer

    echo "Building OrcaSlicer_profile_validator .."
    cmake --build ${BUILD_DIR} --target OrcaSlicer_profile_validator

    ./run_gettext.sh
fi

if [[ -n "${PACK_DEPS}" ]]
then
    echo "Packing dependencies..."
    if [ -d "deps/${BUILD_DIR}/OrcaSlicer_dep" ]; then
        cd deps/${BUILD_DIR}
        tar -czvf OrcaSlicer_dep_ubuntu_$(date +"%Y%m%d").tar.gz OrcaSlicer_dep
    else
        echo "The deps destination directory does not exist"
        echo "Targeted destination directory: $(pwd)/deps/${BUILD_DIR}/OrcaSlicer_dep"
    fi
fi

if [[ -e ${ROOT}/${BUILD_DIR}/src/BuildLinuxImage.sh ]]; then
# Give proper permissions to script
chmod 755 ${ROOT}/${BUILD_DIR}/src/BuildLinuxImage.sh

echo "Generating Linux app..."
    pushd ${BUILD_DIR}
        if [[ -n "${BUILD_IMAGE}" ]]
        then
            ${ROOT}/${BUILD_DIR}/src/BuildLinuxImage.sh -i
        else
            ${ROOT}/${BUILD_DIR}/src/BuildLinuxImage.sh
        fi
    popd
fi
