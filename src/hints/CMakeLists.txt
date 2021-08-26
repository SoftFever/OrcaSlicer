cmake_minimum_required(VERSION 3.13)
project(HintsToPot)

add_executable(hintsToPot
    HintsToPot.cpp)

target_link_libraries(hintsToPot PRIVATE boost_libs)

#encoding_check(HintsToPot)



