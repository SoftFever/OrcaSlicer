project(miniz)
cmake_minimum_required(VERSION 2.6)

add_library(miniz INTERFACE)

# find_package(Miniz 2.1 REQUIRED)
# if(Miniz_FOUND)

# target_link_libraries(miniz INTERFACE ${Miniz_LIBRARIES})
# target_include_directories(miniz INTERFACE ${Miniz_INCLUDE_DIRS})

# else()

add_library(miniz_static STATIC
    miniz.c
    miniz.h
)

target_link_libraries(miniz INTERFACE miniz_static)
target_include_directories(miniz INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# endif()
