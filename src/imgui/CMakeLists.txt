project(imgui)
cmake_minimum_required(VERSION 2.6)

add_library(imgui STATIC
    imconfig.h
    imgui.h
    imgui_internal.h
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
    imgui_tables.cpp
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_widgets.cpp
)
