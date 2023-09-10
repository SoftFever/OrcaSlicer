set WP=%CD%
cd deps
mkdir build
cd build
set DEPS=%CD%/OrcaSlicer_dep
if "%1"=="slicer" (
    GOTO :slicer
)
echo "building deps.."
cmake ../ -G "Visual Studio 16 2019" -DDESTDIR="%CD%/OrcaSlicer_dep" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --target deps -- -m

if "%1"=="deps" exit /b 0

:slicer
echo "building Orca Slicer..."
cd %WP%
mkdir build 
cd build

cmake .. -G "Visual Studio 16 2019" -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=Release -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.19041.0"
cmake --build . --config Release --target ALL_BUILD -- -m
cd ..
call run_gettext.bat
cd build
cmake --build . --target install --config Release