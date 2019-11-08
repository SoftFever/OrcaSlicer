get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    ${PROJECT_SOURCE_DIR}/src/libslic3r/Time.cpp
    ${PROJECT_SOURCE_DIR}/src/libslic3r/Time.hpp
    )
target_link_libraries(${_TEST_NAME}_tests test_common)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
