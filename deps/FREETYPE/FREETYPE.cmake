if(WIN32)
    set(library_build_shared "1")
else()
    set(library_build_shared "0")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_ft_disable_zlib "-D FT_DISABLE_ZLIB=FALSE")
else()
    set(_ft_disable_zlib "-D FT_DISABLE_ZLIB=TRUE")
endif()

orcaslicer_add_cmake_project(FREETYPE
    URL https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.gz
    URL_HASH SHA256=9186627467a616d9a9e3e574748ac0c45fa77398e6364edef3041a8ba8d92f64
    #DEPENDS ${ZLIB_PKG}
    #"${_patch_step}"
    CMAKE_ARGS
	-D BUILD_SHARED_LIBS=${library_build_shared}
	${_ft_disable_zlib}
        -D FT_DISABLE_BZIP2=TRUE
        -D FT_DISABLE_PNG=TRUE
        -D FT_DISABLE_HARFBUZZ=TRUE
        -D FT_DISABLE_BROTLI=TRUE
)

if(MSVC)
    add_debug_dep(dep_FREETYPE)
endif()
