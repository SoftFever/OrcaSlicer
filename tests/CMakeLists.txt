# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)

add_library(Catch2 INTERFACE)
list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/Catch2)
target_include_directories(Catch2 INTERFACE ${CMAKE_CURRENT_LIST_DIR})
add_library(Catch2::Catch2 ALIAS Catch2)
include(Catch)

add_library(test_catch2_common INTERFACE)
target_compile_definitions(test_catch2_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)" CATCH_CONFIG_FAST_COMPILE)
target_link_libraries(test_catch2_common INTERFACE Catch2::Catch2)

add_library(test_common INTERFACE)
if (APPLE)
    target_link_libraries(test_common INTERFACE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
endif()

target_link_libraries(test_common INTERFACE test_catch2_common)

# DEPRECATED:
#find_package(GTest REQUIRED)
#add_library(test_gtest_common INTERFACE)
#target_compile_definitions(test_gtest_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)")
#target_link_libraries(test_gtest_common INTERFACE GTest::GTest GTest::Main)

add_subdirectory(libnest2d)
add_subdirectory(timeutils)
add_subdirectory(sla_print)

