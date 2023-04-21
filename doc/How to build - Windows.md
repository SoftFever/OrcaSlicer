# Building Orca Slicer on Windows

## Enviroment setup
Install Following tools:
- Visual Studio Community 2019 from [visualstudio.microsoft.com/vs/](https://visualstudio.microsoft.com/vs/) (Older versions are not supported as Orca Slicer requires support for C++17, and newer versions should also be ok);
- Cmake from [cmake.org](https://cmake.org/download/)
- Git from [gitforwindows.org](https://gitforwindows.org/) 
- Perl from [strawberryperl](https://strawberryperl.com/)

## building the deps
Suppose you download the codes into D:/work/Projects/BambuStudio  
create a directory to store the dependence built: D:/work/Projects/OrcaSlicer_dep

`cd BambuStudio/deps`  
`mkdir build;cd build`  
`cmake ../ -G "Visual Studio 16 2019" -DDESTDIR="D:/work/Projects/OrcaSlicer_dep" -DCMAKE_BUILD_TYPE=Release`  
`msbuild /m ALL_BUILD.vcxproj`  

It takes "00:14:27.37" to finish it on my machine (11th Gen Intel(R) Core(TM) i9-11900 @2.50GHz   2.50 GHz, with 32.0 GB DDR)

## building the Orca Slicer
create a directory to store the installed files at D:/work/Projects/BambuStudio/install_dir  
`cd BambuStudio`  
`mkdir install_dir`  
`mkdir build;cd build`  

set -DWIN10SDK_PATH to your windows sdk path(for example: C:/Program Files (x86)/Windows Kits/10/Include/10.0.19041.0) in below command:  
`cmake .. -G "Visual Studio 16 2019" -DBBL_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="D:/work/Projects/OrcaSlicer_dep/usr/local" -DCMAKE_INSTALL_PREFIX="../install_dir" -DCMAKE_BUILD_TYPE=Release -DWIN10SDK_PATH="C:/Program Files (x86)/Windows Kits/10/Include/10.0.19041.0"` 

then build it using command  
`cmake --build . --target install --config Release`  

or building it under the Visual Studio 2019  
(set the OrcaSlicer_app_gui as start project)  
![image](https://user-images.githubusercontent.com/106916061/179185940-06135b47-f2a4-415a-9be4-666680fa0f9a.png)

