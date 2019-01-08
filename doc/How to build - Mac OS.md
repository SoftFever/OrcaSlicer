
# Building Slic3r PE on Mac OS

To build Slic3r PE on Mac OS, you will need to install XCode and an appropriate SDK.
You will also need [CMake](https://cmake.org/) installed (available on Brew) and possibly git.

Currently Slic3r PE is built against the Mac OS X SDK version 10.9.
Building against older SDKs is unsupported. Building against newer SDKs might work,
but there may be subtle issues, such as dark mode not working very well on Mojave or other GUI problems.

You can obtain the SDK 10.9 for example [in this repository](https://github.com/phracker/MacOSX-SDKs).
If you don't already have the 10.9 version as part of your Mac OS installation, please download it
and place it into a reachable location.

The default location for Mac OS SDKs is:

    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/

Wherever the 10.9 SDK is, please note down its location, it will be required to build Slic3r.

On my system, for example, the path to the SDK is

    /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.9.sdk

### Dependencies

Slic3r comes with a set of CMake scripts to build its dependencies, it lives in the `deps` directory.
Open a terminal window and navigate to Slic3r sources directory and then to `deps`.
Use the following commands to build the dependencies:

    mkdir build
    cd build
    cmake .. -DDEPS_OSX_SYSROOT=<path to the 10.9 SDK>

This will create a dependencies bundle inside the `build/destdir` directory.
You can also customize the bundle output path using the `-DDESTDIR=<some path>` option passed to `cmake`.

### Building Slic3r

If dependencies built without an error, you can proceed to build Slic3r itself.
Go back to top level Slic3r sources directory and use these commands:

    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local" -DCMAKE_OSX_SYSROOT=<path to the 10.9 SDK>

The `CMAKE_PREFIX_PATH` is the path to the dependencies bundle but with `/usr/local` appended - if you set a custom path
using the `DESTDIR` option, you will need to change this accordingly. **Warning:** the `CMAKE_PREFIX_PATH` needs to be an absolute path.

The CMake command above prepares Slic3r for building from the command line.
To start the build, use

    make -jN

where `N` is the number of CPU cores, so, for example `make -j4` for a 4-core machine.

Alternatively, if you would like to use XCode GUI, modify the `cmake` command to include the `-GXcode` option:

    cmake .. -GXcode -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local" -DCMAKE_OSX_SYSROOT=<path to the 10.9 SDK>

and then open the `Slic3r.xcodeproj` file.
This should open up XCode where you can perform build using the GUI or perform other tasks.
