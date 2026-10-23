set(_wx_toolkit "")
set(_wx_debug_postfix "")
set(_wx_shared -DwxBUILD_SHARED=OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gtk_ver 2)

    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()

    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
    if (FLATPAK)
        set(_wx_debug_postfix "d")
        set(_wx_shared -DwxBUILD_SHARED=ON -DBUILD_SHARED_LIBS:BOOL=ON)
    endif ()
endif()

# Build options and patches live in the Orca-deps-wxWidgets fork (v3.3.2 plus the OrcaSlicer patches).
orcaslicer_add_cmake_project(
    wxWidgets
    GIT_REPOSITORY "https://github.com/OrcaSlicer/Orca-deps-wxWidgets"
    GIT_TAG ac8811d6531bf1b11ad3f990c07701a59c3bf741
    GIT_SHALLOW ON
    GIT_SUBMODULES 3rdparty/catch 3rdparty/pcre 3rdparty/libwebp
    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} ${JPEG_PKG}
    CMAKE_ARGS
        ${_wx_toolkit}
        "-DCMAKE_DEBUG_POSTFIX:STRING=${_wx_debug_postfix}"
        ${_wx_shared}
)

if (MSVC)
    add_debug_dep(dep_wxWidgets)
endif ()
