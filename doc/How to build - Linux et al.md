
# Building PrusaSlicer on UNIX/Linux

Please understand that PrusaSlicer team cannot support compilation on all possible Linux distros. Namely, we cannot help troubleshoot OpenGL driver issues or dependency issues if compiled against distro provided libraries. **We can only support PrusaSlicer statically linked against the dependencies compiled with the `deps` scripts**, the same way we compile PrusaSlicer for our [binary builds](https://github.com/prusa3d/PrusaSlicer/releases).

If you have some reason to link dynamically to your system libraries, you are free to do so, but we can not and will not troubleshoot any issues you possibly run into.

Instead of compiling PrusaSlicer from source code, one may also consider to install PrusaSlicer [pre-compiled by contributors](https://github.com/prusa3d/PrusaSlicer/wiki/PrusaSlicer-on-Linux---binary-distributions).

## Step by step guide

This guide describes building PrusaSlicer statically against dependencies pulled by our `deps` script. Running all the listed commands in order should result in successful build.

#### 0. Prerequisities

GNU build tools, CMake, git and other libraries have to be installed on the build machine.
Unless that's already the case, install them as usual from your distribution packages.
E.g. on Ubuntu 20.10, run
```shell
sudo apt-get install  -y \
git \
build-essential \
autoconf \
cmake \
libglu1-mesa-dev \
libgtk-3-dev \
libdbus-1-dev \

```
The names of the packages may be different on different distros.

#### 1. Cloning the repository


Cloning the repository is simple thanks to git and Github. Simply `cd` into wherever you want to clone PrusaSlicer code base and run
```
git clone https://www.github.com/prusa3d/PrusaSlicer
cd PrusaSlicer
```
This will download the source code into a new directory and `cd` into it. You can now optionally select a tag/branch/commit to build using `git checkout`. Otherwise, `master` branch will be built.


#### 2. Building dependencies

PrusaSlicer uses CMake and the build is quite simple, the only tricky part is resolution of dependencies. The supported and recommended way is to build the dependencies first and link to them statically. PrusaSlicer source base contains a CMake script that automatically downloads and builds the required dependencies. All that is needed is to run the following (from the top of the cloned repository):

    cd deps
    mkdir build
    cd build
    cmake .. -DDEP_WX_GTK3=ON
    make
    cd ../..


**Warning**: Once the dependency bundle is installed in a destdir, the destdir cannot be moved elsewhere. This is because wxWidgets hardcode the installation path.


#### 3. Building PrusaSlicer

Now when the dependencies are compiled, all that is needed is to tell CMake that we are interested in static build and point it to the dependencies. From the top of the repository, run

    mkdir build
    cd build
    cmake .. -DSLIC3R_STATIC=1 -DSLIC3R_GTK=3 -DSLIC3R_PCH=OFF -DCMAKE_PREFIX_PATH=$(pwd)/../deps/build/destdir/usr/local
    make -j4

And that's it. It is now possible to run the freshly built PrusaSlicer binary:

    cd src
    ./prusa-slicer




## Useful CMake flags when building dependencies

- `-DDESTDIR=<target destdir>` allows to specify a directory where the dependencies will be installed. When not provided, the script creates and uses `destdir` directory where cmake is run.

- `-DDEP_DOWNLOAD_DIR=<download cache dir>` specifies a directory to cache the downloaded source packages for each library. Can be useful for repeated builds, to avoid unnecessary network traffic.

- `-DDEP_WX_GTK3=ON` builds wxWidgets (one of the dependencies) against GTK3 (defaults to OFF)


## Useful CMake flags when building PrusaSlicer
- `-DSLIC3R_ASAN=ON` enables gcc/clang address sanitizer (defaults to `OFF`, requires gcc>4.8 or clang>3.1)
- `-DSLIC3R_GTK=3` to use GTK3 (defaults to `2`). Note that wxWidgets must be built against the same GTK version.
- `-DSLIC3R_STATIC=ON` for static build (defaults to `OFF`)
- `-DSLIC3R_WX_STABLE=ON` to look for wxWidgets 3.0 (defaults to `OFF`)
- `-DCMAKE_BUILD_TYPE=Debug` to build in debug mode (defaults to `Release`)

See the CMake files to get the complete list.



## Building dynamically

As already mentioned above, dynamic linking of dependencies is possible, but PrusaSlicer team is unable to troubleshoot (Linux world is way too complex). Feel free to do so, but you are on your own. Several remarks though:

The list of dependencies can be easily obtained by inspecting the CMake scripts in the `deps/` directory. Some of the dependencies don't have to be as recent as the versions listed - generally versions available on conservative Linux distros such as Debian stable, Ubuntu LTS releases or Fedora are likely sufficient. If you decide to build this way, it is your responsibility to make sure that CMake finds all required dependencies. It is possible to look at your distribution PrusaSlicer package to see how the package maintainers solved the dependency issues.

#### wxWidgets
By default, PrusaSlicer looks for wxWidgets 3.1. Our build script in fact downloads specific patched version of wxWidgets. If you want to link against wxWidgets 3.0 (which are still provided by most distributions because wxWidgets 3.1 have not yet been declared stable), you must set `-DSLIC3R_WX_STABLE=ON` when running CMake. Note that while PrusaSlicer can be linked against wWidgets 3.0, the combination is not well tested and there might be bugs in the resulting application. 

When building on ubuntu 20.04 focal fossa, the package libwxgtk3.0-gtk3-dev needs to be installed instead of libwxgtk3.0-dev and you should use:
```
-DSLIC3R_WX_STABLE=1 -DSLIC3R_GTK=3
``` 

## Miscellaneous

### Installation

At runtime, PrusaSlicer needs a way to access its resource files. By default, it looks for a `resources` directory relative to its binary.

If you instead want PrusaSlicer installed in a structure according to the File System Hierarchy Standard, use the `SLIC3R_FHS` flag

    cmake .. -DSLIC3R_FHS=1

This will make PrusaSlicer look for a fixed-location `share/slic3r-prusa3d` directory instead (note that the location becomes hardcoded).

You can then use the `make install` target to install PrusaSlicer.

### Desktop Integration (PrusaSlicer 2.4 and newer)

If PrusaSlicer is to be distributed as an AppImage or a binary blob (.tar.gz and similar), then a desktop integration support is compiled in by default: PrusaSlicer will offer to integrate with desktop by manually copying the desktop file and application icon into user's desktop configuration. The built-in desktop integration is also handy on Crosstini (Linux on Chrome OS).

If PrusaSlicer is compiled with `SLIC3R_FHS` enabled, then a desktop integration support will not be integrated. One may want to disable desktop integration by running
    
    cmake .. -DSLIC3R_DESKTOP_INTEGRATION=0
    
when building PrusaSlicer for flatpack or snap, where the desktop integration is performed by the installer.
