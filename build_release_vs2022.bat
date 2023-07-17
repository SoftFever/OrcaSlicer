set WP=%CD%
cd deps
mkdir build
cd build
set DEPS=%CD%/OrcaSlicer_dep
if "%1"=="studio" (
    GOTO :studio
)
echo "building deps.."
cmake ../ -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%CD%/OrcaSlicer_dep" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --target ALL_BUILD -- -m

if "%1"=="deps" exit /b 0

:studio
echo "building studio..."
cd %WP%
mkdir build 
cd build

cmake .. -G "Visual Studio 17 2022" -A x64 -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --target ALL_BUILD -- -m
cmake --build . --target install --config Release