find_path(LIBNOISE_INCLUDE_DIR libnoise/noise.h)
find_library(LIBNOISE_LIBRARY NAMES libnoise libnoise_static liblibnoise_static)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libnoise DEFAULT_MSG
    LIBNOISE_LIBRARY
    LIBNOISE_INCLUDE_DIR
)

if(libnoise_FOUND)
    add_library(noise::noise STATIC IMPORTED)
    set_target_properties(noise::noise PROPERTIES
        IMPORTED_LOCATION "${LIBNOISE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBNOISE_INCLUDE_DIR}"
    )
endif()