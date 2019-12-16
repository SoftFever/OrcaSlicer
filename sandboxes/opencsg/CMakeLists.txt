cmake_minimum_required(VERSION 3.0)

project(OpenCSG-example)

add_executable(opencsg_example main.cpp  GLScene.hpp GLScene.cpp Canvas.hpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/GUI/ProgressStatusBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/GUI/I18N.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/GUI/I18N.cpp)

find_package(wxWidgets 3.1 REQUIRED COMPONENTS core base gl html)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenCSG REQUIRED)
find_package(GLUT REQUIRED)
include(${wxWidgets_USE_FILE})


target_link_libraries(opencsg_example libslic3r)
target_include_directories(opencsg_example PRIVATE ${wxWidgets_INCLUDE_DIRS})
target_compile_definitions(opencsg_example PRIVATE ${wxWidgets_DEFINITIONS})
target_link_libraries(opencsg_example ${wxWidgets_LIBRARIES} GLEW::GLEW OpenCSG::opencsg GLUT::GLUT OpenGL::OpenGL -lXrandr -lXext -lX11)
