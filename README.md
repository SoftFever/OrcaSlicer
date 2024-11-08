
<h1> <p "font-size:200px;"><img align="left" src="https://github.com/kennethjiang/JusPrin/blob/main/resources/images/JusPrin.ico" width="50"> JusPrin</p> </h1>

**Welcome to JusPrin, the first GenAI app for 3D printing!**

Brought to you by the creator of [The Spaghetti Detective](https://www.thespaghettidetective.com/), JusPrin aims to revolutionize the 3D printing experience by eliminating the need for manual slicing. Imagine: **no confusing slicing parameters, no headaches over printing issues caused by incorrect settings. Just print.** That's the promise of JusPrin.

## Table of Contents

- [Why JusPrin?](#why-jusprin)
- [Download](#download)
- [Getting Started](#getting-started)
- [Known Issues](#known-issues)
- [Feedback](#feedback)
- [License](#license)

## Why JusPrin?

3D printing enthusiasts have relied on slicing tools to prepare models, often struggling with complex parameters and trial-and-error setups. JusPrin changes all of that by leveraging Generative AI to automatically select and optimize slicing settings based on your requirements. It’s designed to:

- **Eliminate Complexity**: No need to tweak dozens of parameters. JusPrin’s AI takes care of it.
- **Reduce Printing Issues**: By intelligently choosing settings, JusPrin minimizes common print failures and errors.
- **Unlock Creative Solutions**: JusPrin sometimes surprises even us with creative combinations of slicing settings, pushing the boundaries of what’s possible in 3D printing.

This is a beta version, and while it’s not perfect yet, it’s already showing exciting potential. JusPrin is available for **Windows** and **Mac**. Linux support is coming soon.

## Download

📥 **[Download the Latest Stable Release](https://github.com/kennethjiang/JusPrin/releases/latest)**

## Getting Started

1. **Download**: Download the appropriate version for your operating system.
2. **Open JusPrin**: Launch the app. No complicated setup—just start creating.
3. **Load a Model**: Upload your 3D model (STL, OBJ, or 3MF formats).
4. **Just Print**: Tell JusBot your print requirements, even if you’re not exactly sure what they are. JusBot will guide you with suggestions based on your model and the AI's best estimations.
5. **Enjoy the Magic**: Watch as JusPrin slices your model!

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
## Feedback

Your feedback is invaluable! If you encounter any issues or notice settings that don’t seem optimal, please let us know. Send your thoughts directly to Kenneth at [k@obico.io](mailto:k@obico.io).

Every bit of feedback helps JusPrin become a better, smarter tool for everyone.

## License

JusPrin is open-source software licensed under the GNU Affero General Public License, based on Orca Slicer.

**Windows**:

- Install https://nsis.sourceforge.io/Download
- Open Developer PowerShell for VS 2022.
- `cd build`
- `cpack`

**MacOS**:

- `hdiutil create -volname "JustPrin" -srcfolder JusPrin.app -ov -format UDZO  JustPrin.dmg`


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
**Orca Slicer** is an open-source project and I'm deeply grateful to all my sponsors and backers.
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

**Special thank you to SoftFever's [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) that acts as a foundation for JusPrin!**
