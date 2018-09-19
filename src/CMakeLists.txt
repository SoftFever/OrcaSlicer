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
    add_subdirectory(png/libpng)
    set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set_target_properties(png_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(PNG_LIBRARIES png_static zlibstatic)
    set(PNG_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/png/libpng ${CMAKE_CURRENT_BINARY_DIR}/png/libpng)
endif()

add_subdirectory(libslic3r)
add_subdirectory(slic3r)

# Create a slic3r executable
add_executable(slic3r slic3r.cpp)
if(SLIC3R_GUI)
    set_target_properties(slic3r PROPERTIES OUTPUT_NAME "slic3r-gui")
else()
    set_target_properties(slic3r PROPERTIES OUTPUT_NAME "slic3r-console")
endif()
target_link_libraries(slic3r libslic3r)
if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(slic3r "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
elseif (NOT MSVC)
    target_link_libraries(slic3r -ldl -lstdc++)
endif ()

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
    message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    find_package(wxWidgets REQUIRED COMPONENTS base core adv html gl)
    include(${wxWidgets_USE_FILE})

    target_link_libraries(slic3r libslic3r_gui ${wxWidgets_LIBRARIES} ${CURL_LIBRARIES})

    # Configure libcurl & OpenSSL
    find_package(CURL REQUIRED)
    target_include_directories(slic3r PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(slic3r ${CURL_LIBRARIES})
    if (SLIC3R_STATIC)
        if (NOT APPLE)
            # libcurl is always linked dynamically to the system libcurl on OSX.
            # On other systems, libcurl is linked statically if SLIC3R_STATIC is set.
            target_compile_definitions(slic3r PRIVATE CURL_STATICLIB)
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # As of now, our build system produces a statically linked libcurl,
            # which links the OpenSSL library dynamically.
            find_package(OpenSSL REQUIRED)
            message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
            message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
            target_include_directories(slic3r PRIVATE ${OPENSSL_INCLUDE_DIR})
            target_link_libraries(slic3r ${OPENSSL_LIBRARIES})
        endif()
    endif()

    if (MSVC)
        target_link_libraries(slic3r user32.lib Setupapi.lib OpenGL32.Lib GlU32.Lib)
    elseif (MINGW)
        target_link_libraries(slic3r -lopengl32)
    elseif (APPLE)
        target_link_libraries(slic3r "-framework OpenGL")
    else ()
        target_link_libraries(slic3r -ldl -lGL -lGLU)
    endif ()
endif ()
