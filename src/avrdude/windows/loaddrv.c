// loaddrv.c - Dynamic driver install/start/stop/remove
// based on Paula Tomlinson's LOADDRV program. 
// She describes it in her May 1995 article in Windows/DOS Developer's
// Journal (now Windows Developer's Journal).
// Modified by Chris Liechti <cliechti@gmx.net>
// I removed the old/ugly dialog, it now accepts command line options and
// prints error messages with textual description from the OS.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "loaddrv.h"

// globals
SC_HANDLE hSCMan = NULL;

//get ext messages for windows error codes:
void DisplayErrorText(DWORD dwLastError) {
    LPSTR MessageBuffer;
    DWORD dwBufferLength;
    
    DWORD dwFormatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_FROM_SYSTEM;
    
    dwBufferLength = FormatMessageA(
        dwFormatFlags,
        NULL, // module to get message from (NULL == system)
        dwLastError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
        (LPSTR) &MessageBuffer,
        0,
        NULL
    );
    if (dwBufferLength) {
        // Output message
        puts(MessageBuffer);
        // Free the buffer allocated by the system.
        LocalFree(MessageBuffer);
    }
}

int exists(char *filename) {
    FILE * pFile;
    pFile = fopen(filename, "r");
    return pFile != NULL;
}

void usage(void) {
    printf("USGAE: loaddrv command drivername [args...]\n\n"
           "NT/2k/XP Driver and Service modification tool.\n"
           "(C)2002 Chris Liechti <cliechti@gmx.net>\n\n"
           "Suported commands:\n\n"
           "  install [fullpathforinstall]\n"
           "      Install new service. Loaded from given path. If path is not present,\n"
           "      the local directory is searched for a .sys file. If the service\n"
           "      already exists, it must be removed first.\n"
           "  start\n"
           "      Start service. It must be installed in advance.\n"
           "  stop\n"
           "      Stop service.\n"
           "  remove\n"
           "      Remove service. It must be stopped in advance.\n"
           "  status\n"
           "      Show status information about service.\n"
           "  starttype auto|manual|system|disable\n"
           "      Change startup type to the given type.\n"
    );
}

int main(int argc, char *argv[]) {
    DWORD status = 0;
    int level = 0;
    if (argc < 3) {
        usage();
        exit(1);
    }
    LoadDriverInit();
    if (strcmp(argv[1], "start") == 0) {
        printf("starting %s... ", argv[2]);
        status = DriverStart(argv[2]);
        if ( status != OKAY) {
            printf("start failed (status %ld):\n", status);
            level = 1;
        } else {
            printf("ok.\n");
        }
    } else if (strcmp(argv[1], "stop") == 0) {
        printf("stoping %s... ", argv[2]);
        status = DriverStop(argv[2]);
        if ( status != OKAY) {
            printf("stop failed (status %ld):\n", status);
            level = 1;
        } else {
            printf("ok.\n");
        }
    } else if (strcmp(argv[1], "install") == 0) {
        char path[MAX_PATH*2];
        if (argc<4) {
            char cwd[MAX_PATH];
            getcwd(cwd, sizeof cwd);
            sprintf(path, "%s\\%s.sys", cwd, argv[2]);
        } else {
            strncpy(path, argv[3], MAX_PATH);
        }
        if (exists(path)) {
            printf("installing %s from %s... ", argv[2], path);
            status = DriverInstall(path, argv[2]);
            if ( status != OKAY) {
                printf("install failed (status %ld):\n", status);
                level = 2;
            } else {
                printf("ok.\n");
            }
        } else {
            printf("install failed, file not found: %s\n", path);
            level = 1;
        }
    } else if (strcmp(argv[1], "remove") == 0) {
        printf("removing %s... ", argv[2]);
        status = DriverRemove(argv[2]);
        if ( status != OKAY) {
            printf("remove failed (status %ld):\n", status);
            level = 1;
        } else {
            printf("ok.\n");
        }
    } else if (strcmp(argv[1], "status") == 0) {
        printf("status of %s:\n", argv[2]);
        status = DriverStatus(argv[2]);
        if ( status != OKAY) {
            printf("stat failed (status %ld):\n", status);
            level = 1;
        } else {
            printf("ok.\n");
        }
    } else if (strcmp(argv[1], "starttype") == 0) {
        if (argc < 4) {
            printf("Error: need start type (string) as argument.\n");
            level = 2;
        } else {
            DWORD type = 0;
            printf("set start type of %s to %s... ", argv[2], argv[3]);
            if (strcmp(argv[1], "boot") == 0) {
                type = SERVICE_BOOT_START;
            } else if (strcmp(argv[3], "system") == 0) {
                type = SERVICE_SYSTEM_START;
            } else if (strcmp(argv[3], "auto") == 0) {
                type = SERVICE_AUTO_START;
            } else if (strcmp(argv[3], "manual") == 0) {
                type = SERVICE_DEMAND_START;
            } else if (strcmp(argv[3], "disabled") == 0) {
                type = SERVICE_DISABLED;
            } else {
                printf("unknown type\n");
                level = 1;
            }
            if (level == 0) {
                status = DriverStartType(argv[2], type);
                if ( status != OKAY) {
                    printf("set start type failed (status %ld):\n", status);
                    level = 1;
                } else {
                    printf("ok.\n");
                }
            }
        }
    } else {
        usage();
        level = 1;
    }
    if (status) DisplayErrorText(status);
    LoadDriverCleanup();
    exit(level);
    return 0;
}


