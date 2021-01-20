if (APPLE)
    # The new OSX 11 (Big Sur) is not compatible with wxWidgets 3.1.3.
    # Let's use patched wxWidgets 3.1.4, even though it is not quite tested.
    set(_wx_git_tag v3.1.4-patched)
else ()
    # Use the tested patched wxWidgets 3.1.3 everywhere else.
    set(_wx_git_tag v3.1.3-patched)
endif ()

# set(_patch_command "")
set(_wx_toolkit "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
endif()

prusaslicer_add_cmake_project(wxWidgets
    GIT_REPOSITORY "https://github.com/prusa3d/wxWidgets"
    GIT_TAG ${_wx_git_tag}
    # PATCH_COMMAND "${_patch_command}"
    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG}
    CMAKE_ARGS
        -DwxBUILD_PRECOMP=ON
        ${_wx_toolkit}
        "-DCMAKE_DEBUG_POSTFIX:STRING="
        -DwxBUILD_DEBUG_LEVEL=0
        -DwxUSE_DETECT_SM=OFF
        -DwxUSE_UNICODE=ON
        -DwxUSE_OPENGL=ON
        -DwxUSE_LIBPNG=sys
        -DwxUSE_ZLIB=sys
        -DwxUSE_REGEX=builtin
        -DwxUSE_LIBXPM=builtin
        -DwxUSE_LIBJPEG=builtin
        -DwxUSE_LIBTIFF=builtin
        -DwxUSE_EXPAT=sys
)

if (MSVC)
    add_debug_dep(dep_wxWidgets)
endif ()