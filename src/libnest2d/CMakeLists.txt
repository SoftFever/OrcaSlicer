cmake_minimum_required(VERSION 3.0)

project(Libnest2D)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
    # Update if necessary
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long ")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED)

# Add our own cmake module path.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_modules/)

option(LIBNEST2D_UNITTESTS "If enabled, googletest framework will be downloaded
    and the provided unit tests will be included in the build." OFF)

option(LIBNEST2D_BUILD_EXAMPLES "If enabled, examples will be built." OFF)

option(LIBNEST2D_HEADER_ONLY "If enabled static library will not be built." ON)

set(GEOMETRY_BACKENDS clipper boost eigen)
set(LIBNEST2D_GEOMETRIES clipper CACHE STRING "Geometry backend")
set_property(CACHE LIBNEST2D_GEOMETRIES PROPERTY STRINGS ${GEOMETRY_BACKENDS})
list(FIND GEOMETRY_BACKENDS ${LIBNEST2D_GEOMETRIES} GEOMETRY_TYPE)
if(${GEOMETRY_TYPE} EQUAL -1)
    message(FATAL_ERROR "Option ${LIBNEST2D_GEOMETRIES} not supported, valid entries are ${GEOMETRY_BACKENDS}")
endif()

set(OPTIMIZERS nlopt optimlib)
set(LIBNEST2D_OPTIMIZER nlopt CACHE STRING "Optimization backend")
set_property(CACHE LIBNEST2D_OPTIMIZER PROPERTY STRINGS ${OPTIMIZERS})
list(FIND OPTIMIZERS ${LIBNEST2D_OPTIMIZER} OPTIMIZER_TYPE)
if(${OPTIMIZER_TYPE} EQUAL -1)
    message(FATAL_ERROR "Option ${LIBNEST2D_OPTIMIZER} not supported, valid entries are ${OPTIMIZERS}")
endif()

add_library(libnest2d INTERFACE)

set(SRC_DIR ${PROJECT_SOURCE_DIR}/include)

set(LIBNEST2D_SRCFILES
    ${SRC_DIR}/libnest2d/libnest2d.hpp         # Templates only
    ${SRC_DIR}/libnest2d/geometry_traits.hpp
    ${SRC_DIR}/libnest2d/geometry_traits_nfp.hpp
    ${SRC_DIR}/libnest2d/common.hpp
    ${SRC_DIR}/libnest2d/optimizer.hpp
    ${SRC_DIR}/libnest2d/utils/metaloop.hpp
    ${SRC_DIR}/libnest2d/utils/rotfinder.hpp
    ${SRC_DIR}/libnest2d/utils/rotcalipers.hpp
    ${SRC_DIR}/libnest2d/utils/bigint.hpp
    ${SRC_DIR}/libnest2d/utils/rational.hpp
    ${SRC_DIR}/libnest2d/placers/placer_boilerplate.hpp
    ${SRC_DIR}/libnest2d/placers/bottomleftplacer.hpp
    ${SRC_DIR}/libnest2d/placers/nfpplacer.hpp
    ${SRC_DIR}/libnest2d/selections/selection_boilerplate.hpp
    ${SRC_DIR}/libnest2d/selections/filler.hpp
    ${SRC_DIR}/libnest2d/selections/firstfit.hpp
    ${SRC_DIR}/libnest2d/selections/djd_heuristic.hpp
    )

set(TBB_STATIC ON)
find_package(TBB QUIET)
if(TBB_FOUND)
    message(STATUS "Parallelization with Intel TBB")
    target_include_directories(libnest2d INTERFACE ${TBB_INCLUDE_DIRS})
    target_compile_definitions(libnest2d INTERFACE ${TBB_DEFINITIONS} -DUSE_TBB)
    if(MSVC)
       # Suppress implicit linking of the TBB libraries by the Visual Studio compiler.
       target_compile_definitions(libnest2d INTERFACE -D__TBB_NO_IMPLICIT_LINKAGE)
    endif()
    # The Intel TBB library will use the std::exception_ptr feature of C++11.
    target_compile_definitions(libnest2d INTERFACE -DTBB_USE_CAPTURED_EXCEPTION=0)

    find_package(Threads REQUIRED)
    target_link_libraries(libnest2d INTERFACE 
        tbb # VS debug mode needs linking this way:
        # ${TBB_LIBRARIES} 
        ${CMAKE_DL_LIBS}
        Threads::Threads
        )
else()
   find_package(OpenMP QUIET)

   if(OpenMP_CXX_FOUND)
       message(STATUS "Parallelization with OpenMP")
       target_include_directories(libnest2d INTERFACE OpenMP::OpenMP_CXX)
       target_link_libraries(libnest2d INTERFACE OpenMP::OpenMP_CXX)
   else()
       message("Parallelization with C++11 threads")
       find_package(Threads REQUIRED)
       target_link_libraries(libnest2d INTERFACE Threads::Threads)
   endif()
endif()

add_subdirectory(${SRC_DIR}/libnest2d/backends/${LIBNEST2D_GEOMETRIES})
target_link_libraries(libnest2d INTERFACE ${LIBNEST2D_GEOMETRIES}Backend)

add_subdirectory(${SRC_DIR}/libnest2d/optimizers/${LIBNEST2D_OPTIMIZER})
target_link_libraries(libnest2d INTERFACE ${LIBNEST2D_OPTIMIZER}Optimizer)

# target_sources(libnest2d INTERFACE ${LIBNEST2D_SRCFILES})
target_include_directories(libnest2d INTERFACE ${SRC_DIR})

if(NOT LIBNEST2D_HEADER_ONLY)
    set(LIBNAME libnest2d_${LIBNEST2D_GEOMETRIES}_${LIBNEST2D_OPTIMIZER})
    add_library(${LIBNAME} ${PROJECT_SOURCE_DIR}/src/libnest2d.cpp)
    target_link_libraries(${LIBNAME} PUBLIC libnest2d)
    target_compile_definitions(${LIBNAME} PUBLIC LIBNEST2D_STATIC)
endif()

if(LIBNEST2D_BUILD_EXAMPLES)

    add_executable(example     examples/main.cpp
    #                           tools/libnfpglue.hpp
    #                           tools/libnfpglue.cpp
                               tools/nfp_svgnest.hpp
                               tools/nfp_svgnest_glue.hpp
                               tools/svgtools.hpp
                               tests/printer_parts.cpp
                               tests/printer_parts.h
                               )

    if(NOT LIBNEST2D_HEADER_ONLY)
        target_link_libraries(example ${LIBNAME})
    else()
        target_link_libraries(example libnest2d)
    endif()
endif()

if(LIBNEST2D_UNITTESTS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif()
