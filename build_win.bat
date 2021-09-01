@setlocal disableDelayedExpansion enableExtensions
@IF "%PS_ECHO_ON%" NEQ "" (echo on) ELSE (echo off)
@GOTO :MAIN
:HELP
@ECHO.
@ECHO Performs initial build or rebuild of the app (build) and deps (build/deps).
@ECHO Default options are determined from build directories and system state.
@ECHO.
@ECHO Usage: build_win [-ARCH ^<arch^>] [-CONFIG ^<config^>] [-DESTDIR ^<directory^>]
@ECHO                  [-STEPS ^<all^|all-dirty^|app^|app-dirty^|deps^|deps-dirty^>]
@ECHO                  [-RUN ^<console^|custom^|none^|viewer^|window^>]
@ECHO.
@ECHO  -a -ARCH      Target processor architecture
@ECHO                Default: %PS_ARCH_HOST%
@ECHO  -c -CONFIG    MSVC project config
@ECHO                Default: %PS_CONFIG_DEFAULT%
@ECHO  -s -STEPS     Performs only the specified build steps:
@ECHO                  all - clean and build deps and app
@ECHO                  all-dirty - build deps and app without cleaning
@ECHO                  app - clean and build main applications
@ECHO                  app-dirty - build main applications without cleaning
@ECHO                  deps - clean and build deps
@ECHO                  deps-dirty - build deps without cleaning
@ECHO                Default: %PS_STEPS_DEFAULT%
@ECHO  -r -RUN       Specifies what to perform at the run step:
@ECHO                  console - run and wait on prusa-slicer-console.exe
@ECHO                  custom - run and wait on your custom build/%PS_CUSTOM_RUN_FILE%
@ECHO                  ide - open project in Visual Studio if not open (no wait)
@ECHO                  none - run step does nothing
@ECHO                  viewer - run prusa-gcodeviewer.exe (no wait)
@ECHO                  window - run prusa-slicer.exe (no wait)
@ECHO                Default: none
@ECHO  -d -DESTDIR   Deps destination directory
@ECHO                Warning: Changing destdir path will not delete the old destdir.
@ECHO                Default: %PS_DESTDIR_DEFAULT_MSG%
@ECHO.
@ECHO  Examples:
@ECHO.
@ECHO  Initial build:           build_win -d "c:\src\PrusaSlicer-deps"
@ECHO  Build post deps change:  build_win -s all
@ECHO  App dirty build:         build_win
@ECHO  App dirty build ^& run:   build_win -r console
@ECHO  All clean build ^& run:   build_win -s all -r console -d "deps\build\out_deps"
@ECHO.
GOTO :END

:MAIN
REM Script constants
SET START_TIME=%TIME%
SET PS_START_DIR=%CD%
SET PS_SOLUTION_NAME=PrusaSlicer
SET PS_CHOICE_TIMEOUT=30
SET PS_CUSTOM_RUN_FILE=custom_run.bat
SET PS_DEPS_PATH_FILE_NAME=.DEPS_PATH.txt
SET PS_DEPS_PATH_FILE=%~dp0deps\build\%PS_DEPS_PATH_FILE_NAME%
SET PS_CONFIG_LIST="Debug;MinSizeRel;Release;RelWithDebInfo"

REM Probe build directories and system state for reasonable default arguments
pushd %~dp0
SET PS_CONFIG=RelWithDebInfo
SET PS_ARCH=%PROCESSOR_ARCHITECTURE:amd64=x64%
CALL :TOLOWER PS_ARCH
SET PS_RUN=none
SET PS_DESTDIR=
CALL :RESOLVE_DESTDIR_CACHE

REM Set up parameters used by help menu
SET EXIT_STATUS=0
SET PS_CONFIG_DEFAULT=%PS_CONFIG%
SET PS_ARCH_HOST=%PS_ARCH%
(echo " -help /help -h /h -? /? ")| findstr /I /C:" %~1 ">nul && GOTO :HELP

REM Parse arguments
SET EXIT_STATUS=1
SET PS_CURRENT_STEP=arguments
SET PARSER_STATE=
SET PARSER_FAIL=
FOR %%I in (%*) DO CALL :PARSE_OPTION "ARCH CONFIG DESTDIR STEPS RUN" PARSER_STATE "%%~I"
IF "%PARSER_FAIL%" NEQ "" (
    @ECHO ERROR: Invalid switch: %PARSER_FAIL% 1>&2
    GOTO :HELP
)ELSE IF "%PARSER_STATE%" NEQ "" (
    @ECHO ERROR: Missing parameter for: %PARSER_STATE% 1>&2
    GOTO :HELP
)

