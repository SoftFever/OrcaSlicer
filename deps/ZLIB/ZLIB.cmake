prusaslicer_add_cmake_project(ZLIB
  # GIT_REPOSITORY https://github.com/madler/zlib.git
  # GIT_TAG v1.2.11
  URL https://github.com/madler/zlib/archive/refs/tags/v1.2.11.zip
  URL_HASH SHA256=f5cc4ab910db99b2bdbba39ebbdc225ffc2aa04b4057bc2817f1b94b6978cfc3
  PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Respect-BUILD_SHARED_LIBS.patch
  CMAKE_ARGS
    -DSKIP_INSTALL_FILES=ON         # Prevent installation of man pages et al.
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

