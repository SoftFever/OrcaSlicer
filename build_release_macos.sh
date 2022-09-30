#!/bin/sh

WD=$(pwd)
cd deps
mkdir -p build
cd build
DEPS=$PWD/BambuStudio_dep
mkdir -p $DEPS
cmake ../ -DDESTDIR="$DEPS" -DOPENSSL_ARCH="darwin64-$(uname -m)-cc" -DCMAKE_BUILD_TYPE=Release 
make -j10

cd $WD
mkdir -p build
cd build
cmake .. -DBBL_RELEASE_TO_PUBLIC=0 -DCMAKE_PREFIX_PATH="$DEPS/usr/local" -DCMAKE_INSTALL_PREFIX="$PWD/BambuStudio-SoftFever" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="$DEPS/usr/local" -DCMAKE_MACOSX_BUNDLE=ON
make -j10
cmake --build . --target install --config Release -j10
