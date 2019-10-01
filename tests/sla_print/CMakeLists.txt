add_executable(sla_print_tests sla_print_tests_main.cpp)
target_link_libraries(sla_print_tests test_common libslic3r ${Boost_LIBRARIES} ${TBB_LIBRARIES} ${Boost_LIBRARIES})

add_test(sla_print_tests sla_print_tests)
#gtest_discover_tests(sla_print_tests TEST_PREFIX sla_print.)