REM Validate arguments
SET PS_ASK_TO_CONTINUE=
CALL :TOLOWER PS_ARCH
SET PS_ARCH=%PS_ARCH:amd64=x64%
CALL :PARSE_OPTION_VALUE %PS_CONFIG_LIST:;= % PS_CONFIG
IF "%PS_CONFIG%" EQU "" GOTO :HELP
REM RESOLVE_DESTDIR_CACHE must go after PS_ARCH and PS_CONFIG, but before PS STEPS
CALL :RESOLVE_DESTDIR_CACHE
IF "%PS_STEPS%" EQU "" SET PS_STEPS=%PS_STEPS_DEFAULT%
CALL :PARSE_OPTION_VALUE "all all-dirty deps-dirty deps app-dirty app app-cmake" PS_STEPS
IF "%PS_STEPS%" EQU "" GOTO :HELP
(echo %PS_STEPS%)| findstr /I /C:"dirty">nul && SET PS_STEPS_DIRTY=1 || SET PS_STEPS_DIRTY=
IF "%PS_STEPS%" EQU "app-cmake" SET PS_STEPS_DIRTY=1
IF "%PS_DESTDIR%" EQU "" SET PS_DESTDIR=%PS_DESTDIR_CACHED%
IF "%PS_DESTDIR%" EQU "" (
    @ECHO ERROR: Parameter required: -DESTDIR 1>&2
    GOTO :HELP
)
CALL :CANONICALIZE_PATH PS_DESTDIR "%PS_START_DIR%"
IF "%PS_DESTDIR%" NEQ "%PS_DESTDIR_CACHED%" (
    (echo "all deps all-dirty deps-dirty")| findstr /I /C:"%PS_STEPS%">nul || (
        IF EXIST "%PS_DESTDIR%" (
            @ECHO WARNING: DESTDIR does not match cache: 1>&2
            @ECHO WARNING:  new: %PS_DESTDIR% 1>&2
            @ECHO WARNING:  old: %PS_DESTDIR_CACHED% 1>&2
            SET PS_ASK_TO_CONTINUE=1
        ) ELSE (
            @ECHO ERROR: Invalid parameter: DESTDIR=%PS_DESTDIR% 1>&2
            GOTO :HELP
        )
    )
)
SET PS_DESTDIR_DEFAULT_MSG=
CALL :PARSE_OPTION_VALUE "console custom ide none viewer window" PS_RUN
IF "%PS_RUN%" EQU "" GOTO :HELP
IF "%PS_RUN%" NEQ "none" IF "%PS_STEPS:~0,4%" EQU "deps" (
    @ECHO ERROR: RUN=%PS_RUN% specified with STEPS=%PS_STEPS%
    @ECHO ERROR: RUN=none is the only valid option for STEPS "deps" or "deps-dirty"
    GOTO :HELP
)
REM Give the user a chance to cancel if we found something odd.
IF "%PS_ASK_TO_CONTINUE%" EQU "" GOTO :BUILD_ENV
@ECHO.
@ECHO Unexpected parameters detected. Build paused for %PS_CHOICE_TIMEOUT% seconds.
choice /T %PS_CHOICE_TIMEOUT% /C YN /D N /M "Continue"
IF %ERRORLEVEL% NEQ 1 GOTO :HELP

REM Set up MSVC environment
:BUILD_ENV
SET EXIT_STATUS=2
SET PS_CURRENT_STEP=environment
@ECHO **********************************************************************
@ECHO ** Build Config: %PS_CONFIG%
@ECHO ** Target Arch:  %PS_ARCH%
@ECHO ** Build Steps:  %PS_STEPS%
@ECHO ** Run App:      %PS_RUN%
@ECHO ** Deps path:    %PS_DESTDIR%
@ECHO ** Using Microsoft Visual Studio installation found at:
SET VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
IF NOT EXIST "%VSWHERE%" SET VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe
FOR /F "tokens=* USEBACKQ" %%I IN (`"%VSWHERE%" -nologo -property installationPath`) DO SET MSVC_DIR=%%I
@ECHO **  %MSVC_DIR%
CALL "%MSVC_DIR%\Common7\Tools\vsdevcmd.bat" -arch=%PS_ARCH% -host_arch=%PS_ARCH_HOST% -app_platform=Desktop
IF %ERRORLEVEL% NEQ 0 GOTO :END
REM Need to reset the echo state after vsdevcmd.bat clobbers it.
@IF "%PS_ECHO_ON%" NEQ "" (echo on) ELSE (echo off)
IF "%PS_DRY_RUN_ONLY%" NEQ "" (
    @ECHO Script terminated early because PS_DRY_RUN_ONLY is set. 1>&2
    GOTO :END
)
IF /I "%PS_STEPS:~0,3%" EQU "app" GOTO :BUILD_APP

