add_executable(openvdb_example openvdb_example.cpp)

find_package(OpenVDB 5.0 REQUIRED COMPONENTS openvdb)
slic3r_remap_configs(IlmBase::Half RelWithDebInfo Release)

target_link_libraries(openvdb_example libslic3r)
target_link_libraries(openvdb_example OpenVDB::openvdb)

