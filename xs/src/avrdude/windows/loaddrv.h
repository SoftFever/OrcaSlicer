#ifndef LOADDRV_H
#define LOADDRV_H

#include <windows.h>

#define OKAY                   0
#define UNEXPECTED_ERROR       9999

//prototypes
DWORD LoadDriverInit(void);
void LoadDriverCleanup(void);
DWORD DriverInstall(LPSTR, LPSTR);
DWORD DriverStart(LPSTR);
DWORD DriverStop(LPSTR);
DWORD DriverRemove(LPSTR);
DWORD DriverStatus(LPSTR);
DWORD DriverStartType(LPSTR, DWORD);
#endif //LOADDRV_H