REM Build deps
:BUILD_DEPS
SET EXIT_STATUS=3
SET PS_CURRENT_STEP=deps
IF "%PS_STEPS_DIRTY%" EQU "" CALL :MAKE_OR_CLEAN_DIRECTORY deps\build "%PS_DEPS_PATH_FILE_NAME%" .vs
cd deps\build || GOTO :END
cmake.exe .. -DDESTDIR="%PS_DESTDIR%"
IF %ERRORLEVEL% NEQ 0 IF "%PS_STEPS_DIRTY%" NEQ "" (
    (del CMakeCache.txt && cmake.exe .. -DDESTDIR="%PS_DESTDIR%") || GOTO :END
) ELSE GOTO :END
(echo %PS_DESTDIR%)> "%PS_DEPS_PATH_FILE%"
msbuild /m ALL_BUILD.vcxproj /p:Configuration=%PS_CONFIG%  /v:quiet || GOTO :END
cd ..\..
IF /I "%PS_STEPS:~0,4%" EQU "deps" GOTO :RUN_APP

REM Build app
:BUILD_APP
SET EXIT_STATUS=4
SET PS_CURRENT_STEP=app
IF "%PS_STEPS_DIRTY%" EQU "" CALL :MAKE_OR_CLEAN_DIRECTORY build "%PS_CUSTOM_RUN_FILE%" .vs
cd build || GOTO :END
REM Make sure we have a custom batch file skeleton for the run stage
set PS_CUSTOM_BAT=%PS_CUSTOM_RUN_FILE%
CALL :CANONICALIZE_PATH PS_CUSTOM_BAT
IF NOT EXIST %PS_CUSTOM_BAT% CALL :WRITE_CUSTOM_SCRIPT_SKELETON %PS_CUSTOM_BAT%
SET PS_PROJECT_IS_OPEN=
FOR /F "tokens=2 delims=," %%I in (
    'tasklist /V /FI "IMAGENAME eq devenv.exe " /NH /FO CSV ^| find "%PS_SOLUTION_NAME%"'
) do SET PS_PROJECT_IS_OPEN=%%~I
cmake.exe .. -DCMAKE_PREFIX_PATH="%PS_DESTDIR%\usr\local" -DCMAKE_CONFIGURATION_TYPES=%PS_CONFIG_LIST%
IF %ERRORLEVEL% NEQ 0 IF "%PS_STEPS_DIRTY%" NEQ "" (
    (del CMakeCache.txt && cmake.exe .. -DCMAKE_PREFIX_PATH="%PS_DESTDIR%\usr\local" -DCMAKE_CONFIGURATION_TYPES=%PS_CONFIG_LIST%) || GOTO :END
) ELSE GOTO :END
REM Skip the build step if we're using the undocumented app-cmake to regenerate the full config from inside devenv
IF "%PS_STEPS%" NEQ "app-cmake" msbuild /m ALL_BUILD.vcxproj /p:Configuration=%PS_CONFIG% /v:quiet || GOTO :END
(echo %PS_DESTDIR%)> "%PS_DEPS_PATH_FILE_FOR_CONFIG%"

