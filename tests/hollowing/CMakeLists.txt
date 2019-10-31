if(TARGET OpenVDB::openvdb)
    add_executable(hollowing_tests hollowing_test_main.cpp hollowing_tests.cpp openvdb_utils.cpp openvdb_utils.hpp)

    #find_package(GTest REQUIRED)
    #target_link_libraries(hollowing_tests libslic3r OpenVDB::openvdb GTest::GTest GTest::Main)
    #target_compile_definitions(hollowing_tests PRIVATE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)")
    
    target_link_libraries(hollowing_tests test_common libslic3r OpenVDB::openvdb)
endif()
