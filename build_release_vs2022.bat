@REM OcarSlicer build script for Windows
@echo off
set WP=%CD%

set debug=OFF
set debuginfo=OFF
set arch=x64
set buildoption=slicer

@REM Parse command line args
:parse
if not "%1"=="" (
    if "%1"=="debug" (
        set debug=ON
    )
    if "%1"=="debuginfo" (
        set debuginfo=ON
    )
    if "%1"=="-arch" (
        set arch="%2"
        shift
    )
    if "%1"=="-option" (
        set buildoption="%2"
        shift
    )
    shift
    GOTO :parse
)

if %buildoption%=="pack" (
    GOTO :pack
)

GOTO :build

:pack
setlocal ENABLEDELAYEDEXPANSION 
cd %WP%/deps/build
for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
set targetname=win64
if %arch%=="arm64" (
    set targetname=winarm64
)

echo packing deps: OrcaSlicer_dep_%targetname%_!build_date!_vs2022.zip

%WP%/tools/7z.exe a OrcaSlicer_dep_%targetname%_!build_date!_vs2022.zip OrcaSlicer_dep
exit /b 0

:build
if %debug%=="ON" (
    set build_type=Debug
    set build_dir=build-dbg
) else (
    if %debuginfo%=="ON" (
        set build_type=RelWithDebInfo
        set build_dir=build-dbginfo
    ) else (
        set build_type=Release
        set build_dir=build
    )
)
echo build type set to %build_type%

setlocal DISABLEDELAYEDEXPANSION 
cd deps
mkdir %build_dir%
cd %build_dir%
set DEPS=%CD%/OrcaSlicer_dep

echo %buildoption%

if %buildoption%=="deps" (
    GOTO :deps
)
if %buildoption%=="slicer" (
    GOTO :slicer
)
exit /b 1

:deps
echo "building deps.."

echo on
cmake ../ -G "Visual Studio 17 2022" -A %arch% -DDESTDIR="%DEPS%" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
cmake --build . --config %build_type% --target deps -- -m
@echo off

exit /b 0

:slicer
echo "building Orca Slicer..."
cd %WP%
mkdir %build_dir%
cd %build_dir%

echo on
cmake .. -G "Visual Studio 17 2022" -A %arch% -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type% -DWIN10SDK_PATH="%WindowsSdkDir%Include\%WindowsSDKVersion%\"
cmake --build . --config %build_type% --target ALL_BUILD -- -m
@echo off
cd ..
call run_gettext.bat
cd %build_dir%
cmake --build . --target install --config %build_type%