REM Run app
:RUN_APP
REM All build steps complete.
CALL :DIFF_TIME ELAPSED_TIME %START_TIME% %TIME%
IF "%PS_CURRENT_STEP%" NEQ "arguments" (
    @ECHO.
    @ECHO Total Build Time Elapsed %ELAPSED_TIME%
)
SET EXIT_STATUS=5
SET PS_CURRENT_STEP=run
cd src\%PS_CONFIG% || GOTO :END
IF "%PS_RUN%" EQU "none" GOTO :PROLOGUE
SET PS_PROJECT_IS_OPEN=
FOR /F "tokens=2 delims=," %%I in (
    'tasklist /V /FI "IMAGENAME eq devenv.exe " /NH /FO CSV ^| find "%PS_SOLUTION_NAME%"'
) do SET PS_PROJECT_IS_OPEN=%%~I
@ECHO.
@ECHO Running %PS_RUN% application...
@REM icacls below is just a hack for file-not-found error handling
IF "%PS_RUN%" EQU "console" (
    icacls prusa-slicer-console.exe >nul || GOTO :END
    start /wait /b prusa-slicer-console.exe
) ELSE IF "%PS_RUN%" EQU "window" (
    icacls prusa-slicer.exe >nul || GOTO :END
    start prusa-slicer.exe
) ELSE IF "%PS_RUN%" EQU "viewer" (
    icacls prusa-gcodeviewer.exe >nul || GOTO :END
    start prusa-gcodeviewer.exe
) ELSE IF "%PS_RUN%" EQU "custom" (
    icacls %PS_CUSTOM_BAT% >nul || GOTO :END
    CALL %PS_CUSTOM_BAT%
) ELSE IF "%PS_RUN%" EQU "ide" (
    IF "%PS_PROJECT_IS_OPEN%" NEQ "" (
        @ECHO WARNING: Solution is already open in Visual Studio. Skipping ide run step. 1>&2
    ) ELSE (
        @ECHO Preparing to run Visual Studio...
        cd ..\.. || GOTO :END
        REM This hack generates a single config for MSVS, guaranteeing it gets set as the active config.
        cmake.exe .. -DCMAKE_PREFIX_PATH="%PS_DESTDIR%\usr\local" -DCMAKE_CONFIGURATION_TYPES=%PS_CONFIG% > nul 2> nul || GOTO :END
        REM Now launch devenv with the single config (setting it active) and a /command switch to re-run cmake and generate the full config list
        start devenv.exe %PS_SOLUTION_NAME%.sln /command ^"shell /o ^^^"%~f0^^^" -d ^^^"%PS_DESTDIR%^^^" -c %PS_CONFIG% -a %PS_ARCH% -r none -s app-cmake^"
        REM If devenv fails to launch just directly regenerate the full config list.
        IF %ERRORLEVEL% NEQ 0 (
            cmake.exe .. -DCMAKE_PREFIX_PATH="%PS_DESTDIR%\usr\local" -DCMAKE_CONFIGURATION_TYPES=%PS_CONFIG_LIST% 2> nul 1> nul || GOTO :END
        )
    )
)

@REM **********    DON'T ADD ANY CODE BETWEEN THESE TWO SECTIONS    **********
@REM RUN_APP may hand off control, so let exit codes fall through to PROLOGUE.

:PROLOGUE
SET EXIT_STATUS=%ERRORLEVEL%
:END
@IF "%PS_ECHO_ON%%PS_DRY_RUN_ONLY%" NEQ "" (
    @ECHO **********************************************************************
    @ECHO ** Script Parameters:
    @ECHO **********************************************************************
    @SET PS_
)
IF "%EXIT_STATUS%" NEQ "0" (
    IF "%PS_CURRENT_STEP%" NEQ "arguments" (
        @ECHO.
        @ECHO ERROR: *** Build process failed at %PS_CURRENT_STEP% step. *** 1>&2
    )
) ELSE (
    @ECHO All steps completed successfully.
)
popd
exit /B %EXIT_STATUS%

GOTO :EOF
REM Functions and stubs start here.

:RESOLVE_DESTDIR_CACHE
@REM Resolves all DESTDIR cache values and sets PS_STEPS_DEFAULT
@REM Note: This just sets global variables, so it doesn't use setlocal.
SET PS_DEPS_PATH_FILE_FOR_CONFIG=%~dp0build\.vs\%PS_ARCH%\%PS_CONFIG%\%PS_DEPS_PATH_FILE_NAME%
mkdir "%~dp0build\.vs\%PS_ARCH%\%PS_CONFIG%" > nul 2> nul
REM Copy a legacy file if we don't have one in the proper location.
echo f|xcopy /D "%~dp0build\%PS_ARCH%\%PS_CONFIG%\%PS_DEPS_PATH_FILE_NAME%" "%PS_DEPS_PATH_FILE_FOR_CONFIG%"
CALL :CANONICALIZE_PATH PS_DEPS_PATH_FILE_FOR_CONFIG
IF EXIST "%PS_DEPS_PATH_FILE_FOR_CONFIG%" (
    FOR /F "tokens=* USEBACKQ" %%I IN ("%PS_DEPS_PATH_FILE_FOR_CONFIG%") DO (
        SET PS_DESTDIR_CACHED=%%I
        SET PS_DESTDIR_DEFAULT_MSG=%%I
    )
    SET PS_STEPS_DEFAULT=app-dirty
) ELSE IF EXIST "%PS_DEPS_PATH_FILE%" (
    FOR /F "tokens=* USEBACKQ" %%I IN ("%PS_DEPS_PATH_FILE%") DO (
        SET PS_DESTDIR_CACHED=%%I
        SET PS_DESTDIR_DEFAULT_MSG=%%I
    )
    SET PS_STEPS_DEFAULT=app
) ELSE (
    SET PS_DESTDIR_CACHED=
    SET PS_DESTDIR_DEFAULT_MSG=Cache missing. Argument required.
    SET PS_STEPS_DEFAULT=all
)
GOTO :EOF

