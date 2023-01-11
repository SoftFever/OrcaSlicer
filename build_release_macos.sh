#!/bin/sh

while getopts ":a:sdh" opt; do
  case ${opt} in
    d )
        export BUILD_TARGET="deps"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="studio"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only (optional)"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64)"
        echo "   -s: Build studio only (optional)"
        exit 0
        ;;
  esac
done

if [ -z "$ARCH" ]
then
  export ARCH=$(uname -m)
fi

echo "Arch: $ARCH"
echo "BUILD_TARGET: $BUILD_TARGET"

brew --prefix libiconv
brew --prefix zstd
export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/

WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd $WD/deps
mkdir -p build
cd build
DEPS=$PWD/BambuStudio_dep
mkdir -p $DEPS
if [ "studio." != $BUILD_TARGET. ]; 
then
    echo "building deps..."
    echo "cmake ../ -DDESTDIR=$DEPS -DOPENSSL_ARCH=darwin64-${ARCH}-cc -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES:STRING=${ARCH}"
    cmake ../ -DDESTDIR="$DEPS" -DOPENSSL_ARCH="darwin64-${ARCH}-cc" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES:STRING=${ARCH}
    cmake --build . --config Release --BUILD_TARGET all 
fi


if [ "deps." == "$BUILD_TARGET". ];
then
    exit 0
fi

cd $WD
mkdir -p build
cd build
echo "building studio..."
cmake .. -GXcode -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="$DEPS/usr/local" -DCMAKE_INSTALL_PREFIX="$PWD/BambuStudio-SoftFever" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="$DEPS/usr/local" -DCMAKE_MACOSX_BUNDLE=ON -DCMAKE_OSX_ARCHITECTURES=${ARCH}
cmake --build . --config Release --BUILD_TARGET ALL_BUILD 
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
zip -FSr BambuStudio-SoftFever_V${ver}_Mac_${ARCH}.zip BambuStudio-SoftFever.app

