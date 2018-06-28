cmake_minimum_required(VERSION 2.8)

project(Libnest2D)

enable_testing()

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  # Update if necessary
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long ")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED)

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/)

option(LIBNEST2D_UNITTESTS "If enabled, googletest framework will be downloaded
    and the provided unit tests will be included in the build." OFF)

option(LIBNEST2D_BUILD_EXAMPLES "If enabled, examples will be built." OFF)
option(LIBNEST2D_BUILD_SHARED_LIB "Build shared library." ON)

#set(LIBNEST2D_GEOMETRIES_TARGET "" CACHE STRING
#    "Build libnest2d with geometry classes implemented by the chosen target.")

#set(libnest2D_TEST_LIBRARIES "" CACHE STRING
#    "Libraries needed to compile the test executable for libnest2d.")


set(LIBNEST2D_SRCFILES
    libnest2d/libnest2d.hpp       # Templates only
    libnest2d.h         # Exports ready made types using template arguments
    libnest2d/geometry_traits.hpp
    libnest2d/geometries_io.hpp
    libnest2d/common.hpp
    libnest2d/optimizer.hpp
    libnest2d/placers/placer_boilerplate.hpp
    libnest2d/placers/bottomleftplacer.hpp
    libnest2d/placers/nfpplacer.hpp
    libnest2d/geometries_nfp.hpp
    libnest2d/selections/selection_boilerplate.hpp
    libnest2d/selections/filler.hpp
    libnest2d/selections/firstfit.hpp
    libnest2d/selections/djd_heuristic.hpp
    libnest2d/optimizers/simplex.hpp
    libnest2d/optimizers/subplex.hpp
    libnest2d/optimizers/genetic.hpp
    libnest2d/optimizers/nlopt_boilerplate.hpp
    )

if((NOT LIBNEST2D_GEOMETRIES_TARGET) OR (LIBNEST2D_GEOMETRIES_TARGET STREQUAL ""))
    message(STATUS "libnest2D backend is default")

    if(NOT Boost_INCLUDE_DIRS_FOUND)
        find_package(Boost 1.58 REQUIRED)
        # TODO automatic download of boost geometry headers
    endif()

    add_subdirectory(libnest2d/clipper_backend)

    set(LIBNEST2D_GEOMETRIES_TARGET ${CLIPPER_LIBRARIES})

    include_directories(BEFORE ${CLIPPER_INCLUDE_DIRS})
    include_directories(${Boost_INCLUDE_DIRS})

    list(APPEND LIBNEST2D_SRCFILES libnest2d/clipper_backend/clipper_backend.cpp
                                   libnest2d/clipper_backend/clipper_backend.hpp
                                   libnest2d/boost_alg.hpp)

else()
    message(STATUS "Libnest2D backend is: ${LIBNEST2D_GEOMETRIES_TARGET}")
endif()

message(STATUS "clipper lib is: ${LIBNEST2D_GEOMETRIES_TARGET}")

find_package(NLopt 1.4)
if(NOT NLopt_FOUND)
    message(STATUS "NLopt not found so downloading and automatic build is performed...")
    include(DownloadNLopt)
endif()
find_package(Threads REQUIRED)

add_library(libnest2d_static STATIC ${LIBNEST2D_SRCFILES} )
target_link_libraries(libnest2d_static PRIVATE ${LIBNEST2D_GEOMETRIES_TARGET} ${NLopt_LIBS})
target_include_directories(libnest2d_static PUBLIC ${CMAKE_SOURCE_DIR} ${NLopt_INCLUDE_DIR})
set_target_properties(libnest2d_static PROPERTIES PREFIX "")

if(LIBNEST2D_BUILD_SHARED_LIB)
    add_library(libnest2d SHARED ${LIBNEST2D_SRCFILES} )
    target_link_libraries(libnest2d PRIVATE ${LIBNEST2D_GEOMETRIES_TARGET} ${NLopt_LIBS})
    target_include_directories(libnest2d PUBLIC ${CMAKE_SOURCE_DIR}  ${NLopt_INCLUDE_DIR})
    set_target_properties(libnest2d PROPERTIES PREFIX "")
endif()

set(LIBNEST2D_HEADERS ${CMAKE_CURRENT_SOURCE_DIR})

get_directory_property(hasParent PARENT_DIRECTORY)
if(hasParent)
    set(LIBNEST2D_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
endif()

if(LIBNEST2D_UNITTESTS)
    add_subdirectory(tests)
endif()

if(LIBNEST2D_BUILD_EXAMPLES)
    add_executable(example tests/main.cpp
                           tests/svgtools.hpp
                           tests/printer_parts.cpp
                           tests/printer_parts.h)
    target_link_libraries(example libnest2d_static Threads::Threads)
    target_include_directories(example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()
