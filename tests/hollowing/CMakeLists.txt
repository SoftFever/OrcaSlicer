if(TARGET OpenVDB::openvdb)
    add_executable(hollowing_tests hollowing_tests.cpp)

    find_package(GTest REQUIRED)

    target_link_libraries(hollowing_tests libslic3r OpenVDB::openvdb GTest::GTest GTest::Main)
    target_compile_definitions(hollowing_tests PRIVATE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)")
endif()
