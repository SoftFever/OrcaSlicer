# We have to check for OpenGL to compile GLEW
set(OpenGL_GL_PREFERENCE "LEGACY") # to prevent a nasty warning by cmake
find_package(OpenGL QUIET REQUIRED)

orcaslicer_add_cmake_project(
  GLEW
  GIT_REPOSITORY "https://github.com/nigels-com/glew"
  GIT_SHALLOW ON
  CMAKE_ARGS
    -DBUILD_UTILS=OFF
)

if (MSVC)
    add_debug_dep(dep_GLEW)
endif ()
