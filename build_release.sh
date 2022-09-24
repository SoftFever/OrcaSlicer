#!/bin/sh

WD=$(pwd)
cd deps
mkdir build
cd build
DEPS=$PWD/BambuStudio_dep
cmake ../ -DDESTDIR="$DEPS" -DOPENSSL_ARCH="darwin64-arm64-cc"
# make -j

cd $WD
mkdir build
cd build
cmake .. -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="$DEPS/usr/local" -DCMAKE_INSTALL_PREFIX="$PWD/BambuStudio-SoftFever" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="$DEPS/usr/local" -DCMAKE_MACOSX_BUNDLE=on
cmake --build . --target install --config Release -j