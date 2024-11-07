cmake_minimum_required(VERSION 2.8.12)
project(imgui)

add_library(imgui STATIC
    imconfig.h
    imgui.cpp
    imgui.h
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_stdlib.cpp
    imgui_stdlib.h
    imgui_tables.cpp
    imgui_widgets.cpp
    # imgui STB
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
    # imgui Freetype
    misc/freetype/imgui_freetype.h
    misc/freetype/imgui_freetype.cpp
)

if(Boost_FOUND)
    target_link_libraries(imgui PRIVATE boost_headeronly)
endif()

find_package(Freetype REQUIRED)

message("ft01 ${FREETYPE_INCLUDE_DIR_ft2build}")
message("ft02 ${FREETYPE_INCLUDE_DIR_freetype2}")
message("ft03 ${FREETYPE_INCLUDE_DIRS}")

target_link_libraries(imgui PRIVATE ${FREETYPE_LIBRARIES})
target_include_directories(imgui PRIVATE ${FREETYPE_INCLUDE_DIRS})
