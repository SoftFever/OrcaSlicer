
# Building Orca Slicer on Mac OS

## Enviroment setup
Install Following tools:  
- Xcode from app store  
- Cmake  
- git  
- gettext  

Cmake, git, gettext can be installed from brew(brew install cmake git gettext)

## building the deps
You need to build the dependence of OrcaSlicer first. (Only needs for the first time)  

Suppose you download the codes into /Users/_username_/work/projects/BambuStudio  
create a directory to store the dependence built: /Users/_username_/work/projects/OrcaSlicer_dep  
**(Please make sure to replace the username with the one on your computer)**  

`cd BambuStudio/deps`  
`mkdir build;cd build`  

for arm64 architecture  
`cmake ../ -DDESTDIR="/Users/username/work/projects/OrcaSlicer_dep" -DOPENSSL_ARCH="darwin64-arm64-cc"`  
for x86 architeccture  
`cmake ../ -DDESTDIR="/Users/username/work/projects/OrcaSlicer_dep" -DOPENSSL_ARCH="darwin64-x86_64-cc"`  
`make -jN`  (N can be a number between 1 and the max cpu number)  

## building the Orca Slicer
create a directory to store the installed files at /Users/username/work/projects/BambuStudio/install_dir  
`cd BambuStudio`  
`mkdir install_dir`  
`mkdir build;cd build`  

building it use cmake  
`cmake ..  -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="/Users/username/work/projects/OrcaSlicer_dep/usr/local" -DCMAKE_INSTALL_PREFIX="../install_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="/Users/username/work/projects/OrcaSlicer_dep/usr/local" -DCMAKE_MACOSX_BUNDLE=on`  
`cmake --build . --target install --config Release -jN`  

building it use xcode  
`cmake .. -GXcode -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="/Users/username/work/projects/OrcaSlicer_dep/usr/local" -DCMAKE_INSTALL_PREFIX="../install_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="/Users/username/work/projects/OrcaSlicer_dep/usr/local" -DCMAKE_MACOSX_BUNDLE=on`  
then building it using Xcode