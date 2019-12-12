# We have to check for OpenGL to compile GLEW
find_package(OpenGL QUIET REQUIRED)

prusaslicer_add_cmake_project(
  GLEW
  SOURCE_DIR  ${CMAKE_CURRENT_LIST_DIR}/glew
)

if (MSVC)
    add_debug_dep(dep_GLEW)
endif ()