:PARSE_OPTION
@REM Argument parser called for each argument
@REM %1 - Valid option list
@REM %2 - Variable name for parser state; must be unset when parsing finished
@REM %3 - Current argument value
@REM PARSER_FAIL will be set on an error
@REM Note: Must avoid delayed expansion since filenames may contain ! character
setlocal disableDelayedExpansion
IF "%PARSER_FAIL%" NEQ "" GOTO :EOF
CALL SET LAST_ARG=%%%2%%
IF "%LAST_ARG%" EQU "" (
    CALL :PARSE_OPTION_NAME %1 %~2 %~3 1
    SET ARG_TYPE=NAME
) ELSE (
    SET PS_SET_COMMAND=SET PS_%LAST_ARG%=%~3
    SET ARG_TYPE=LAST_ARG
    SET %~2=
)
CALL SET LAST_ARG=%%%2%%
IF "%LAST_ARG%%ARG_TYPE%" EQU "NAME" SET PARSER_FAIL=%~3
(
    endlocal
    SET PARSER_FAIL=%PARSER_FAIL%
    SET %~2=%LAST_ARG%
    %PS_SET_COMMAND%
)
GOTO :EOF

:PARSE_OPTION_VALUE
setlocal disableDelayedExpansion
@REM Parses value and verifies it is within the supplied list
@REM %1 - Valid option list
@REM %2 - In/out variable name; unset on error
CALL SET NAME=%~2
CALL SET SAVED_VALUE=%%%NAME%%%
CALL :PARSE_OPTION_NAME %1 %NAME% -%SAVED_VALUE%
CALL SET NEW_VALUE=%%%NAME%%%
IF "%NEW_VALUE%" EQU "" (
    @ECHO ERROR: Invalid parameter: %NAME:~3%=%SAVED_VALUE% 1>&2
)
endlocal & SET %NAME%=%NEW_VALUE%
GOTO :EOF

:PARSE_OPTION_NAME
@REM Parses an option name
@REM %1 - Valid option list
@REM %2 - Out variable name; unset on error
@REM %3 - Current argument value
@REM %4 - Boolean indicating single character switches are valid
@REM Note: Delayed expansion safe because ! character is invalid in option name
setlocal enableDelayedExpansion
IF "%4" NEQ "" FOR %%I IN (%~1) DO @(
    SET SHORT_NAME=%%~I
    SET SHORT_ARG_!SHORT_NAME:~0,1!=%%~I
)
@SET OPTION_NAME=%~3
@(echo %OPTION_NAME%)| findstr /R /C:"[-/]..*">nul || GOTO :PARSE_OPTION_NAME_FAIL
@SET OPTION_NAME=%OPTION_NAME:~1%
IF "%4" NEQ "" (
    IF "%OPTION_NAME%" EQU "%OPTION_NAME:~0,1%" (
        IF "!SHORT_ARG_%OPTION_NAME:~0,1%!" NEQ "" SET OPTION_NAME=!SHORT_ARG_%OPTION_NAME:~0,1%!
    )
)
@(echo %OPTION_NAME%)| findstr /R /C:".[ ][ ]*.">nul && GOTO :PARSE_OPTION_NAME_FAIL
@(echo  %~1 )| findstr /I /C:" %OPTION_NAME% ">nul || GOTO :PARSE_OPTION_NAME_FAIL
FOR %%I IN (%~1) DO SET OPTION_NAME=!OPTION_NAME:%%~I=%%~I!
endlocal & SET %~2=%OPTION_NAME%
GOTO :EOF
:PARSE_OPTION_NAME_FAIL
endlocal & SET %~2=
GOTO :EOF

