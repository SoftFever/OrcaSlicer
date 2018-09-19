if(NOT TARGET clipper) # If there is a clipper target in the parent project we are good to go.

    find_package(Clipper 6.1)

    if(NOT CLIPPER_FOUND)
        find_package(Subversion QUIET)
        if(Subversion_FOUND)        
            message(STATUS "Clipper not found so it will be downloaded.")
            # Silently download and build the library in the build dir

            if (CMAKE_VERSION VERSION_LESS 3.2)
                set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
            else()
                set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
            endif()

            include(DownloadProject)
            download_project(   PROJ                clipper_library
                                SVN_REPOSITORY      https://svn.code.sf.net/p/polyclipping/code/trunk/cpp
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

            add_library(clipper_lib STATIC
                ${clipper_library_SOURCE_DIR}/clipper.cpp
                ${clipper_library_SOURCE_DIR}/clipper.hpp)

            set(CLIPPER_INCLUDE_DIRS ${clipper_library_SOURCE_DIR}
                PARENT_SCOPE)
    
            set(CLIPPER_LIBRARIES clipper_lib PARENT_SCOPE)

        else()
            message(FATAL_ERROR "Can't find clipper library and no SVN client found to download.
                You can download the clipper sources and define a clipper target in your project, that will work for libnest2d.")
        endif()
    endif()
else()
    set(CLIPPER_LIBRARIES clipper PARENT_SCOPE)
endif()
