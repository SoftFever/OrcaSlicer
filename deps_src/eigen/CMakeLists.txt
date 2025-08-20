cmake_minimum_required(VERSION 2.8.12)
project(eigen)

add_library(eigen INTERFACE)

target_include_directories(eigen INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Eigen is header-only, so we only need to specify the include directory
# The headers are in the Eigen/ subdirectory structure