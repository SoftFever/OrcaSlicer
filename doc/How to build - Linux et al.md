
# Building PrusaSlicer on UNIX/Linux

PrusaSlicer uses the CMake build system and requires several dependencies.
The dependencies can be listed in `deps/deps-linux.cmake` and `deps/deps-unix-common.cmake`, although they don't necessarily need to be as recent
as the versions listed - generally versions available on conservative Linux distros such as Debian stable or CentOS should suffice.

Perl is not required any more.

In a typical situation, one would open a command line, go to the PrusaSlicer sources, create a directory called `build` or similar,
`cd` into it and call:

    cmake ..
    make -jN

where `N` is the number of CPU cores available.

Additional CMake flags may be applicable as explained below.

### Dependency resolution

By default PrusaSlicer looks for dependencies the default way CMake looks for them, i.e. in default system locations.
On Linux this will typically make PrusaSlicer depend on dynamically loaded libraries from the system, however, PrusaSlicer can be told
to specifically look for static libraries with the `SLIC3R_STATIC` flag passed to cmake:

    cmake .. -DSLIC3R_STATIC=1

Additionally, PrusaSlicer can be built in a static manner mostly independent of the system libraries with a dependencies bundle
created using CMake script in the `deps` directory (these are not interconnected with the rest of the CMake scripts).

Note: We say _mostly independent_ because it's still expected the system will provide some transitive dependencies, such as GTK for wxWidgets.

To do this, go to the `deps` directory, create a `build` subdirectory (or the like) and use:

    cmake .. -DDESTDIR=<target destdir>

where the target destdir is a directory of your choosing where the dependencies will be installed.
You can also omit the `DESTDIR` option to use the default, in that case the `destdir` will be created inside the `build` directory where `cmake` is run.

To pass the destdir path to the top-level PrusaSlicer CMake script, use the `CMAKE_PREFIX_PATH` option along with turning on `SLIC3R_STATIC`:

    cmake .. -DSLIC3R_STATIC=1 -DCMAKE_PREFIX_PATH=<path to destdir>/usr/local

Note that `/usr/local` needs to be appended to the destdir path and also the prefix path should be absolute.

**Warning**: Once the dependency bundle is installed in a destdir, the destdir cannot be moved elsewhere.
This is because wxWidgets hardcode the installation path.

### wxWidgets version

By default, PrusaSlicer looks for wxWidgets 3.1, this is because the 3.1 version has
a number of bugfixes and improvements not found in 3.0. However, it can also be built with wxWidgets 3.0.
This is done by passing this option to CMake:

    -DSLIC3R_WX_STABLE=1

Note that PrusaSlicer is tested with wxWidgets 3.0 somewhat sporadically and so there may be bugs in bleeding edge releases.

When building on ubuntu 20.04 focal fossa, the package libwxgtk3.0-gtk3-dev needs to be installed instead of libwxgtk3.0-dev and you should use:

    -DSLIC3R_WX_STABLE=1 -DSLIC3R_GTK=3

### Build variant

By default PrusaSlicer builds the release variant.
To create a debug build, use the following CMake flag:

    -DCMAKE_BUILD_TYPE=Debug

### Enabling address sanitizer

If you're using GCC/Clang compiler, it is possible to build PrusaSlicer with the built-in address sanitizer enabled to help detect memory-corruption issues.
To enable it, simply use the following CMake flag:

    -DSLIC3R_ASAN=1

This requires GCC>4.8 or Clang>3.1.

### Installation

At runtime, PrusaSlicer needs a way to access its resource files. By default, it looks for a `resources` directory relative to its binary.

If you instead want PrusaSlicer installed in a structure according to the File System Hierarchy Standard, use the `SLIC3R_FHS` flag

    cmake .. -DSLIC3R_FHS=1

This will make PrusaSlicer look for a fixed-location `share/slic3r-prusa3d` directory instead (note that the location becomes hardcoded).

You can then use the `make install` target to install PrusaSlicer.
