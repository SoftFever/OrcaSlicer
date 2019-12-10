
prusaslicer_add_cmake_project(OpenCSG
    GIT_REPOSITORY https://github.com/floriankirsch/OpenCSG.git
    GIT_TAG 83e274457b46c9ad11a4ee599203250b1618f3b9 #v1.4.2
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in ./CMakeLists.txt
    DEPENDS dep_GLEW
)