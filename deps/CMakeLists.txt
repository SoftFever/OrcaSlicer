project(Slic3r-deps)
cmake_minimum_required(VERSION 3.2)

include(ExternalProject)

include(ProcessorCount)
ProcessorCount(NPROC)
if (NPROC EQUAL 0)
    set(NPROC 1)
endif ()

if (MSVC)
    set(DEPS_BITS 64)
    set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir-64")
    include("deps-windows.cmake")
    set(DEPS_BITS 32)
    set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir-32")
    include("deps-windows.cmake")

    add_custom_target(deps ALL
        DEPENDS dep_boost_64 dep_tbb_64 dep_libcurl_64 dep_wxwidgets_64
        #         dep_boost_32 dep_tbb_32 dep_libcurl_32 dep_wxwidgets_32
    )
else ()
    set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir")
    include("deps-unix-static.cmake")

    add_custom_target(deps ALL
        DEPENDS dep_boost dep_tbb dep_libcurl dep_wxwidgets
    )
endif()
