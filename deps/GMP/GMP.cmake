
set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/gmp)
set(_dstdir ${DESTDIR}/usr/local)

if (MSVC)
    set(_output  ${_dstdir}/include/gmp.h 
                 ${_dstdir}/lib/libgmp-10.lib 
                 ${_dstdir}/lib/libgmp-10.dll)

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/gmp.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libgmp-10.lib ${_dstdir}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libgmp-10.dll ${_dstdir}/lib/
    )
    
    add_custom_target(dep_GMP SOURCES ${_output})

endif ()