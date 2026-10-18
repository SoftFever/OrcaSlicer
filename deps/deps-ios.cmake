
# iOS (device and simulator) settings for the dependency superbuild.
#
# Included from deps/CMakeLists.txt when CMAKE_SYSTEM_NAME is "iOS". The caller passes
#   -DCMAKE_SYSTEM_NAME=iOS
#   -DCMAKE_OSX_SYSROOT=iphoneos | iphonesimulator
#   -DCMAKE_OSX_ARCHITECTURES=<single arch>
#   -DCMAKE_OSX_DEPLOYMENT_TARGET=<min iOS version>
# scripts/build_ios_core.sh is the intended driver and builds one slice per invocation.
#
# Every dependency is built static. DEPS_MOBILE is forced on: the dependency list (see
# deps/CMakeLists.txt) leaves out the GUI, network-host and plugin libraries the slicing
# core does not link.

if (NOT CMAKE_OSX_SYSROOT)
    message(FATAL_ERROR "iOS deps: set CMAKE_OSX_SYSROOT to iphoneos or iphonesimulator")
endif ()

list(LENGTH CMAKE_OSX_ARCHITECTURES _ios_arch_len)
if (NOT _ios_arch_len EQUAL 1)
    message(FATAL_ERROR "iOS deps: CMAKE_OSX_ARCHITECTURES must name exactly one architecture (got '${CMAKE_OSX_ARCHITECTURES}')")
endif ()

set(DEPS_IOS TRUE)
set(DEPS_IOS_ARCH "${CMAKE_OSX_ARCHITECTURES}")
# Only the core's dependencies exist for iOS.
set(DEPS_MOBILE ON)

# project() resolves a sysroot name such as "iphoneos" to the full SDK path. Resolve it
# here as well so the autotools recipes (GMP, MPFR) and OpenSSL get an -isysroot flag.
if (IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
    set(DEPS_IOS_SDK_PATH "${CMAKE_OSX_SYSROOT}")
else ()
    execute_process(
        COMMAND xcrun --sdk ${CMAKE_OSX_SYSROOT} --show-sdk-path
        OUTPUT_VARIABLE DEPS_IOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _xcrun_result
    )
    if (NOT _xcrun_result EQUAL 0 OR NOT IS_DIRECTORY "${DEPS_IOS_SDK_PATH}")
        message(FATAL_ERROR "iOS deps: could not resolve SDK '${CMAKE_OSX_SYSROOT}' with xcrun")
    endif ()
endif ()

if (DEPS_IOS_SDK_PATH MATCHES "iPhoneSimulator")
    set(DEPS_IOS_SIMULATOR TRUE)
    set(DEPS_IOS_MIN_VERSION_FLAG "-mios-simulator-version-min=${DEP_OSX_TARGET}")
    # OpenSSL 1.1.1 configure target; it picks the simulator SDK through xcrun itself.
    set(DEPS_IOS_OPENSSL_TARGET "iossimulator-xcrun")
else ()
    set(DEPS_IOS_SIMULATOR FALSE)
    set(DEPS_IOS_MIN_VERSION_FLAG "-miphoneos-version-min=${DEP_OSX_TARGET}")
    set(DEPS_IOS_OPENSSL_TARGET "ios64-xcrun")
endif ()

# Flags for the recipes that do not go through CMake (GMP, MPFR, OpenSSL).
set(DEPS_IOS_TARGET_FLAGS "-arch ${DEPS_IOS_ARCH} -isysroot ${DEPS_IOS_SDK_PATH} ${DEPS_IOS_MIN_VERSION_FLAG}")

# autotools --host triple. GMP.cmake and MPFR.cmake read TOOLCHAIN_PREFIX whenever
# CMAKE_CROSSCOMPILING is set, which CMake does automatically for CMAKE_SYSTEM_NAME=iOS.
if (DEPS_IOS_ARCH STREQUAL "arm64")
    set(TOOLCHAIN_PREFIX "aarch64-apple-darwin")
else ()
    set(TOOLCHAIN_PREFIX "${DEPS_IOS_ARCH}-apple-darwin")
endif ()

# Same availability guard as the macOS build: a dependency must not use SDK features
# newer than the deployment target.
set(DEP_WERRORS_SDK "-Werror=partial-availability -Werror=unguarded-availability -Werror=unguarded-availability-new")

# CMAKE_SYSTEM_NAME is not forwarded by ExternalProject, so each CMake-based dependency
# gets the full iOS description here. CMake's built-in iOS support sets the
# CMAKE_FIND_ROOT_PATH_MODE_* variables to ONLY, which would hide the deps prefix from
# find_package; BOTH keeps the prefix visible while CMAKE_IGNORE_PREFIX_PATH still keeps
# host package managers out.
set(DEP_CMAKE_OPTS
    "-DCMAKE_SYSTEM_NAME=iOS"
    "-DCMAKE_SYSTEM_PROCESSOR=${DEPS_IOS_ARCH}"
    "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
    "-DCMAKE_OSX_ARCHITECTURES=${DEPS_IOS_ARCH}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEP_OSX_TARGET}"
    "-DCMAKE_FIND_ROOT_PATH=${DESTDIR}"
    "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH"
    "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH"
    "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DCMAKE_MACOSX_BUNDLE=OFF"
    "-DCMAKE_CXX_FLAGS=${DEP_WERRORS_SDK}"
    "-DCMAKE_C_FLAGS=${DEP_WERRORS_SDK}"
    "-DCMAKE_FIND_FRAMEWORK=LAST"
    "-DCMAKE_FIND_APPBUNDLE=LAST"
)

message(STATUS "iOS deps: sdk=${DEPS_IOS_SDK_PATH} arch=${DEPS_IOS_ARCH} min=${DEP_OSX_TARGET} simulator=${DEPS_IOS_SIMULATOR}")

include("deps-unix-common.cmake")
