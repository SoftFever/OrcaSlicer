@REM OcarSlicer build script for Windows
@echo off
set WP=%CD%

set build_type=Release
set build_dir=build
set generator="Visual Studio 17 2022"
:GETOPTS
 if /I "%1" == "deps" set deps=ON
 if /I "%1" == "slicer" set slicer=ON
 if /I "%1" == "debug" (
    set debug=ON
    set build_type=Debug
    set build_dir=build-dbg
 )
 if /I "%1" == "debuginfo" (
    set debuginfo=ON
    set build_type=RelWithDebInfo
    set build_dir=build-dbginfo
 )
 if /I "%1" == "pack" set pack=ON
 if /I "%1" == "vs2019" set generator="Visual Studio 16 2019"
 shift
if not "%1" == "" goto GETOPTS

@REM Pack deps
if "%pack%"=="ON" (
    setlocal ENABLEDELAYEDEXPANSION 
    cd %WP%/deps/build
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
    echo packing deps: OrcaSlicer_dep_win64_!build_date!_vs2022.zip

    %WP%/tools/7z.exe a OrcaSlicer_dep_win64_!build_date!_vs2022.zip OrcaSlicer_dep
    exit /b 0
)

echo Build type set to %build_type%

setlocal DISABLEDELAYEDEXPANSION

if "%slicer%"=="ON" (
    GOTO :slicer
)
echo "Building Orca Slicer deps..."


echo cmake . -B deps/%build_dir% -G %generator% -A x64 -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
cmake . -B deps/%build_dir% -G %generator% -A x64 -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
if %errorlevel% neq 0 exit /b %errorlevel%

echo cmake --build deps/%build_dir% . --config %build_type% --target deps -- -m
cmake --build deps/%build_dir% . --config %build_type% --target deps -- -m
if %errorlevel% neq 0 exit /b %errorlevel%


if not "%slicer%"=="ON" if "%deps%"=="ON" exit /b 0

:slicer
echo Building Orca Slicer...


echo cmake . -B %build_dir% -G %generator% -A x64 -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type%
cmake . -B %build_dir% -G %generator% -A x64 -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type%
if %errorlevel% neq 0 exit /b %errorlevel%

echo cmake --build %build_dir% --config %build_type% --target ALL_BUILD -- -m
if %errorlevel% neq 0 exit /b %errorlevel%

call run_gettext.bat
echo cmake --build %build_dir% --target install --config %build_type%
cmake --build %build_dir% --target install --config %build_type%
if %errorlevel% neq 0 exit /b %errorlevel%
