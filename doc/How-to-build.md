# How to Build

## Windows 64-bit

This guide is for building your Visual Studio 2022 solution for OrcaSlicer on Windows 64-bit.

### Tools Required

- [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) or Visual Studio 2019
  ```shell
  winget install --id=Microsoft.VisualStudio.2022.Professional -e
  ```
- [CMake (version 3.31)](https://cmake.org/) — **⚠️ version 3.31.x is mandatory**
  ```shell
  winget install --id=Kitware.CMake -v "3.31.6" -e
  ```
- [Strawberry Perl](https://strawberryperl.com/)
  ```shell
  winget install --id=StrawberryPerl.StrawberryPerl -e
  ```
- [Git](https://git-scm.com/)
  ```shell
  winget install --id=Git.Git -e
  ```
- [git-lfs](https://git-lfs.com/)
  ```shell
  winget install --id=GitHub.GitLFS -e
  ```

> [!TIP]
> GitHub Desktop (optional): A GUI for Git and Git LFS, which already includes both tools.
> ```shell
> winget install --id=GitHub.GitHubDesktop -e
> ```

### Instructions

1. Clone the repository:
   - If using GitHub Desktop clone the repository from the GUI.
   - If using the command line:
     1. Clone the repository:
     ```shell
     git clone https://github.com/SoftFever/OrcaSlicer
     ```
     2. Run lfs to download tools on Windows:
     ```shell
     git lfs pull
     ```
2. Open the appropriate command prompt:
   - For Visual Studio 2019:  
     Open **x64 Native Tools Command Prompt for VS 2019** and run:
     ```shell
     build_release.bat
     ```
   - For Visual Studio 2022:  
     Open **x64 Native Tools Command Prompt for VS 2022** and run:
     ```shell
     build_release_vs2022.bat
     ```
3. If successful, you will find the VS 2022 solution file in:
   ```shell
   build\OrcaSlicer.sln
   ```

> [!IMPORTANT]
> Make sure that CMake version 3.31.x is actually being used. Run `cmake --version` and verify it returns a **3.31.x** version.
> If you see an older version (e.g. 3.29), it's likely due to another copy in your system's PATH (e.g. from Strawberry Perl).
> You can run where cmake to check the active paths and rearrange your System Environment Variables > PATH, ensuring the correct CMake (e.g. C:\Program Files\CMake\bin) appears before others like C:\Strawberry\c\bin.

> [!NOTE]
> If the build fails, try deleting the `build/` and `deps/build/` directories to clear any cached build data. Rebuilding after a clean-up is usually sufficient to resolve most issues.

## macOS 64-bit

### Tools Required

- Xcode
- CMake (version 3.31.x is mandatory)
- Git
- gettext
- libtool
- automake
- autoconf
- texinfo

> [!TIP]
> You can install most of them by running:
> ```shell
> brew install gettext libtool automake autoconf texinfo
> ```

Homebrew currently only offers the latest version of CMake (e.g. **4.X**), which is not compatible. To install the required version **3.31.X**, follow these steps:

1. Download CMake **3.31.7** from: [https://cmake.org/download/](https://cmake.org/download/)
2. Install the application (drag it to `/Applications`).
3. Add the following line to your shell configuration file (`~/.zshrc` or `~/.bash_profile`):

```sh
export PATH="/Applications/CMake.app/Contents/bin:$PATH"
```

4. Restart the terminal and check the version:

```sh
cmake --version
```

5. Make sure it reports a **3.31.x** version.

> [!IMPORTANT]
> If you've recently upgraded Xcode, be sure to open Xcode at least once and install the required macOS build support.

### Instructions

1. Clone the repository:
   ```shell
   git clone https://github.com/SoftFever/OrcaSlicer
   cd OrcaSlicer
   ```
2. Build the application:
   ```shell
   ./build_release_macos.sh
   ```
3. Open the application:
   ```shell
   open build/arm64/OrcaSlicer/OrcaSlicer.app
   ```

### Debugging in Xcode

To build and debug directly in Xcode:

1. Open the Xcode project:
   ```shell
   open build/arm64/OrcaSlicer.xcodeproj
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

```shell
git clone https://github.com/SoftFever/OrcaSlicer && cd OrcaSlicer && ./DockerBuild.sh && ./DockerRun.sh
```

> [!NOTE]
> To troubleshoot common Docker-related errors, refer to the comments in
> ```shell
> DockerRun.sh
> ```

## Ubuntu

### Dependencies

All required dependencies will be installed automatically by the provided shell script, including:

- libmspack-dev
- libgstreamerd-3-dev
- libsecret-1-dev
- libwebkit2gtk-4.0-dev
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

```shell
`./build_linux.sh -u`      # install dependencies
`./build_linux.sh -disr`    # build OrcaSlicer
```