DWORD LoadDriverInit(void) {
    // connect to local service control manager
    if ((hSCMan = OpenSCManager(NULL, NULL, 
        SC_MANAGER_ALL_ACCESS)) == NULL) {
        return -1;
    }
    return OKAY;
}

void LoadDriverCleanup(void) {
    if (hSCMan != NULL) CloseServiceHandle(hSCMan);
}

/**-----------------------------------------------------**/
DWORD DriverInstall(LPSTR lpPath, LPSTR lpDriver) {
   BOOL dwStatus = OKAY;
   SC_HANDLE hService = NULL;

   // add to service control manager's database
   if ((hService = CreateService(hSCMan, lpDriver, 
      lpDriver, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
      SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, lpPath, 
      NULL, NULL, NULL, NULL, NULL)) == NULL)
         dwStatus = GetLastError();
   else CloseServiceHandle(hService);

   return dwStatus;
} // DriverInstall

/**-----------------------------------------------------**/
DWORD DriverStart(LPSTR lpDriver) {
   BOOL dwStatus = OKAY;
   SC_HANDLE hService = NULL;

   // get a handle to the service
   if ((hService = OpenService(hSCMan, lpDriver, 
      SERVICE_ALL_ACCESS)) != NULL) 
   {
      // start the driver
      if (!StartService(hService, 0, NULL))
         dwStatus = GetLastError();
   } else dwStatus = GetLastError();

   if (hService != NULL) CloseServiceHandle(hService);
   return dwStatus;
} // DriverStart

/**-----------------------------------------------------**/
DWORD DriverStop(LPSTR lpDriver)
{
   BOOL dwStatus = OKAY;
   SC_HANDLE hService = NULL;
   SERVICE_STATUS serviceStatus;

   // get a handle to the service
   if ((hService = OpenService(hSCMan, lpDriver, 
      SERVICE_ALL_ACCESS)) != NULL) 
   {
      // stop the driver
      if (!ControlService(hService, SERVICE_CONTROL_STOP,
         &serviceStatus))
            dwStatus = GetLastError();
   } else dwStatus = GetLastError();

   if (hService != NULL) CloseServiceHandle(hService);
   return dwStatus;
} // DriverStop

