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
    echo "Usage: ./BuildLinux.sh [-1][-b][-c][-f][-d][-i][-r][-s][-u]"
    echo "   -1: limit builds to 1 core (where possible)"
    echo "   -b: build in debug mode"
    echo "   -c: force a clean build"
    echo "   -f: force a deps build (even if they are already built)"
    echo "   -d: build deps (if needed)"
    echo "   -h: this help output"
    echo "   -i: Generate appimage (optional)"
    echo "   -r: skip ram and disk checks (low ram compiling)"
    echo "   -s: build orca-slicer (optional)"
    echo "   -u: update and build dependencies (optional and need sudo)"
    echo "For a first use, you want to 'sudo ./BuildLinux.sh -u'"
    echo "   and then './BuildLinux.sh -dsi'"
}

PRESET="linux-release"
BUILD_DEPS="0"
FORCE_BUILD_DEPS="0"
while getopts ":1bcfdghirsu" opt; do
  case ${opt} in
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    b )
        BUILD_DEBUG="1"
        PRESET="linux-debug"
        ;;
    c )
        CLEAN_BUILD=1
        ;;
    f )
        FORCE_BUILD_DEPS="1"
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
echo "done"


if ! [[ -n "${SKIP_RAM_CHECK}" ]]
then
    check_available_memory_and_disk
fi

if [[ -n "${CLEAN_BUILD}" ]]
then
  CONFIG_ARGS="--fresh"
fi

cmake --preset ${PRESET} -DBUILD_DEPS=${BUILD_DEPS} -DCLEAN_DEPS=${CLEAN_BUILD} -DFORCE_DEPS=${FORCE_BUILD_DEPS} ${CONFIG_ARGS}

# get the build directory's name from the cmake cache
BUILD_DIR_NAME=$(cmake --preset ${PRESET} -LA -N | grep BIN_DIR_NAME | cut -d "=" -f2)

if [[ -n "${BUILD_ORCA}" ]]
then
    if [[ -n "${CLEAN_BUILD}" ]]
    then
      echo "Cleaning OrcaSlicer ..."
      cmake --build --preset ${PRESET} --target clean
    fi
    echo "Building OrcaSlicer ..."
    cmake --build --preset ${PRESET}
    echo "Building OrcaSlicer_profile_validator .."
    cmake --build ${BUILD_DIR_NAME} --target OrcaSlicer_profile_validator
    ./run_gettext.sh
fi

if [[ -e ${ROOT}/${BUILD_DIR_NAME}/src/BuildLinuxImage.sh ]]; then
# Give proper permissions to script
chmod 755 ${ROOT}/${BUILD_DIR_NAME}/src/BuildLinuxImage.sh

    pushd ${BUILD_DIR_NAME} # cmake exported env variable
        if [[ -n "${BUILD_IMAGE}" ]]
        then
            ${ROOT}/${BUILD_DIR_NAME}/src/BuildLinuxImage.sh -i
        else
            ${ROOT}/${BUILD_DIR_NAME}/src/BuildLinuxImage.sh
        fi
    popd
fi