:MAKE_OR_CLEAN_DIRECTORY
@REM Create directory if it doesn't exist or clean it if it does
@REM %1 - Directory path to clean or create
@REM %* - Optional list of files/dirs to keep (in the base directory only)
setlocal disableDelayedExpansion
IF NOT EXIST "%~1" (
    @ECHO Creating %~1
    mkdir "%~1" && (
        endlocal
        GOTO :EOF
    )
)
@ECHO Cleaning %~1 ...
SET KEEP_LIST=
:MAKE_OR_CLEAN_DIRECTORY_ARG_LOOP
IF "%~2" NEQ "" (
    SET KEEP_LIST=%KEEP_LIST% "%~2"
    SHIFT /2
    GOTO :MAKE_OR_CLEAN_DIRECTORY_ARG_LOOP
)
for /F "usebackq delims=" %%I in (`dir /a /b "%~1"`) do (
    (echo %KEEP_LIST%)| findstr /I /L /C:"\"%%I\"">nul || (
        rmdir /s /q  "%~1\%%I" 2>nul ) || del /q /f "%~1\%%I"
)
endlocal
GOTO :EOF

:TOLOWER
@REM Converts supplied environment variable to lowercase
@REM %1 - Input/output variable name
@REM Note: This is slow on very long strings, but is used only on very short ones
setlocal disableDelayedExpansion
@FOR %%b IN (a b c d e f g h i j k l m n o p q r s t u v w x y z) DO @CALL set %~1=%%%1:%%b=%%b%%
@CALL SET OUTPUT=%%%~1%%
endlocal & SET %~1=%OUTPUT%
GOTO :EOF

:CANONICALIZE_PATH
@REM Canonicalizes the path in the supplied variable
@REM %1 - Input/output variable containing path to canonicalize
@REM %2 - Optional base directory
setlocal
CALL :CANONICALIZE_PATH_INNER %1 %%%~1%% %2
endlocal & SET %~1=%OUTPUT%
GOTO :EOF
:CANONICALIZE_PATH_INNER
if "%~3" NEQ "" (pushd %3 || GOTO :EOF)
SET OUTPUT=%~f2
if "%~3" NEQ "" popd
GOTO :EOF

:DIFF_TIME
@REM Calculates elapsed time between two timestamps (TIME environment variable format)
@REM %1 - Output variable
@REM %2 - Start time
@REM %3 - End time
setlocal EnableDelayedExpansion
set START_ARG=%2
set END_ARG=%3
set END=!END_ARG:%TIME:~8,1%=%%100)*100+1!
set START=!START_ARG:%TIME:~8,1%=%%100)*100+1!
set /A DIFF=((((10!END:%TIME:~2,1%=%%100)*60+1!%%100)-((((10!START:%TIME:~2,1%=%%100)*60+1!%%100), DIFF-=(DIFF^>^>31)*24*60*60*100
set /A CC=DIFF%%100+100,DIFF/=100,SS=DIFF%%60+100,DIFF/=60,MM=DIFF%%60+100,HH=DIFF/60+100
@endlocal & set %1=%HH:~1%%TIME:~2,1%%MM:~1%%TIME:~2,1%%SS:~1%%TIME:~8,1%%CC:~1%
@GOTO :EOF

:WRITE_CUSTOM_SCRIPT_SKELETON
@REM Writes the following text to the supplied file
@REM %1 - Output filename
setlocal
@(
ECHO @ECHO.
ECHO @ECHO ********************************************************************************
ECHO @ECHO ** This is a custom run script skeleton.
ECHO @ECHO ********************************************************************************
ECHO @ECHO.
ECHO @ECHO ********************************************************************************
ECHO @ECHO ** The working directory is:
ECHO @ECHO ********************************************************************************
ECHO dir
ECHO @ECHO.
ECHO @ECHO ********************************************************************************
ECHO @ECHO ** The environment is:
ECHO @ECHO ********************************************************************************
ECHO set
ECHO @ECHO.
ECHO @ECHO ********************************************************************************
ECHO @ECHO ** Edit or replace this script to run custom steps after a successful build:
ECHO @ECHO ** %~1
ECHO @ECHO ********************************************************************************
ECHO @ECHO.
) > "%~1"
endlocal
GOTO :EOF
