cmake_minimum_required(VERSION 2.8.12)
project(deps_src)

# Include all dependency subdirectories

# Header-only libraries (INTERFACE)
add_subdirectory(agg)
add_subdirectory(ankerl)
add_subdirectory(fast_float)
add_subdirectory(nanosvg)
add_subdirectory(nlohmann)

# Static libraries
add_subdirectory(Shiny)
add_subdirectory(admesh)
add_subdirectory(clipper)
add_subdirectory(expat)
add_subdirectory(glu-libtess)
add_subdirectory(hidapi)
add_subdirectory(imgui)
add_subdirectory(imguizmo)
add_subdirectory(libigl)
add_subdirectory(libnest2d)
add_subdirectory(mcut)
add_subdirectory(miniz)
add_subdirectory(minilzo)
add_subdirectory(qhull)
add_subdirectory(qoi)

# Eigen header-only library
add_subdirectory(eigen)