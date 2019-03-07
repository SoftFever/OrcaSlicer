
# Building Slic3r PE on Mac OS

To build Slic3r PE on Mac OS, you will need to install XCode, [CMake](https://cmake.org/) (available on Brew) and possibly git.

### Dependencies

Slic3r comes with a set of CMake scripts to build its dependencies, it lives in the `deps` directory.
Open a terminal window and navigate to Slic3r sources directory and then to `deps`.
Use the following commands to build the dependencies:

    mkdir build
    cd build
    cmake ..
    make

This will create a dependencies bundle inside the `build/destdir` directory.
You can also customize the bundle output path using the `-DDESTDIR=<some path>` option passed to `cmake`.

**Warning**: Once the dependency bundle is installed in a destdir, the destdir cannot be moved elsewhere.
(This is because wxWidgets hardcodes the installation path.)


### Building Slic3r

If dependencies are built without errors, you can proceed to build Slic3r itself.
Go back to top level Slic3r sources directory and use these commands:

    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local"

The `CMAKE_PREFIX_PATH` is the path to the dependencies bundle but with `/usr/local` appended - if you set a custom path
using the `DESTDIR` option, you will need to change this accordingly. **Warning:** the `CMAKE_PREFIX_PATH` needs to be an absolute path.

The CMake command above prepares Slic3r for building from the command line.
To start the build, use

    make -jN

where `N` is the number of CPU cores, so, for example `make -j4` for a 4-core machine.

Alternatively, if you would like to use XCode GUI, modify the `cmake` command to include the `-GXcode` option:

    cmake .. -GXcode -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local"

and then open the `Slic3r.xcodeproj` file.
This should open up XCode where you can perform build using the GUI or perform other tasks.

### Note on Mac OS X SDKs

By default Slic3r builds against whichever SDK is the default on the current system.

This can be customized. The `CMAKE_OSX_SYSROOT` option sets the path to the SDK directory location
and the `CMAKE_OSX_DEPLOYMENT_TARGET` option sets the target OS X system version (eg. `10.14` or similar).
Note you can set just one value and the other will be guessed automatically.
In case you set both, the two settings need to agree with each other. (Building with a lower deployment target
is currently unsupported because some of the dependencies don't support this, most notably wxWidgets.)

Please note that the `CMAKE_OSX_DEPLOYMENT_TARGET` and `CMAKE_OSX_SYSROOT` options need to be set the same
on both the dependencies bundle as well as Slic3r PE itself.

Official Mac Slic3r builds are currently built against SDK 10.9 to ensure compatibility with older Macs.
