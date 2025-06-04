# How to build

## Windows 64-bit
This guide is for building your Visual Studio 2022 solution for OrcaSlicer on Windows 64-bit.

> [!Note]
> If you are using **Visual Studio 2019** you must use the following script instead:
> ```shell
> build_release.bat
> ```

### Tools needed:
 - Visual Studio 2022
   - ```shell
     winget install --id=Microsoft.VisualStudio.2022.Professional -e
     ```
   - Official download: [Visual Studio](https://visualstudio.microsoft.com/)
 - CMake (version > 3.14 and < 4.0; recommended: 3.31.6)
   - ```shell
     winget install --id=Kitware.CMake -v "3.31.6" -e
     ```
   - Github releases: [CMake 3.31.6](https://github.com/Kitware/CMake/releases/tag/v3.31.6)
 - Strawberry Perl.
   - ```shell
     winget install --id=StrawberryPerl.StrawberryPerl -e
     ```
   - Github releases [Strawberry Perl Latest](https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releaseslatest).
 - Git
   - ```shell
     winget install --id=Git.Git -e
     ```
   - Official download: [Git-scm](https://git-scm.com/downloads/win)
 - git-lfs
   - ```shell
     winget install --id=GitHub.GitLFS -e
     ```
   - Official download: [Git LFS](https://git-lfs.com/)

> [!Tip]
>  - GitHub Desktop (optional): A GUI for Git and Git LFS, which already includes both tools.
>    - ```shell
>      winget install --id=GitHub.GitHubDesktop -e
>      ```
>    - Official download: [GitHub Desktop](https://desktop.github.com/)

### Clone and Build

  1. Clone this repository:
     - If using GitHub Desktop clone the repository from the GUI.
     - If using the command line:
        1. Clone the repository:
        ```sh
        git clone https://github.com/SoftFever/OrcaSlicer
        ```
         2. Run lfs to download tools on Windows:
         ```sh
         git lfs pull
         ```
  2. Open:
     ```shell
     x64 Native Tools Command Prompt for VS 2022
     ```
     and run:
     ```shell
     build_release_vs2022.bat
     ```
  3. If successful, you will find the VS 2022 solution file in:
     ```shell
     build\OrcaSlicer.sln
     ```

## Mac 64-bit

### Tools needed:
 - Xcode
   - Official download: [Xcode](https://developer.apple.com/xcode/)
 - Cmake
   - ```shell
     brew install cmake
     ```
 - Git
   - ```shell
     brew install git
     ```
 - gettext
   - ```shell
     brew install gettext
     ```
 - Libtool
   - ```shell
     brew install libtool
     ```
 - Automake
   - ```shell
     brew install automake
     ```
 - Autoconf
   - ```shell
     brew install autoconf
     ```
 - Texinfo
   - ```shell
     brew install texinfo
     ```

> [!Tip]
> You can install most of them by running:
> ```shell
> brew install cmake gettext libtool automake autoconf texinfo
> ```

> [!IMPORTANT]
> If you haven’t already done so after upgrading Xcode, open it and install the macOS build support components.

### Clone and Build

  1. Clone this repository
  2. Run:
     ```shell
     build_release_macos.sh
     ```
  3. Open:
     ```shell
     build_arm64/OrcaSlicer/OrcaSlicer.app
     ```

  To build and debug in Xcode:

  1. Run:
     ```shell
     Xcode.app
     ```
  2. Open:
     ```shell
     build_`arch`/OrcaSlicer.Xcodeproj
     ```
  3. Menu bar: Product => Scheme => OrcaSlicer
  4. Menu bar: Product => Scheme => Edit Scheme...
  5. Run => Info tab => Build Configuration:
     ```shell
     RelWithDebInfo
     ```
  6. Run => Options tab => Document Versions: uncheck
     ```text
     Allow debugging when browsing versions
     ```
  7. Menu bar: Product => Run

## Linux (All Distros)

### Docker

#### Dependencies
 - Docker [Installation Instructions](https://www.docker.com/get-started/)
 - Git

#### Clone and Build
 1. Clone this repository:
    ```shell
    git clone https://github.com/SoftFever/OrcaSlicer
    ```
 2. Run:
    ```shell
    cd OrcaSlicer
    ```
 3. Run:
    ```shell
    ./DockerBuild.sh
    ```
 4. To run OrcaSlicer:
    ```shell
    ./DockerRun.sh
    ```

> [!Note]
> For most common errors, open:
> ```shell
> DockerRun.sh
> ```
> and read the comments.

### Ubuntu
#### Dependencies
All required dependencies will be automatically installed by the provided shell script:
```shell
libmspack-dev libgstreamerd-3-dev libsecret-1-dev libwebkit2gtk-4.0-dev libosmesa6-dev libssl-dev libcurl4-openssl-dev eglexternalplatform-dev libudev-dev libdbus-1-dev extra-cmake-modules libgtk2.0-dev libglew-dev libudev-dev libdbus-1-dev cmake git texinfo
```

#### Clone and Build
  1. Run sudo:
     ```shell
     ./BuildLinux.sh -u
     ```
  2. Run:
     ```shell
     ./BuildLinux.sh -dsi
     ```