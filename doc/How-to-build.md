# How to Compile

## Windows 64-bit

### Tools Required
- [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) or Visual Studio 2019  
- [CMake (version 3.31)](https://cmake.org/) — **⚠️ version 3.31.x is mandatory**
- [Strawberry Perl](https://strawberryperl.com/)
- [Git](https://git-scm.com/)
- [Git LFS](https://git-lfs.github.com/)

### Instructions
1. Clone the repository:
   ```sh
   git clone https://github.com/SoftFever/OrcaSlicer
   cd OrcaSlicer
   git lfs pull
   ```
2. Open the appropriate command prompt:
   - For Visual Studio 2019:  
     Open **x64 Native Tools Command Prompt for VS 2019** and run:
     ```sh
     build_release.bat
     ```
   - For Visual Studio 2022:  
     Open **x64 Native Tools Command Prompt for VS 2022** and run:
     ```sh
     build_release_vs2022.bat
     ```

**⚠️ Note 1:** Make sure that CMake version 3.31.x is actually being used. Run `cmake --version` and verify it returns a **3.31.x** version.
If you see an older version (e.g. **3.29**), it's likely due to another copy in your system's PATH (e.g. from Strawberry Perl).
You can run where cmake to check the active paths and rearrange your System Environment Variables > PATH, ensuring the correct CMake (e.g. C:\Program Files\CMake\bin) appears before others like C:\Strawberry\c\bin.

**⚠️ Note 2:** If the build fails, delete the entire project directory, re-clone the repository, and try again to ensure a clean cache.

## macOS 64-bit

### Tools Required
- Xcode
- CMake
- Git
- gettext
- libtool
- automake
- autoconf
- texinfo

You can install most dependencies via Homebrew:
```sh
brew install cmake gettext libtool automake autoconf texinfo
```

If you've recently upgraded Xcode, be sure to open Xcode at least once and install the required macOS build support.

### Instructions
1. Clone the repository:
   ```sh
   git clone https://github.com/SoftFever/OrcaSlicer
   cd OrcaSlicer
   ```
2. Build the application:
   ```sh
   ./build_release_macos.sh
   ```
3. Open the application:
   ```sh
   open build_arm64/OrcaSlicer/OrcaSlicer.app
   ```

### Debugging in Xcode
To build and debug directly in Xcode:

1. Open the Xcode project:
   ```sh
   open build_`arch`/OrcaSlicer.Xcodeproj
   ```
2. In the menu bar:
   - **Product > Scheme > OrcaSlicer**
   - **Product > Scheme > Edit Scheme...**
     - Under **Run > Info**, set **Build Configuration** to `RelWithDebInfo`
     - Under **Run > Options**, uncheck **Allow debugging when browsing versions**
   - **Product > Run**

## Linux

### Using Docker (Recommended)

#### Dependencies
- Docker
- Git

#### Instructions
```sh
git clone https://github.com/SoftFever/OrcaSlicer
cd OrcaSlicer
./DockerBuild.sh
./DockerRun.sh
```

To troubleshoot common Docker-related errors, refer to the comments in `DockerRun.sh`.

## Ubuntu

### Dependencies
All required dependencies will be installed automatically by the provided shell script, including:
- libmspack-dev
- libgstreamerd-3-dev
- libsecret-1-dev
- libwebkit2gtk-4.0-dev
- libosmesa6-dev
- libssl-dev
- libcurl4-openssl-dev
- eglexternalplatform-dev
- libudev-dev
- libdbus-1-dev
- extra-cmake-modules
- libgtk2.0-dev
- libglew-dev
- cmake
- git
- texinfo

### Instructions
```sh
sudo ./BuildLinux.sh -u      # Install dependencies
./BuildLinux.sh -dsi         # Build OrcaSlicer
```
