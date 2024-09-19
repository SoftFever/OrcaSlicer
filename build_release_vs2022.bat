@REM OrcaSlicer build script for Windows
@echo off
set WP=%CD%

set build_type=Release
set build_dir=build
set debug=OFF
set debuginfo=OFF
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
 if /I "%1" == "killbuild" (
    taskkill /F /IM cl.exe
    taskkill /F /IM MSBuild.exe
    exit /b 0
 )
 shift
if not "%1" == "" goto GETOPTS

if "%deps%"=="ON" if "%slicer%"=="ON" (
    set deps=OFF
    set slicer=OFF
)

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

set command=cmake -S deps -B deps/%build_dir% -G %generator% -A x64 -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
echo Configuring deps with the following command: %command%
%command%
if %errorlevel% neq 0 exit /b %errorlevel%

set command=cmake --build deps/%build_dir% --config %build_type% --target deps -- -m
echo Building deps with the following command: %command%
%command%
if %errorlevel% neq 0 exit /b %errorlevel%


if "%deps%"=="ON" exit /b 0


:slicer
echo Building Orca Slicer...

set command=cmake . -B %build_dir% -G %generator% -A x64 -DCMAKE_BUILD_TYPE=%build_type%
echo Configuring Orca Slicer with the following command: %command%
%command%
if %errorlevel% neq 0 exit /b %errorlevel%

set command=cmake --build %build_dir% --config %build_type% --target ALL_BUILD -- -m
echo Building Orca Slicer with the following command: %command%
%command%
if %errorlevel% neq 0 exit /b %errorlevel%

echo Calling run_gettext.bat
call run_gettext.bat

set command=cmake --build %build_dir% --target install --config %build_type%
echo Installing Orca Slicer with the following command: %command%
%command%
if %errorlevel% neq 0 exit /b %errorlevel%
