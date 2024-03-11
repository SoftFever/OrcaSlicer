set(_wx_version 3.1.5)
set(_wx_git_tag v${_wx_version})

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

set(_wx_orcaslicer_patch "${CMAKE_CURRENT_LIST_DIR}/0001-wx-3.1.5-patch-for-Orca.patch")
if (MSVC)
    set(_patch_cmd if not exist WXWIDGETS_PATCHED ( "${GIT_EXECUTABLE}" apply --verbose --ignore-space-change --whitespace=fix ${_wx_orcaslicer_patch} && type nul > WXWIDGETS_PATCHED ) )
else ()
    set(_patch_cmd test -f WXWIDGETS_PATCHED || ${PATCH_CMD} ${_wx_orcaslicer_patch} && touch WXWIDGETS_PATCHED)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_patch_cmd ${PATCH_CMD} ${_wx_orcaslicer_patch})
endif ()

# Note: for anybody wanting to switch to tarball fetching - this won't just work as
# git apply expects a git repo. Either git init empty repo, or change patching method.
# if (WIN32)
#     # Windows requires a different tarball because of configured line endings as stated in docs.
#     set(_wx_tarball_url https://github.com/wxWidgets/wxWidgets/releases/download/${_wx_git_tag}/wxWidgets-${_wx_version}.7z)
#     set(_wx_tarball_hash 99f5382312e4a4aea444ada07341a72c5d4a69b58d8e76586d4b94ede7f5ef4d)
# else()
#     set(_wx_tarball_url https://github.com/wxWidgets/wxWidgets/releases/download/${_wx_git_tag}/wxWidgets-${_wx_version}.tar.bz2)
#     set(_wx_tarball_hash d7b3666de33aa5c10ea41bb9405c40326e1aeb74ee725bb88f90f1d50270a224)
# endif()

orcaslicer_add_cmake_project(
    wxWidgets
    GIT_REPOSITORY "https://github.com/wxWidgets/wxWidgets"
    GIT_TAG ${_wx_git_tag}
    GIT_SHALLOW ON
    # URL ${_wx_tarball_url}
    # URL_HASH SHA256=${_wx_tarball_hash}
    PATCH_COMMAND ${_patch_cmd}
    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} dep_TIFF dep_JPEG
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
        -DwxUSE_REGEX=builtin
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