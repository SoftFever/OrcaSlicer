
# Orca Slicer
Orca Slicer is a fork of Bambu Studio.  
Bambu Studio is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community. 
Orca Slicer incorporates a lot of features from SuperSlicer by @supermerill


Prebuilt binaries are available through the [github releases page](https://github.com/SoftFever/OrcaSlicer/releases/).  


# Main features
- Auto calibrations for all printers
- Sandwich(inner-outer-inner) mode - an improved version of the `External perimeters first` mode
- Precise wall
- Klipper support
- More granular controls
- More features can be found in [change notes](https://github.com/SoftFever/OrcaSlicer/releases/)  

# How to install
## Windows: 
Unzip the binaries to any folder you prefer, then execute orca-slicer.exe to start the application.

## Mac:
 1. Download the right binaries for your computer: `arm64` version for Apple Silicon and `x86_64` for Intel CPU.  
 2. Double click to unzip the package, move OrcaSlicer.app to Application folder.  
 3. Run the following command in the terminal to allow running unsigned applications: `xattr -d com.apple.quarantine /Applications/OrcaSlicer.app`.

## Linux(Ubuntu):
Run the downlaoded AppImage.  
# How to compile
- Windows 64-bit  
  - Tools needed: Visual Studio 2019, Cmake, git, Strawberry Perl.
  - Run `build_release.bat` in `x64 Native Tools Command Prompt for VS 2019`

- Mac 64-bit  
  - Tools needed: Xcode, Cmake, git, gettext
  - run `build_release_macos.sh`


# License
Orca Slicer is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

The bambu networking plugin is based on non-free libraries. It is optional to the Orca Slicer and provides extended functionalities for users.

