
[![Build Win64](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_win.yml/badge.svg)](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_win.yml)
[![Build Mac AppleSilicon](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_mac_arm64.yml/badge.svg)](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_mac_arm64.yml)
[![Build Mac Intel](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_mac_x64.yml/badge.svg)](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_mac_x64.yml)
[![Build Linux](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_linux.yml/badge.svg)](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_linux.yml)

# Orca Slicer     
Orca Slicer is an open source slicer for FDM printers.   
You can download Orca Slicer here: [github releases page](https://github.com/SoftFever/OrcaSlicer/releases/).  
![discord-mark-blue](https://github.com/SoftFever/OrcaSlicer/assets/103989404/b97d5ffc-072d-4d0a-bbda-e67ef373876f) Join community: [OrcaSlicer Official Discord Server](https://discord.gg/WTEkCR6SnV)   

# Main features
- Auto calibrations for all printers
- Sandwich(inner-outer-inner) mode - an improved version of the `External perimeters first` mode
- Precise wall
- Klipper support
- More granular controls
- More features can be found in [change notes](https://github.com/SoftFever/OrcaSlicer/releases/)  

### Some background
OrcaSlicer is fork of Bambu Studio  
It was previously known as BambuStudio-SoftFever  
Bambu Studio is forked from [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community. 
Orca Slicer incorporates a lot of features from SuperSlicer by @supermerill
Orca Slicer's logo is designed by community member Justin Levine(@freejstnalxndr)  

# How to install
**Windows**: 
1.  Unzip the binaries to any folder you prefer, then execute orca-slicer.exe to start the application.  
    - *If you have troubles to run the build, you might need to install following runtimes:*
      - [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/BambuStudio-SoftFever/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)  
      - [vcredist2019_x64](https://github.com/SoftFever/BambuStudio-SoftFever/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)  

**Mac**:
1. Download the right binaries for your computer: `arm64` version for Apple Silicon and `x86_64` for Intel CPU.  
2. Double click to unzip the package, move OrcaSlicer.app to Application folder.  
3. The app is signed but not notarized at the moment due to the proptiery network plugin.  
    We have two options to make it run on our machines  
  - Option 1 (You only need to do this once. After that the app can be oppened normally.):
    - Step 1: Hold _cmd_ and right click the app, from the context menu choose **Open**.
    - Step 2: A warning window will pop up, click _Open_  
    
  - Option 2:  
    Execute this command in terminal: `xattr -dr com.apple.quarantine /Applications/OrcaSlicer.app`
     ```console
        softfever@mac:~$ xattr -dr com.apple.quarantine /Applications/OrcaSlicer.app
    ```
  - Option 3:  
      - Step 1: open the app, a warning window will pop up  
          ![image](./SoftFever_doc/mac_cant_open.png)  
      - Step 2: in `System Settings` -> `Privacy & Security`, click `Open Anyway`:  
          ![image](./SoftFever_doc/mac_security_setting.png)  

**Linux(Ubuntu)**:
 1. If you run into trouble to execute it, try this command in terminal:  
    `chmod +x /path_to_appimage/OrcaSlicer_ubu64.AppImage`
# How to compile
- Windows 64-bit  
  - Tools needed: Visual Studio 2019, Cmake, git, Strawberry Perl.
  - Run `build_release.bat` in `x64 Native Tools Command Prompt for VS 2019`

- Mac 64-bit  
  - Tools needed: Xcode, Cmake, git, gettext
  - run `build_release_macos.sh`

- Ubuntu  
  - run `BuildLinux.sh -udisr`


# Note: 
If you're running Klipper, it's recommended to add the following configuration to your `printer.cfg` file.
```
# Enable object exclusion
[exclude_object]

# Enable arcs support
[gcode_arcs]
resolution: 0.1
```

# License
Orca Slicer is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Orca Slicer includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The bambu networking plugin is based on non-free libraries from Bambulab. It is optional to the Orca Slicer and provides extended functionalities for Bambulab printer users.

