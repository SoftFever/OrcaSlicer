set(_wx_git_tag v3.2.1)
set(_wx_patch_name 0001-patch-v3.2.1-for-OrcaSlicer.patch)

set(_wx_toolkit "")
    set(_wx_private_font "-DwxUSE_PRIVATE_FONTS=1")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
endif()

if (MSVC)
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=ON")
else ()
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=OFF")
endif ()

if (MSVC)
    set(_patch_cmd if not exist WXWIDGETS_PATCHED ( "${GIT_EXECUTABLE}" apply --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/${_wx_patch_name} && type nul > WXWIDGETS_PATCHED ) )
else ()
    set(_patch_cmd test -f WXWIDGETS_PATCHED || ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/${_wx_patch_name} && touch WXWIDGETS_PATCHED)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_patch_cmd ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/${_wx_patch_name})
endif ()

orcaslicer_add_cmake_project(
    wxWidgets
    GIT_REPOSITORY "https://github.com/wxWidgets/wxWidgets"
    GIT_TAG ${_wx_git_tag}
    PATCH_COMMAND ${_patch_cmd}
    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} dep_TIFF dep_JPEG dep_NanoSVG
    CMAKE_ARGS
        -DwxBUILD_PRECOMP=ON
        ${_wx_toolkit}
        "-DCMAKE_DEBUG_POSTFIX:STRING="
        -DwxBUILD_DEBUG_LEVEL=0
        -DwxBUILD_SAMPLES=OFF
        -DwxBUILD_SHARED=OFF
        -DwxUSE_MEDIACTRL=ON
        -DwxUSE_DETECT_SM=OFF
        -DwxUSE_UNICODE=ON
        ${_wx_private_font}
        -DwxUSE_OPENGL=ON
        -DwxUSE_WEBVIEW=ON
        ${_wx_edge}
        -DwxUSE_WEBVIEW_IE=OFF
        -DwxUSE_NANOSVG=sys
        -DwxUSE_NANOSVG_EXTERNAL=ON
        -DwxUSE_REGEX=OFF
        -DwxUSE_LIBXPM=builtin
        -DwxUSE_LIBSDL=OFF
        -DwxUSE_XTEST=OFF
        -DwxUSE_STC=OFF
        -DwxUSE_AUI=ON
        -DwxUSE_LIBPNG=sys
        -DwxUSE_ZLIB=sys
        -DwxUSE_LIBJPEG=sys
        -DwxUSE_LIBTIFF=sys
        -DwxUSE_EXPAT=sys
)

if (MSVC)
    add_debug_dep(dep_wxWidgets)
endif ()