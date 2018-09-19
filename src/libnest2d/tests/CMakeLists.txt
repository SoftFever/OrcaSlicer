
# Try to find existing GTest installation
find_package(GTest 1.7)

if(NOT GTEST_FOUND)
    message(STATUS "GTest not found so downloading...")
    # Go and download google test framework, integrate it with the build
    set(GTEST_LIBS_TO_LINK gtest gtest_main)

    if (CMAKE_VERSION VERSION_LESS 3.2)
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
    else()
        set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
    endif()

    include(DownloadProject)
    download_project(PROJ                googletest
                     GIT_REPOSITORY      https://github.com/google/googletest.git
                     GIT_TAG             release-1.7.0
                     ${UPDATE_DISCONNECTED_IF_AVAILABLE}
    )

    # Prevent GoogleTest from overriding our compiler/linker options
    # when building with Visual Studio
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    add_subdirectory(${googletest_SOURCE_DIR}
                     ${googletest_BINARY_DIR}
                     )

    set(GTEST_INCLUDE_DIRS ${googletest_SOURCE_DIR}/include)

else()
    find_package(Threads REQUIRED)
    set(GTEST_LIBS_TO_LINK  ${GTEST_BOTH_LIBRARIES}  Threads::Threads)
endif()

add_executable(bp2d_tests   test.cpp
                            ../tools/svgtools.hpp
#                            ../tools/libnfpglue.hpp
#                            ../tools/libnfpglue.cpp
                            printer_parts.h
                            printer_parts.cpp
                            ${LIBNEST2D_SRCFILES}
                            )
target_link_libraries(bp2d_tests ${LIBNEST2D_LIBRARIES} ${GTEST_LIBS_TO_LINK} )

target_include_directories(bp2d_tests PRIVATE BEFORE ${LIBNEST2D_HEADERS}
                                                     ${GTEST_INCLUDE_DIRS})

add_test(libnest2d_tests bp2d_tests)
