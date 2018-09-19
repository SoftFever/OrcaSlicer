project(Slic3r-native)

add_subdirectory(admesh)
add_subdirectory(avrdude)
# boost/nowide
add_subdirectory(boost)
add_subdirectory(clipper)
add_subdirectory(miniz)
add_subdirectory(polypartition)
add_subdirectory(poly2tri)
add_subdirectory(qhull)
add_subdirectory(Shiny)
add_subdirectory(semver)

# Adding libnest2d project for bin packing...
set(LIBNEST2D_UNITTESTS ON CACHE BOOL "Force generating unittests for libnest2d")
add_subdirectory(libnest2d)

include_directories(${LIBDIR}/qhull/src)
#message(STATUS ${LIBDIR}/qhull/src)


# ##############################################################################
# Configure rasterizer target
# ##############################################################################

find_package(PNG QUIET)

option(RASTERIZER_FORCE_BUILTIN_LIBPNG "Force the usage of builting libpng instead of the system version." OFF)

if(PNG_FOUND AND NOT RASTERIZER_FORCE_BUILTIN_LIBPNG)
    message(STATUS "Using system libpng.")
else()
    set(ZLIB_LIBRARY "")
    message(WARNING "Using builtin libpng. This can cause crashes on some platforms.")
    add_subdirectory(png/zlib)
    set(ZLIB_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/png/zlib ${CMAKE_CURRENT_BINARY_DIR}/png/zlib)
    include_directories(${ZLIB_INCLUDE_DIR})
    message(STATUS "ZLIB_INCLUDE_DIR ${ZLIB_INCLUDE_DIR}")
    add_subdirectory(png/libpng)
    set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set_target_properties(png_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
#	target_include_directories(png_static PRIVATE ${ZLIB_INCLUDE_DIR})
    set(PNG_LIBRARIES png_static zlibstatic)
    set(PNG_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/png/libpng ${CMAKE_CURRENT_BINARY_DIR}/png/libpng)
endif()

add_subdirectory(libslic3r)
add_subdirectory(slic3r)

# Create a slic3r executable
add_executable(slic3r slic3r.cpp)
target_link_libraries(slic3r libslic3r libslic3r_gui ${wxWidgets_LIBRARIES} ${CURL_LIBRARIES})
# Add the OpenGL and GLU libraries.
if (SLIC3R_GUI)
    if (MSVC)
        target_link_libraries(slic3r user32.lib Setupapi.lib OpenGL32.Lib GlU32.Lib)
    elseif (MINGW)
        target_link_libraries(slic3r -lopengl32)
    elseif (APPLE)
        target_link_libraries(slic3r "-framework OpenGL")
    else ()
        target_link_libraries(slic3r -lGL -lGLU)
    endif ()
endif ()
