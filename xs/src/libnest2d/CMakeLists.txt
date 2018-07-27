cmake_minimum_required(VERSION 2.8)

project(Libnest2D)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
    # Update if necessary
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long ")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED)

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/)

option(LIBNEST2D_UNITTESTS "If enabled, googletest framework will be downloaded
    and the provided unit tests will be included in the build." OFF)

option(LIBNEST2D_BUILD_EXAMPLES "If enabled, examples will be built." OFF)

set(LIBNEST2D_GEOMETRIES_BACKEND "clipper" CACHE STRING
    "Build libnest2d with geometry classes implemented by the chosen backend.")

set(LIBNEST2D_OPTIMIZER_BACKEND "nlopt" CACHE STRING
    "Build libnest2d with optimization features implemented by the chosen backend.")

set(LIBNEST2D_SRCFILES
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/libnest2d.hpp         # Templates only
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d.h                     # Exports ready made types using template arguments
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/geometry_traits.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/common.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/optimizer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/metaloop.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/placers/placer_boilerplate.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/placers/bottomleftplacer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/placers/nfpplacer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/geometry_traits_nfp.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/selections/selection_boilerplate.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/selections/filler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/selections/firstfit.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/selections/djd_heuristic.hpp
    )

set(LIBNEST2D_LIBRARIES "")

set(LIBNEST2D_HEADERS ${CMAKE_CURRENT_SOURCE_DIR})

if(LIBNEST2D_GEOMETRIES_BACKEND STREQUAL "clipper")

    # Clipper backend is not enough on its own, it still needs some functions
    # from Boost geometry
    if(NOT Boost_INCLUDE_DIRS_FOUND)
        find_package(Boost 1.58 REQUIRED)
        # TODO automatic download of boost geometry headers
    endif()

    add_subdirectory(libnest2d/clipper_backend)

    include_directories(BEFORE ${CLIPPER_INCLUDE_DIRS})
    include_directories(${Boost_INCLUDE_DIRS})

    list(APPEND LIBNEST2D_SRCFILES ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/clipper_backend/clipper_backend.hpp
                                   ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/boost_alg.hpp)
    list(APPEND LIBNEST2D_LIBRARIES ${CLIPPER_LIBRARIES})
    list(APPEND LIBNEST2D_HEADERS ${CLIPPER_INCLUDE_DIRS}
                                  ${Boost_INCLUDE_DIRS_FOUND})
endif()

if(LIBNEST2D_OPTIMIZER_BACKEND STREQUAL "nlopt")
    find_package(NLopt 1.4)
    if(NOT NLopt_FOUND)
        message(STATUS  "NLopt not found so downloading "
                        "and automatic build is performed...")
        include(DownloadNLopt)
    endif()
    find_package(Threads REQUIRED)

    list(APPEND LIBNEST2D_SRCFILES  ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/optimizers/simplex.hpp
                                    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/optimizers/subplex.hpp
                                    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/optimizers/genetic.hpp
                                    ${CMAKE_CURRENT_SOURCE_DIR}/libnest2d/optimizers/nlopt_boilerplate.hpp)
    list(APPEND LIBNEST2D_LIBRARIES ${NLopt_LIBS})
    list(APPEND LIBNEST2D_HEADERS ${NLopt_INCLUDE_DIR})
endif()

if(LIBNEST2D_UNITTESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LIBNEST2D_BUILD_EXAMPLES)
    add_executable(example examples/main.cpp
#                           tools/libnfpglue.hpp
#                           tools/libnfpglue.cpp
                           tools/svgtools.hpp
                           tests/printer_parts.cpp
                           tests/printer_parts.h
                           ${LIBNEST2D_SRCFILES})


    target_link_libraries(example ${LIBNEST2D_LIBRARIES})
    target_include_directories(example PUBLIC ${LIBNEST2D_HEADERS})
endif()

get_directory_property(hasParent PARENT_DIRECTORY)
if(hasParent)
    set(LIBNEST2D_INCLUDES ${LIBNEST2D_HEADERS} PARENT_SCOPE)
    set(LIBNEST2D_LIBRARIES ${LIBNEST2D_LIBRARIES} PARENT_SCOPE)
endif()