/**-----------------------------------------------------**/
DWORD DriverRemove(LPSTR lpDriver)
{
   BOOL dwStatus = OKAY;
   SC_HANDLE hService = NULL;

   // get a handle to the service
   if ((hService = OpenService(hSCMan, lpDriver, 
      SERVICE_ALL_ACCESS)) != NULL) 
   {  // remove the driver
      if (!DeleteService(hService))
         dwStatus = GetLastError();
   } else dwStatus = GetLastError();

   if (hService != NULL) CloseServiceHandle(hService);
   return dwStatus;
} // DriverRemove

/**-----------------------------------------------------**/
////extensions by Lch
/**-----------------------------------------------------**/
DWORD DriverStatus(LPSTR lpDriver) {
    BOOL dwStatus = OKAY;
    SC_HANDLE hService = NULL;
    DWORD dwBytesNeeded;

    // get a handle to the service
    if ((hService = OpenService(hSCMan, lpDriver, 
                                SERVICE_ALL_ACCESS)) != NULL) 
    {
        LPQUERY_SERVICE_CONFIG lpqscBuf;
        //~ LPSERVICE_DESCRIPTION lpqscBuf2;
        // Allocate a buffer for the configuration information. 
        if ((lpqscBuf = (LPQUERY_SERVICE_CONFIG) LocalAlloc( 
            LPTR, 4096)) != NULL)
        {
            //~ if ((lpqscBuf2 = (LPSERVICE_DESCRIPTION) LocalAlloc( 
                //~ LPTR, 4096)) != NULL)
            {
                // Get the configuration information. 
                if (QueryServiceConfig(
                        hService,
                        lpqscBuf,
                        4096,
                        &dwBytesNeeded) //&&
                    //~ QueryServiceConfig2( 
                        //~ hService,
                        //~ SERVICE_CONFIG_DESCRIPTION,
                        //~ lpqscBuf2,
                        //~ 4096, 
                        //~ &dwBytesNeeded
                )
                {
                    // Print the configuration information. 
                    printf("Type:           [0x%02lx] ", lpqscBuf->dwServiceType);
                    switch (lpqscBuf->dwServiceType) {
                        case SERVICE_WIN32_OWN_PROCESS:
                            printf("The service runs in its own process.");
                            break;
                        case SERVICE_WIN32_SHARE_PROCESS:
                            printf("The service shares a process with other services.");
                            break;
                        case SERVICE_KERNEL_DRIVER:
                            printf("Kernel driver.");
                            break;
                        case SERVICE_FILE_SYSTEM_DRIVER:
                            printf("File system driver.");
                            break;
                        case SERVICE_INTERACTIVE_PROCESS:
                            printf("The service can interact with the desktop.");
                            break;
                        default:
                            printf("Unknown type.");
                    }
                    printf("\nStart Type:     [0x%02lx] ", lpqscBuf->dwStartType); 
                    switch (lpqscBuf->dwStartType) {
                        case SERVICE_BOOT_START:
                            printf("Boot");
                            break;
                        case SERVICE_SYSTEM_START:
                            printf("System");
                            break;
                        case SERVICE_AUTO_START:
                            printf("Automatic");
                            break;
                        case SERVICE_DEMAND_START:
                            printf("Manual");
                            break;
                        case SERVICE_DISABLED:
                            printf("Disabled");
                            break;
                        default:
                            printf("Unknown.");
                    }
                    printf("\nError Control:  [0x%02lx] ", lpqscBuf->dwErrorControl); 
                    switch (lpqscBuf->dwErrorControl) {
                        case SERVICE_ERROR_IGNORE:
                            printf("IGNORE: Ignore.");
                            break;
                        case SERVICE_ERROR_NORMAL:
                            printf("NORMAL: Display a message box.");
                            break;
                        case SERVICE_ERROR_SEVERE:
                            printf("SEVERE: Restart with last-known-good config.");
                            break;
                        case SERVICE_ERROR_CRITICAL:
                            printf("CRITICAL: Restart w/ last-known-good config.");
                            break;
                        default:
                            printf("Unknown.");
                    }
                    printf("\nBinary path:    %s\n", lpqscBuf->lpBinaryPathName); 
                    
                    if (lpqscBuf->lpLoadOrderGroup != NULL) 
                        printf("Load order grp: %s\n", lpqscBuf->lpLoadOrderGroup); 
                    if (lpqscBuf->dwTagId != 0) 
                        printf("Tag ID:         %ld\n", lpqscBuf->dwTagId); 
                    if (lpqscBuf->lpDependencies != NULL) 
                        printf("Dependencies:   %s\n", lpqscBuf->lpDependencies); 
                    if (lpqscBuf->lpServiceStartName != NULL) 
                        printf("Start Name:     %s\n", lpqscBuf->lpServiceStartName); 
                    //~ if (lpqscBuf2->lpDescription != NULL) 
                        //~ printf("Description:    %s\n", lpqscBuf2->lpDescription); 
                }
                //~ LocalFree(lpqscBuf2);
            }
            LocalFree(lpqscBuf);
        } else {
            dwStatus = GetLastError();
        }
    } else {
        dwStatus = GetLastError();
    }

   if (hService != NULL) CloseServiceHandle(hService);
   return dwStatus;
} // DriverStatus

