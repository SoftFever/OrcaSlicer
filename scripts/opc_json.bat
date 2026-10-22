@echo off
rem Build and run the opc_json dev tool: decode a per-vendor <vendor>.opc preset
rem cache to editable JSON, or rebuild the cache from an edited JSON file.
rem
rem   scripts\opc_json.bat decode resources\profiles\BambuLab.opc -o %TEMP%\BambuLab.json
rem   rem edit %TEMP%\BambuLab.json
rem   scripts\opc_json.bat encode %TEMP%\BambuLab.json -o %TEMP%\BambuLab.opc
rem
rem   scripts\opc_json.bat --help
rem
rem The build tree defaults to "build" (configure it with -DORCA_TOOLS=ON).
rem Override with:
rem   set ORCA_BUILD_DIR=<dir>   build tree holding the tool
rem   set ORCA_CONFIG=<cfg>      build config for multi-config generators
rem   set ORCA_NO_BUILD=1        run the tool already in the tree, without rebuilding
setlocal enabledelayedexpansion

set "REPO_ROOT=%~dp0.."
if not defined ORCA_BUILD_DIR set "ORCA_BUILD_DIR=build"
if not exist "%ORCA_BUILD_DIR%\" (
    echo ERROR: build tree not found: %ORCA_BUILD_DIR% 1>&2
    exit /b 1
)

call :find_tool
if not defined ORCA_CONFIG (
    for %%c in (Debug Release RelWithDebInfo MinSizeRel) do (
        echo !TOOL! | findstr /i "\\%%c\\" >nul && set "ORCA_CONFIG=%%c"
    )
)
if not defined ORCA_CONFIG set "ORCA_CONFIG=Release"

if not "%ORCA_NO_BUILD%"=="1" (
    cmake --build "%ORCA_BUILD_DIR%" --target opc_json --config %ORCA_CONFIG%
    if errorlevel 1 (
        echo ERROR: could not build opc_json - configure the build tree with -DORCA_TOOLS=ON: 1>&2
        echo        cmake -S "%REPO_ROOT%" -B "%ORCA_BUILD_DIR%" -DORCA_TOOLS=ON 1>&2
        exit /b 1
    )
    call :find_tool
)

if not defined TOOL (
    echo ERROR: opc_json.exe not found under %ORCA_BUILD_DIR% - build with -DORCA_TOOLS=ON 1>&2
    exit /b 1
)
"%TOOL%" %*
exit /b %errorlevel%

:find_tool
set "TOOL="
for /f "delims=" %%f in ('dir /s /b /o-d "%ORCA_BUILD_DIR%\opc_json.exe" 2^>nul') do (
    if not defined TOOL set "TOOL=%%f"
)
exit /b 0
