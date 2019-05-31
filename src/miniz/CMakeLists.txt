project(miniz)
cmake_minimum_required(VERSION 2.6)

add_library(miniz STATIC
    miniz.h
    miniz_common.h
    miniz_tdef.h
    miniz_tinfl.h
    miniz_zip.h
    miniz.cpp
    miniz_tdef.cpp
    miniz_tinfl.cpp
    miniz_zip.cpp
)

target_link_libraries(miniz nowide)
