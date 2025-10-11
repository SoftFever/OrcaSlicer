#ifndef __SLIC3R_VERSION_H
#define __SLIC3R_VERSION_H

#define SLIC3R_APP_NAME "OrcaSlicer"
#define SLIC3R_APP_KEY "OrcaSlicer"
#define SLIC3R_VERSION "01.10.01.50"
#define SoftFever_VERSION "2.3.2-dev"
#ifndef GIT_COMMIT_HASH
    #define GIT_COMMIT_HASH "0000000" // 0000000 means uninitialized
#endif
#define SLIC3R_BUILD_ID ""
//#define SLIC3R_RC_VERSION "01.10.01.50"
#define BBL_RELEASE_TO_PUBLIC 1
#define BBL_INTERNAL_TESTING 0
#define ORCA_CHECK_GCODE_PLACEHOLDERS 0

#endif /* __SLIC3R_VERSION_H */
