/* ac_cfg.h.  Generated from ac_cfg.h.in by configure.  */
/* ac_cfg.h.in.  Generated from configure.ac by autoheader.  */


// Edited by hand for usage with Slic3r PE

#define CONFIG_DIR "CONFIG_DIR"


/* Define to 1 if you have the <ddk/hidsdi.h> header file. */
/* #undef HAVE_DDK_HIDSDI_H */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `gettimeofday' function. */
#if defined (WIN32NATIVE)
/* #undef HAVE_GETTIMEOFDAY */
// We have a gettimeofday() replacement in unistd.cpp (there is also one in ppiwin.c, but that file is written for Cygwin/MinGW)
#else
#define HAVE_GETTIMEOFDAY 1
#endif

/* Define to 1 if you have the <hidapi/hidapi.h> header file. */
/* #undef HAVE_HIDAPI_HIDAPI_H */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if ELF support is enabled via libelf */
// #define HAVE_LIBELF 1

/* Define to 1 if you have the <libelf.h> header file. */
// #define HAVE_LIBELF_H 1

/* Define to 1 if you have the <libelf/libelf.h> header file. */
/* #undef HAVE_LIBELF_LIBELF_H */

/* Define if FTDI support is enabled via libftdi */
/* #undef HAVE_LIBFTDI */

/* Define if FTDI support is enabled via libftdi1 */
// #define HAVE_LIBFTDI1 1

/* Define if libftdi supports FT232H, libftdi version >= 0.20 */
/* #undef HAVE_LIBFTDI_TYPE_232H */

/* Define if HID support is enabled via the Win32 DDK */
/* #undef HAVE_LIBHID */

/* Define if HID support is enabled via libhidapi */
/* #undef HAVE_LIBHIDAPI */

/* Define to 1 if you have the `ncurses' library (-lncurses). */
// #define HAVE_LIBNCURSES 1

/* Define to 1 if you have the `readline' library (-lreadline). */
// #define HAVE_LIBREADLINE 1

/* Define to 1 if you have the `termcap' library (-ltermcap). */
/* #undef HAVE_LIBTERMCAP */

/* Define if USB support is enabled via libusb */
// #define HAVE_LIBUSB 1

/* Define if USB support is enabled via a libusb-1.0 compatible libusb */
// #define HAVE_LIBUSB_1_0 1

/* Define to 1 if you have the <libusb-1.0/libusb.h> header file. */
// #define HAVE_LIBUSB_1_0_LIBUSB_H 1

/* Define to 1 if you have the <libusb.h> header file. */
/* #undef HAVE_LIBUSB_H */

/* Define to 1 if you have the `ws2_32' library (-lws2_32). */
/* #undef HAVE_LIBWS2_32 */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Linux sysfs GPIO support enabled */
/* #undef HAVE_LINUXGPIO */

/* Define to 1 if you have the <lusb0_usb.h> header file. */
/* #undef HAVE_LUSB0_USB_H */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* parallel port access enabled */
// #define HAVE_PARPORT 1

/* Define to 1 if you have the <pthread.h> header file. */
// #define HAVE_PTHREAD_H 1

/* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strcasecmp' function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the `strtol' function. */
#define HAVE_STRTOL 1

/* Define to 1 if you have the `strtoul' function. */
#define HAVE_STRTOUL 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <termios.h> header file. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if the system has the type `uint_t'. */
/* #undef HAVE_UINT_T */

/* Define to 1 if the system has the type `ulong_t'. */
/* #undef HAVE_ULONG_T */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <usb.h> header file. */
#define HAVE_USB_H 1

/* Define to 1 if you have the `usleep' function. */
#define HAVE_USLEEP 1

/* Define if lex/flex has yylex_destroy */
#define HAVE_YYLEX_DESTROY 1

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "avrdude"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "avrdude-dev@nongnu.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "avrdude"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "avrdude 6.3-20160220"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "avrdude"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "6.3-20160220"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Version number of package */
#define VERSION "6.3-20160220"

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */
