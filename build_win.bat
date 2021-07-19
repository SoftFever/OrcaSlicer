@setlocal disableDelayedExpansion enableExtensions
@IF "%PS_ECHO_ON%" NEQ "" (echo on) ELSE (echo off)
@GOTO :MAIN
:HELP
@ECHO Performs initial build or rebuild of the app (build) and deps (build/deps).
@ECHO Default options are determined from build directories and system state.
@ECHO.
@ECHO Usage: build_win [-ARCH ^<arch^>] [-CONFIG ^<config^>] [-DESTDIR ^<directory^>]
@ECHO                  [-STEPS ^<all^|all-dirty^|app^|app-dirty^|deps^|deps-dirty^>]
@ECHO.
@ECHO  -a -ARCH        Target processor architecture
@ECHO                  Default: %PS_ARCH_HOST%
@ECHO  -c -CONFIG      MSVC project config
@ECHO                  Default: %PS_CONFIG_DEFAULT%
@ECHO  -s -STEPS       Performs only the specified build steps:
@ECHO                    all - clean and build deps and app
@ECHO                    all-dirty - build deps and app without cleaning
@ECHO                    app - build main project/application
@ECHO                    app-dirty - does not build main project/application
@ECHO                    deps - clean and build deps
@ECHO                    deps-dirty - build deps without cleaning
@ECHO                  Default: %PS_STEPS_DEFAULT%
@ECHO  -d -DESTDIR     Deps destination directory (ignored on dirty builds)
@ECHO                  %PS_DESTDIR_DEFAULT_MSG%
@ECHO.
@ECHO  Example usage:
@ECHO                  First build:  build_win -d "c:\src\PrusaSlicer-deps"
@ECHO                  Deps change:  build_win -s all
@ECHO                  App rebuild:  build_win
@ECHO.
GOTO :END

:MAIN
SET START_TIME=%TIME%
SET PS_START_DIR=%CD%
pushd %~dp0
REM Probe build directories and sytem state for reasonable default arguments
SET PS_CONFIG=RelWithDebInfo
SET PS_ARCH=%PROCESSOR_ARCHITECTURE%
CALL :TOLOWER PS_ARCH
SET PS_DEPS_PATH_FILE=%~dp0deps\build\.DEPS_PATH.txt
SET PS_DESTDIR=
IF EXIST %PS_DEPS_PATH_FILE% (
    FOR /F "tokens=* USEBACKQ" %%I IN ("%PS_DEPS_PATH_FILE%") DO SET PS_DESTDIR=%%I
    IF EXIST build/ALL_BUILD.vcxproj (
        SET PS_STEPS=app-dirty
    ) ELSE SET PS_STEPS=app
) ELSE SET PS_STEPS=all
SET PS_DESTDIR_CACHED=%PS_DESTDIR%

REM Set up parameters used by help menu
SET PS_CONFIG_DEFAULT=%PS_CONFIG%
SET PS_ARCH_HOST=%PS_ARCH%
SET PS_STEPS_DEFAULT=%PS_STEPS%
IF "%PS_DESTDIR%" NEQ "" (
    SET PS_DESTDIR_DEFAULT_MSG=Default: %PS_DESTDIR%
) ELSE (
    SET PS_DESTDIR_DEFAULT_MSG=Argument required ^(no default available^)
)

REM Parse arguments
SET EXIT_STATUS=1
SET PARSER_STATE=
SET PARSER_FAIL=
FOR %%I in (%*) DO CALL :PARSE_OPTION "ARCH CONFIG DESTDIR STEPS" PARSER_STATE "%%~I"
IF "%PARSER_FAIL%" NEQ "" (
    @ECHO ERROR: Invalid switch: %PARSER_FAIL% 1>&2
    GOTO :HELP
)ELSE IF "%PARSER_STATE%" NEQ "" (
    @ECHO ERROR: Missing parameter for: %PARSER_STATE% 1>&2
    GOTO :HELP
)

REM Validate arguments
SET PS_STEPS_SAVED=%PS_STEPS%
CALL :PARSE_OPTION_NAME "all all-dirty deps-dirty deps app-dirty app" PS_STEPS -%PS_STEPS%
IF "%PS_STEPS%" EQU "" (
    @ECHO ERROR: Invalid parameter: steps=%PS_STEPS_SAVED% 1>&2
    GOTO :HELP
) ELSE SET PS_STEPS_SAVED=
(echo %PS_STEPS%)| findstr /I /C:"dirty">nul && SET PS_STEPS_DIRTY=1
CALL :TOLOWER PS_STEPS
CALL :TOLOWER PS_ARCH
CALL :CANONICALIZE_PATH PS_DESTDIR "%PS_START_DIR%"
IF "%PS_DESTDIR%" EQU "" (
    IF "%PS_STEPS_DIRTY%" EQU "" (
        @ECHO ERROR: Parameter required: destdir 1>&2
        GOTO :HELP
    )
) ELSE IF "%PS_DESTDIR%" NEQ "%PS_DESTDIR_CACHED%" (
    IF "%PS_STEPS_DIRTY%" NEQ "" (
        @ECHO WARNING: Parameter ignored: destdir
    ) ELSE (echo "all deps")| findstr /I /C:"%PS_STEPS%">nul || (
        @ECHO WARNING: Conflict with cached parameter: 1>&2
        @ECHO WARNING:  -destdir=%PS_DESTDIR% 1>&2
        @ECHO WARNING:    cached=%PS_DESTDIR_CACHED% 1>&2
    )
)
SET PS_DESTDIR_DEFAULT_MSG=