/**-----------------------------------------------------**/
DWORD DriverStartType(LPSTR lpDriver, DWORD dwStartType) {
    BOOL dwStatus = OKAY;
    SC_HANDLE hService = NULL;

    SC_LOCK sclLock;
    LPQUERY_SERVICE_LOCK_STATUS lpqslsBuf;
    DWORD dwBytesNeeded;
    
    // Need to acquire database lock before reconfiguring.
    sclLock = LockServiceDatabase(hSCMan);

    // If the database cannot be locked, report the details.
    if (sclLock == NULL) {
        // Exit if the database is not locked by another process.
        if (GetLastError() == ERROR_SERVICE_DATABASE_LOCKED) {
            
            // Allocate a buffer to get details about the lock.
            lpqslsBuf = (LPQUERY_SERVICE_LOCK_STATUS) LocalAlloc(
                LPTR, sizeof(QUERY_SERVICE_LOCK_STATUS)+256);
            if (lpqslsBuf != NULL) {
                // Get and print the lock status information.
                if (QueryServiceLockStatus(
                    hSCMan,
                    lpqslsBuf,
                    sizeof(QUERY_SERVICE_LOCK_STATUS)+256,
                    &dwBytesNeeded) )
                {
                    if (lpqslsBuf->fIsLocked) {
                        printf("Locked by: %s, duration: %ld seconds\n",
                            lpqslsBuf->lpLockOwner,
                            lpqslsBuf->dwLockDuration
                        );
                    } else {
                        printf("No longer locked\n");
                    }
                }
                LocalFree(lpqslsBuf);
            }
        }
        dwStatus = GetLastError();
    } else {
        // The database is locked, so it is safe to make changes.
        // Open a handle to the service.
        hService = OpenService(
            hSCMan,                 // SCManager database
            lpDriver,               // name of service
            SERVICE_CHANGE_CONFIG
        ); // need CHANGE access
        if (hService != NULL) {
            // Make the changes.
            if (!ChangeServiceConfig(
                hService,          // handle of service
                SERVICE_NO_CHANGE, // service type: no change
                dwStartType,       // change service start type
                SERVICE_NO_CHANGE, // error control: no change
                NULL,              // binary path: no change
                NULL,              // load order group: no change
                NULL,              // tag ID: no change
                NULL,              // dependencies: no change
                NULL,              // account name: no change
                NULL,              // password: no change
                NULL) )            // display name: no change
            {
                dwStatus = GetLastError();
            }
        }
        // Release the database lock.
        UnlockServiceDatabase(sclLock);
    }

    if (hService != NULL) CloseServiceHandle(hService);
    return dwStatus;
} // DriverStartType
