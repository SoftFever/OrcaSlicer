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

if (SLIC3R_GUI)
    message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    find_package(wxWidgets REQUIRED COMPONENTS base core adv html gl)
    include(${wxWidgets_USE_FILE})
endif()

add_subdirectory(slic3r)

# Create a slic3r executable
# Process mainfests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/slic3r.rc.in ${CMAKE_CURRENT_BINARY_DIR}/slic3r.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/msw/slic3r.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/slic3r.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
if (MSVC)
    add_library(slic3r SHARED slic3r.cpp)
else ()
    add_executable(slic3r slic3r.cpp)
endif ()
if (NOT MSVC)
    if(SLIC3R_GUI)
        set_target_properties(slic3r PROPERTIES OUTPUT_NAME "slic3r-gui")
    else()
        set_target_properties(slic3r PROPERTIES OUTPUT_NAME "slic3r-console")
    endif()
endif ()

target_link_libraries(slic3r libslic3r)
if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(slic3r "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
elseif (MSVC)
    # Manifest is provided through slic3r.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS /MANIFEST:NO)
else ()
    target_link_libraries(slic3r -ldl -lstdc++)
endif ()

# Add the Slic3r GUI library, libcurl, OpenGL and GLU libraries.
if (SLIC3R_GUI)
    target_link_libraries(slic3r libslic3r_gui ${wxWidgets_LIBRARIES})

    # Configure libcurl & OpenSSL
    find_package(CURL REQUIRED)
    target_include_directories(slic3r PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(slic3r CURL::libcurl)
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

# On Windows, a shim application is required to produce a console / non console version of the Slic3r application.
# Also the shim may load the Mesa software OpenGL renderer if the default renderer does not support OpenGL 2.0 and higher.
if (MSVC)
    add_executable(slic3r_app_gui WIN32 slic3r_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/slic3r.rc)
    target_compile_definitions(slic3r_app_gui PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE -DSLIC3R_WRAPPER_GUI)
    add_dependencies(slic3r_app_gui slic3r)
    set_target_properties(slic3r_app_gui PROPERTIES OUTPUT_NAME "slic3r")

    add_executable(slic3r_app_console slic3r_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/slic3r.rc)
    target_compile_definitions(slic3r_app_console PRIVATE -DSLIC3R_WRAPPER_CONSOLE -DSLIC3R_WRAPPER_NOGUI)
    add_dependencies(slic3r_app_console slic3r)
    set_target_properties(slic3r_app_console PROPERTIES OUTPUT_NAME "slic3r-console")

    add_executable(slic3r_app_noconsole WIN32 slic3r_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/slic3r.rc)
    target_compile_definitions(slic3r_app_noconsole PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE -DSLIC3R_WRAPPER_NOGUI)
    add_dependencies(slic3r_app_noconsole slic3r)
    set_target_properties(slic3r_app_noconsole PROPERTIES OUTPUT_NAME "slic3r-noconsole")
endif ()

# Link the resources dir to where Slic3r GUI expects it
if (MSVC)
    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET slic3r POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )
        endforeach ()
    else ()
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/resources" WIN_RESOURCES_SYMLINK)
        add_custom_command(TARGET slic3r POST_BUILD
            COMMAND if not exist "${WIN_RESOURCES_SYMLINK}" "(" mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}" ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
    endif ()
else ()
    add_custom_command(TARGET slic3r POST_BUILD
        COMMAND ln -sf "${SLIC3R_RESOURCES_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/../resources"
        COMMENT "Symlinking the resources directory into the build tree"
        VERBATIM
    )
endif()
