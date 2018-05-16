

add_definitions(-D_BSD_SOURCE -D_DEFAULT_SOURCE)   # To enable various useful macros and functions on Unices
remove_definitions(-D_UNICODE -DUNICODE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Workaround for an old CMake, which does not understand CMAKE_C_STANDARD.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall")
endif()


set(AVRDUDE_SOURCES
    ${LIBDIR}/avrdude/arduino.c
    ${LIBDIR}/avrdude/avr.c
    # ${LIBDIR}/avrdude/avrftdi.c
    # ${LIBDIR}/avrdude/avrftdi_tpi.c
    ${LIBDIR}/avrdude/avrpart.c
    ${LIBDIR}/avrdude/avr910.c
    ${LIBDIR}/avrdude/bitbang.c
    ${LIBDIR}/avrdude/buspirate.c
    ${LIBDIR}/avrdude/butterfly.c
    ${LIBDIR}/avrdude/config.c
    ${LIBDIR}/avrdude/config_gram.c
    # ${LIBDIR}/avrdude/confwin.c
    ${LIBDIR}/avrdude/crc16.c
    # ${LIBDIR}/avrdude/dfu.c
    ${LIBDIR}/avrdude/fileio.c
    # ${LIBDIR}/avrdude/flip1.c
    # ${LIBDIR}/avrdude/flip2.c
    # ${LIBDIR}/avrdude/ft245r.c
    # ${LIBDIR}/avrdude/jtagmkI.c
    # ${LIBDIR}/avrdude/jtagmkII.c
    # ${LIBDIR}/avrdude/jtag3.c
    ${LIBDIR}/avrdude/lexer.c
    ${LIBDIR}/avrdude/linuxgpio.c
    ${LIBDIR}/avrdude/lists.c
    # ${LIBDIR}/avrdude/par.c
    ${LIBDIR}/avrdude/pgm.c
    ${LIBDIR}/avrdude/pgm_type.c
    ${LIBDIR}/avrdude/pickit2.c
    ${LIBDIR}/avrdude/pindefs.c
    # ${LIBDIR}/avrdude/ppi.c
    # ${LIBDIR}/avrdude/ppiwin.c
    ${LIBDIR}/avrdude/safemode.c
    ${LIBDIR}/avrdude/ser_avrdoper.c
    ${LIBDIR}/avrdude/serbb_posix.c
    ${LIBDIR}/avrdude/serbb_win32.c
    ${LIBDIR}/avrdude/ser_posix.c
    ${LIBDIR}/avrdude/ser_win32.c
    ${LIBDIR}/avrdude/stk500.c
    ${LIBDIR}/avrdude/stk500generic.c
    ${LIBDIR}/avrdude/stk500v2.c
    ${LIBDIR}/avrdude/term.c
    ${LIBDIR}/avrdude/update.c
    # ${LIBDIR}/avrdude/usbasp.c
    # ${LIBDIR}/avrdude/usb_hidapi.c
    # ${LIBDIR}/avrdude/usb_libusb.c
    # ${LIBDIR}/avrdude/usbtiny.c
    ${LIBDIR}/avrdude/wiring.c

    ${LIBDIR}/avrdude/main.c
    ${LIBDIR}/avrdude/avrdude-slic3r.hpp
    ${LIBDIR}/avrdude/avrdude-slic3r.cpp
)
if (WIN32)
    set(AVRDUDE_SOURCES ${AVRDUDE_SOURCES}
        ${LIBDIR}/avrdude/windows/unistd.cpp
        ${LIBDIR}/avrdude/windows/getopt.c
    )
endif()
add_library(avrdude STATIC ${AVRDUDE_SOURCES})

if (WIN32)
    target_compile_definitions(avrdude PRIVATE WIN32NATIVE=1)
    target_include_directories(avrdude SYSTEM PRIVATE ${LIBDIR}/avrdude/windows)    # So that sources find the getopt.h windows drop-in
endif()