REM Set up MSVC environment
SET EXIT_STATUS=2
@ECHO **********************************************************************
@ECHO ** Build Config: %PS_CONFIG%
@ECHO ** Target Arch:  %PS_ARCH%
@ECHO ** Build Steps:  %PS_STEPS%
@ECHO ** Deps path:    %PS_DESTDIR%
@ECHO ** Using Microsoft Visual Studio installation found at:
SET VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
IF NOT EXIST %VSWHERE% SET VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
FOR /F "tokens=* USEBACKQ" %%I IN (`%VSWHERE% -nologo -property installationPath`) DO SET MSVC_DIR=%%I
@ECHO **  %MSVC_DIR%
CALL "%MSVC_DIR%\Common7\Tools\vsdevcmd.bat" -arch=%PS_ARCH% -host_arch=%PS_ARCH_HOST% -app_platform=Desktop
IF "%ERRORLEVEL%" NEQ "0" GOTO :END
IF "%PS_DRY_RUN_ONLY%" NEQ "" (
    @ECHO Script terminated early because PS_DRY_RUN_ONLY is set. 1>&2
    GOTO :END
)
IF /I "%PS_STEPS:~0,3%" EQU "app" GOTO :BUILD_APP

REM Build deps
:BUILD_DEPS
SET EXIT_STATUS=3
IF "%PS_STEPS_DIRTY%" EQU "" CALL :MAKE_OR_CLEAN_DIRECTORY deps\build
cd deps\build || GOTO :END
IF "%PS_STEPS_DIRTY%" EQU "" cmake.exe .. -DDESTDIR="%PS_DESTDIR%" || GOTO :END
(echo %PS_DESTDIR%)> "%PS_DEPS_PATH_FILE%"
msbuild /m ALL_BUILD.vcxproj /p:Configuration=%PS_CONFIG% || GOTO :END
cd ..\..
IF /I "%PS_STEPS:~0,4%" EQU "deps" GOTO :PROLOGUE

REM Build app
:BUILD_APP
SET EXIT_STATUS=4
IF "%PS_STEPS_DIRTY%" EQU "" CALL :MAKE_OR_CLEAN_DIRECTORY build
cd build || GOTO :END
IF "%PS_STEPS_DIRTY%" EQU "" cmake.exe .. -DCMAKE_PREFIX_PATH="%PS_DESTDIR%\usr\local" || GOTO :END
msbuild /m ALL_BUILD.vcxproj /p:Configuration=%PS_CONFIG% || GOTO :END

:PROLOGUE
SET EXIT_STATUS=%ERRORLEVEL%
:END
@IF "%PS_ECHO_ON%%PS_DRY_RUN_ONLY%" NEQ "" (
    @ECHO Script Parameters:
    @SET PS_
)
@ECHO Script started at %START_TIME% and completed at %TIME%.
popd
exit /B %EXIT_STATUS%

GOTO :EOF
REM Functions and stubs start here.

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

:PARSE_OPTION_NAME
@REM Parses an option name
@REM %1 - Valid option list
@REM %2 - Out variable name; unset on error
@REM %3 - Current argument value
@REM $4 - Boolean indicating single character switches are valid
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
endlocal & SET %~2=%OPTION_NAME%
GOTO :EOF
:PARSE_OPTION_NAME_FAIL
endlocal & SET %~2=
GOTO :EOF

:MAKE_OR_CLEAN_DIRECTORY
@REM Create directory if it doesn't exist or clean it if it does
@REM %1 - Directory path to clean or create
setlocal disableDelayedExpansion
IF NOT EXIST "%~1" (
    ECHO Creating %~1
    mkdir "%~1" && GOTO :EOF
)
ECHO Cleaning %~1 ...
for /F "usebackq delims=" %%I in (`dir /a /b "%~1"`) do (
    (rmdir /s /q  "%~1\%%I" 2>nul ) || del /q /f "%~1\%%I")
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
if "%~3" NEQ "" (pushd %~3 || GOTO :EOF)
SET OUTPUT=%~f2
if "%~3" NEQ "" popd %~3
GOTO :EOF
