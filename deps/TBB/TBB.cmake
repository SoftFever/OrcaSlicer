if (MSVC)
    set(_patch_command ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/MSVC.cmake ./cmake/compilers/MSVC.cmake)
elseif (FLATPAK AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(_patch_command ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/GNU.cmake ./cmake/compilers/GNU.cmake)
else()
    set(_patch_command "")
endif()

# oneTBB v2021.5.0 unconditionally strips "-Werror" from CMAKE_CXX_FLAGS in
# cmake/compilers/Clang.cmake without accounting for the "-Werror=<warning>"
# form, so DEP_WERRORS_SDK's "-Werror=partial-availability" reaches the
# compiler as the malformed "=partial-availability" and every source file
# fails to compile. Clear the flags for TBB; the deployment target, sysroot
# and architecture still travel through their own variables, and TBB does
# not call SDK APIs the availability guard would catch.
set(_tbb_clear_flags "")
if (DEPS_IOS)
    set(_tbb_clear_flags
        "-DCMAKE_CXX_FLAGS="
        "-DCMAKE_C_FLAGS="
    )
endif ()

orcaslicer_add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.5.0.zip"
    URL_HASH SHA256=83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47
    PATCH_COMMAND ${_patch_command}
    CMAKE_ARGS
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        -DTBB_TEST=OFF
        -DTBB_ENABLE_IPO=OFF
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
        ${_tbb_clear_flags}
)

if (MSVC)
    add_debug_dep(dep_TBB)
endif ()


