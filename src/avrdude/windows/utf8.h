#ifndef SLIC3R_AVRDUDE_UTF8_H
#define SLIC3R_AVRDUDE_UTF8_H

#include <windows.h>

extern char* wstr_to_utf8(LPWSTR wstr, int len);
extern LPWSTR utf8_to_wstr(const char *str, int len);


#endif  // SLIC3R_AVRDUDE_UTF8_H
