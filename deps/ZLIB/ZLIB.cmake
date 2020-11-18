prusaslicer_add_cmake_project(ZLIB
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG v1.2.11
  PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
                      ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-Respect-BUILD_SHARED_LIBS.patch
  CMAKE_ARGS
    -DSKIP_INSTALL_FILES=ON         # Prevent installation of man pages et al.
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

