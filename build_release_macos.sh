#!/bin/sh
WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd $WD/deps
mkdir -p build
cd build
DEPS=$PWD/BambuStudio_dep
mkdir -p $DEPS
cmake ../ -DDESTDIR="$DEPS" -DOPENSSL_ARCH="darwin64-$(uname -m)-cc" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --target all 

cd $WD
mkdir -p build
cd build
cmake .. -GXcode -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="$DEPS/usr/local" -DCMAKE_INSTALL_PREFIX="$PWD/BambuStudio-SoftFever" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="$DEPS/usr/local" -DCMAKE_MACOSX_BUNDLE=ON
cmake --build . --config Release --target ALL_BUILD 
mkdir -p BambuStudio-SoftFever
cd BambuStudio-SoftFever
rm -r ./BambuStudio-SoftFever.app
cp -pR ../src/Release/BambuStudio.app ./BambuStudio-SoftFever.app
resources_path=$(readlink ./BambuStudio-SoftFever.app/Contents/Resources)
rm ./BambuStudio-SoftFever.app/Contents/Resources
cp -R $resources_path ./BambuStudio-SoftFever.app/Contents/Resources
# extract version
ver=$(grep '^#define SoftFever_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
ver="${ver//\"}"
zip -FSr BambuStudio-SoftFever_V${ver}_Mac_$(uname -m).zip BambuStudio-SoftFever.app

