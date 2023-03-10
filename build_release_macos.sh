#!/bin/sh

while getopts ":a:sdhn" opt; do
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
    n )
        export NIGHTLY_BUILD="1"
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64)"
        echo "   -s: Build studio only"
        echo "   -n: Nightly build"
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

if which -s brew; then
	brew --prefix libiconv
	brew --prefix zstd
	export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/
elif which -s port; then
	port install libiconv
	port install zstd
	export LIBRARY_PATH=$LIBRARY_PATH:/opt/local/lib
else
	echo "Need either brew or macports to successfully build deps"
	exit 1
fi

WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd $WD/deps
mkdir -p build_$ARCH
cd build_$ARCH
DEPS=$PWD/BambuStudio_dep_$ARCH
mkdir -p $DEPS
if [ "studio." != $BUILD_TARGET. ]; 
then
    echo "building deps..."
    echo "cmake ../ -DDESTDIR=$DEPS -DOPENSSL_ARCH=darwin64-${ARCH}-cc -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES:STRING=${ARCH}"
    cmake ../ -DDESTDIR="$DEPS" -DOPENSSL_ARCH="darwin64-${ARCH}-cc" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES:STRING=${ARCH}
    cmake --build . --config Release --target all 
fi


if [ "deps." == "$BUILD_TARGET". ];
then
    exit 0
fi

cd $WD
mkdir -p build_$ARCH
cd build_$ARCH
echo "building studio..."
cmake .. -GXcode -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="$DEPS/usr/local" -DCMAKE_INSTALL_PREFIX="$PWD/OrcaSlicer_$ARCH" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="$DEPS/usr/local" -DCMAKE_MACOSX_BUNDLE=ON -DCMAKE_OSX_ARCHITECTURES=${ARCH}
cmake --build . --config Release --target ALL_BUILD 
mkdir -p OrcaSlicer_$ARCH
cd OrcaSlicer_$ARCH
rm -r ./OrcaSlicer.app
cp -pR ../src/Release/OrcaSlicer.app ./OrcaSlicer.app
resources_path=$(readlink ./OrcaSlicer.app/Contents/Resources)
rm ./OrcaSlicer.app/Contents/Resources
cp -R $resources_path ./OrcaSlicer.app/Contents/Resources
# delete .DS_Store file
find ./OrcaSlicer.app/ -name '.DS_Store' -delete
# extract version
export ver="_dev"
echo $PWD
if [ "1." != "$NIGHTLY_BUILD". ];
then
    ver=$(grep '^#define SoftFever_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
    ver="_V${ver//\"}"
fi


zip -FSr OrcaSlicer${ver}_Mac_${ARCH}.zip OrcaSlicer.app
