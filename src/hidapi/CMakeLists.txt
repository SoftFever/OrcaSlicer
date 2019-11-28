
if (WIN32)
    set(HIDAPI_IMPL win/hid.c)
elseif (APPLE)
    set(HIDAPI_IMPL mac/hid.c)
else ()
    # Assume Linux or Unix other than Mac OS
    set(HIDAPI_IMPL linux/hid.c)
endif()

include_directories(include)

add_library(hidapi STATIC ${HIDAPI_IMPL})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(hidapi udev)
endif()
