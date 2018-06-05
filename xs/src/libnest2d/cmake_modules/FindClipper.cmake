# Find Clipper library (http://www.angusj.com/delphi/clipper.php).
# The following variables are set
#
# CLIPPER_FOUND
# CLIPPER_INCLUDE_DIRS
# CLIPPER_LIBRARIES
#
# It searches the environment variable $CLIPPER_PATH automatically.

FIND_PATH(CLIPPER_INCLUDE_DIRS clipper.hpp
    $ENV{CLIPPER_PATH}
    $ENV{CLIPPER_PATH}/cpp/
    $ENV{CLIPPER_PATH}/include/
    $ENV{CLIPPER_PATH}/include/polyclipping/
    ${PROJECT_SOURCE_DIR}/python/pymesh/third_party/include/
    ${PROJECT_SOURCE_DIR}/python/pymesh/third_party/include/polyclipping/
    ${CMAKE_PREFIX_PATH}/include/polyclipping
    ${CMAKE_PREFIX_PATH}/include/
    /opt/local/include/
    /opt/local/include/polyclipping/
    /usr/local/include/
    /usr/local/include/polyclipping/
    /usr/include
    /usr/include/polyclipping/)

FIND_LIBRARY(CLIPPER_LIBRARIES polyclipping
    $ENV{CLIPPER_PATH}
    $ENV{CLIPPER_PATH}/cpp/
    $ENV{CLIPPER_PATH}/cpp/build/
    $ENV{CLIPPER_PATH}/lib/
    $ENV{CLIPPER_PATH}/lib/polyclipping/
    ${PROJECT_SOURCE_DIR}/python/pymesh/third_party/lib/
    ${PROJECT_SOURCE_DIR}/python/pymesh/third_party/lib/polyclipping/
    ${CMAKE_PREFIX_PATH}/lib/
    ${CMAKE_PREFIX_PATH}/lib/polyclipping/
    /opt/local/lib/
    /opt/local/lib/polyclipping/
    /usr/local/lib/
    /usr/local/lib/polyclipping/
    /usr/lib/polyclipping)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Clipper
    "Clipper library cannot be found.  Consider set CLIPPER_PATH environment variable"
    CLIPPER_INCLUDE_DIRS
    CLIPPER_LIBRARIES)

MARK_AS_ADVANCED(
    CLIPPER_INCLUDE_DIRS
    CLIPPER_LIBRARIES)