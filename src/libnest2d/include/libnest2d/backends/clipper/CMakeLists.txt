if(NOT TARGET clipper) # If there is a clipper target in the parent project we are good to go.

    find_package(Clipper 6.1)

    if(NOT CLIPPER_FOUND)
        find_package(Subversion QUIET)
        if(Subversion_FOUND)        

            set(URL_CLIPPER "https://svn.code.sf.net/p/polyclipping/code/trunk/cpp" 
            CACHE STRING "Clipper source code repository location.")

            message(STATUS "Clipper not found so it will be downloaded.")
            # Silently download and build the library in the build dir

            if (CMAKE_VERSION VERSION_LESS 3.2)
                set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
            else()
                set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
            endif()

            include(DownloadProject)
            download_project(   PROJ                clipper_library
                                SVN_REPOSITORY      ${URL_CLIPPER}
                                SVN_REVISION        -r540
                                #SOURCE_SUBDIR       cpp
                                INSTALL_COMMAND     ""
                                CONFIGURE_COMMAND   "" # Not working, I will just add the source files
                                ${UPDATE_DISCONNECTED_IF_AVAILABLE}
            )

            # This is not working and I dont have time to fix it
            # add_subdirectory(${clipper_library_SOURCE_DIR}/cpp
            #                  ${clipper_library_BINARY_DIR}
            # )

            add_library(clipperBackend STATIC
                ${clipper_library_SOURCE_DIR}/clipper.cpp
                ${clipper_library_SOURCE_DIR}/clipper.hpp)

            target_include_directories(clipperBackend INTERFACE ${clipper_library_SOURCE_DIR})
        else()
            message(FATAL_ERROR "Can't find clipper library and no SVN client found to download.
                You can download the clipper sources and define a clipper target in your project, that will work for libnest2d.")
        endif()
    else()
        add_library(clipperBackend INTERFACE)
        target_link_libraries(clipperBackend INTERFACE Clipper::Clipper)
    endif()
else()
    # set(CLIPPER_INCLUDE_DIRS "" PARENT_SCOPE)
    # set(CLIPPER_LIBRARIES clipper PARENT_SCOPE)
    add_library(clipperBackend INTERFACE)
    target_link_libraries(clipperBackend INTERFACE clipper)
endif()

# Clipper backend is not enough on its own, it still needs some functions
# from Boost geometry
if(NOT Boost_FOUND)
    find_package(Boost 1.58 REQUIRED)
    # TODO automatic download of boost geometry headers
endif()

target_link_libraries(clipperBackend INTERFACE Boost::boost )
#target_sources(ClipperBackend INTERFACE
#    ${CMAKE_CURRENT_SOURCE_DIR}/geometries.hpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/clipper_polygon.hpp
#    ${SRC_DIR}/libnest2d/utils/boost_alg.hpp )

target_compile_definitions(clipperBackend INTERFACE LIBNEST2D_BACKEND_CLIPPER)

# And finally plug the clipperBackend into libnest2d
# target_link_libraries(libnest2d INTERFACE clipperBackend)

