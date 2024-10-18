
<h1> <p "font-size:200px;"><img align="left" src="https://github.com/KDW06/OrcaSlicer/blob/main/resources/images/OrcaSlicer.ico" width="100"> Orca Slicer</p> </h1>

[![Build all](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_all.yml/badge.svg?branch=main)](https://github.com/SoftFever/OrcaSlicer/actions/workflows/build_all.yml)
<br>Orca Slicer is an open source slicer for FDM printers. 


Join our Discord community here:<br>
<a href="https://discord.gg/P4VE9UY9gJ"><img src="https://img.shields.io/static/v1?message=Discord&logo=discord&label=&color=7289DA&logoColor=white&labelColor=&style=for-the-badge" height="35" alt="discord logo"/> </a>
 
 <h3>🚨🚨🚨Important Security Alert🚨🚨🚨</h3> 

Please be aware that "orcaslicer.net" is NOT an official website for OrcaSlicer and may be potentially malicious. This site appears to use AI-generated content, lacking genuine context, and seems to exist solely to profit from advertisements. Worse, it may redirect download links to harmful sources. For your safety, avoid downloading OrcaSlicer from this site as the links may be compromised.

The only official platforms for OrcaSlicer are our GitHub project page and the  <a href="https://discord.gg/P4VE9UY9gJ">official Discord channel</a> .

We deeply value our OrcaSlicer community and appreciate all the social groups that support us. However, it is crucial to address the risk posed by any group that falsely claims to be official or misleads its members. If you encounter such a group or are part of one, please assist by encouraging the group owner to add a clear disclaimer or by alerting its members.

Thank you for your vigilance and support in keeping our community safe!

# Main features
- Auto-calibration for all printers
- Sandwich (inner-outer-inner) mode - An improved version of the `External Perimeters First` mode
- [Precise wall](https://github.com/SoftFever/OrcaSlicer/wiki/Precise-wall)
- Polyholes conversion support: [SuperSlicer Wiki: Polyholes](https://github.com/supermerill/SuperSlicer/wiki/Polyholes)
- Klipper support
- More granular controls
- Additional features can be found in the [change notes](https://github.com/SoftFever/OrcaSlicer/releases/)  

# Wiki
The wiki below aims to provide a detailed explanation of the slicer settings, including how to maximize their use and how to calibrate and set up your printer.

Please note that the wiki is a work in progress. We appreciate your patience as we continue to develop and improve it!

**[Access the wiki here](https://github.com/SoftFever/OrcaSlicer/wiki)**  

# Download

### Stable Release
📥 **[Download the Latest Stable Release](https://github.com/SoftFever/OrcaSlicer/releases/latest)**  
Visit our GitHub Releases page for the latest stable version of Orca Slicer, recommended for most users.

### Nightly Builds
🌙 **[Download the Latest Nightly Build](https://github.com/SoftFever/OrcaSlicer/releases/tag/nightly-builds)**  
Explore the latest developments in Orca Slicer with our nightly builds. Feedback on these versions is highly appreciated.


# How to install
**Windows**: 
1.  Download the installer for your preferred version from the [releases page](https://github.com/SoftFever/OrcaSlicer/releases).
    - *For convenience there is also a portable build available.*
    - *If you have troubles to run the build, you might need to install following runtimes:*
      - [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)
          - [Details of this runtime](https://aka.ms/webview2)
          - [Alternative Download Link Hosted by Microsoft](https://go.microsoft.com/fwlink/p/?LinkId=2124703)
      - [vcredist2019_x64](https://github.com/SoftFever/OrcaSlicer/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)
          -  [Alternative Download Link Hosted by Microsoft](https://aka.ms/vs/17/release/vc_redist.x64.exe)
          -  This file may already be available on your computer if you've installed visual studio.  Check the following location: `%VCINSTALLDIR%Redist\MSVC\v142`

**Mac**:
1. Download the DMG for your computer: `arm64` version for Apple Silicon and `x86_64` for Intel CPU.  
2. Drag OrcaSlicer.app to Application folder. 
3. *If you want to run a build from a PR, you also need to follow the instructions below:*  
    <details quarantine>
    - Option 1 (You only need to do this once. After that the app can be opened normally.):
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
    </details>
    
**Linux (Ubuntu)**:
 1. If you run into trouble executing it, try this command in the terminal:  
    `chmod +x /path_to_appimage/OrcaSlicer_Linux.AppImage`
    
# How to compile
- Windows 64-bit  
  - Tools needed: Visual Studio 2019, Cmake, git, git-lfs, Strawberry Perl.
      - You will require cmake version 3.14 or later, which is available [on their website](https://cmake.org/download/).
      - Strawberry Perl is [available on their GitHub repository](https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/).
  - Run `build_release.bat` in `x64 Native Tools Command Prompt for VS 2019`
  - Note: Don't forget to run `git lfs pull` after cloning the repository to download tools on Windows

- Mac 64-bit  
  - Tools needed: Xcode, Cmake, git, gettext, libtool, automake, autoconf, texinfo
      - You can install most of them by running `brew install cmake gettext libtool automake autoconf texinfo`
  - run `build_release_macos.sh`
  - To build and debug in Xcode:
      - run `Xcode.app`
      - open ``build_`arch`/OrcaSlicer.Xcodeproj``
      - menu bar: Product => Scheme => OrcaSlicer
      - menu bar: Product => Scheme => Edit Scheme...
          - Run => Info tab => Build Configuration: `RelWithDebInfo`
          - Run => Options tab => Document Versions: uncheck `Allow debugging when browsing versions`
      - menu bar: Product => Run

- Ubuntu 
  - Dependencies **Will be auto-installed with the shell script**: `libmspack-dev libgstreamerd-3-dev libsecret-1-dev libwebkit2gtk-4.0-dev libosmesa6-dev libssl-dev libcurl4-openssl-dev eglexternalplatform-dev libudev-dev libdbus-1-dev extra-cmake-modules libgtk2.0-dev libglew-dev libudev-dev libdbus-1-dev cmake git texinfo`
  - run 'sudo ./BuildLinux.sh -u'
  - run './BuildLinux.sh -dsir'


# Note: 
If you're running Klipper, it's recommended to add the following configuration to your `printer.cfg` file.
```
# Enable object exclusion
[exclude_object]

# Enable arcs support
[gcode_arcs]
resolution: 0.1
```

# Supports
**Orca Slicer** is an open-source project, and I'm deeply grateful to all my sponsors and backers.   
Their generous support enables me to purchase filaments and other essential 3D printing materials for the project.   
Thank you! :)

### Sponsors:  
<table>
<tr>
<td>
<a href="https://qidi3d.com/">
    <img src="SoftFever_doc\sponsor_logos\QIDI.png" alt="QIDI" width="96" height="">
</a>
</td>
<td>
<a href="https://phrozen3d.com/">
    <img src="SoftFever_doc\sponsor_logos\Phrozen_Logo圓_.png" alt="Phrozen Technology" width="96" height="">
</a>
</td>
<td>
<a href="https://bigtree-tech.com/">
    <img src="SoftFever_doc\sponsor_logos\BigTreeTech.png" alt="BIGTREE TECH" width="96" height="">
</a>
</td>
</tr>
</table>

### Backers:  
**Ko-fi supporters**: [Backers list](https://github.com/user-attachments/files/16147016/Supporters_638561417699952499.csv)

## Support me  
<a href="https://github.com/sponsors/SoftFever"><img src="https://img.shields.io/static/v1?label=Sponsor&message=%E2%9D%A4&logo=GitHub&color=%23fe8e86" width="130"></a>

<a href="https://ko-fi.com/G2G5IP3CP"><img src="https://ko-fi.com/img/githubbutton_sm.svg" width="200"></a>

[![PayPal](https://img.shields.io/badge/PayPal-00457C?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/softfever3d)


## Some background
OrcaSlicer is originally forked from Bambu Studio, it was previously known as BambuStudio-SoftFever.

Bambu Studio is forked from [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community. 
Orca Slicer incorporates a lot of features from SuperSlicer by @supermerill
Orca Slicer's logo is designed by community member Justin Levine(@freejstnalxndr)  


# License
Orca Slicer is licensed under the GNU Affero General Public License, version 3. Orca Slicer is based on Bambu Studio by BambuLab.

Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

Orca Slicer includes a pressure advance calibration pattern test adapted from Andrew Ellis' generator, which is licensed under GNU General Public License, version 3. Ellis' generator is itself adapted from a generator developed by Sineos for Marlin, which is licensed under GNU General Public License, version 3.

The Bambu networking plugin is based on non-free libraries from BambuLab. It is optional to the Orca Slicer and provides extended functionalities for Bambulab printer users.

