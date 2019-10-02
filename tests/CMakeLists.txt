# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

find_package(GTest REQUIRED)

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)

add_library(test_common INTERFACE)
target_compile_definitions(test_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)")
target_link_libraries(test_common INTERFACE GTest::GTest GTest::Main)
if (APPLE)
    target_link_libraries(test_common INTERFACE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
endif()

add_subdirectory(sla_print)
