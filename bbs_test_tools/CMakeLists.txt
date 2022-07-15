# Add individual tools as executables in separate directories
add_subdirectory(bbs_gcode_checker)

install(TARGETS bbs_gcode_checker RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
