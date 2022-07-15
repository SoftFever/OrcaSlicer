cmake_minimum_required(VERSION 3.0)

project(EXPAT)

if (BUILD_SHARED_LIBS AND MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_library(expat
    xmlparse.c
    xmlrole.c
    xmltok.c
)

target_include_directories(expat PRIVATE ${PROJECT_SOURCE_DIR})

include(GNUInstallDirs)

install( 
    FILES
        ${PROJECT_SOURCE_DIR}/expat.h
        ${PROJECT_SOURCE_DIR}/expat_config.h
        ${PROJECT_SOURCE_DIR}/expat_external.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}
)

add_library(EXPAT INTERFACE)
target_link_libraries(EXPAT INTERFACE expat)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"     
    VERSION 1.95
    COMPATIBILITY AnyNewerVersion
)

install(TARGETS expat EXPAT
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} 
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

export(EXPORT ${PROJECT_NAME}Targets 
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake" 
       NAMESPACE ${PROJECT_NAME}:: )

set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(EXPORT ${PROJECT_NAME}Targets
  FILE
    "${PROJECT_NAME}Targets.cmake"
  NAMESPACE
    ${PROJECT_NAME}::
  DESTINATION
    ${ConfigPackageLocation}
)

configure_file(config.cmake.in ${PROJECT_NAME}Config.cmake @ONLY)

install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION
      ${ConfigPackageLocation}
)

