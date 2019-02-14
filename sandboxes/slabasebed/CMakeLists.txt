add_executable(slabasebed EXCLUDE_FROM_ALL slabasebed.cpp)
target_link_libraries(slabasebed libslic3r ${Boost_LIBRARIES} ${TBB_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_DL_LIBS})
