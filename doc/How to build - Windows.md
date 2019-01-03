
# Building Slic3r PE on Microsoft Windows

The currently supported way of building Slic3r PE on Windows is with CMake and MS Visual Studio 2013.
You can use the free [Visual Studio 2013 Community Edition](https://www.visualstudio.com/vs/older-downloads/).
CMake installer can be downloaded from [the official website](https://cmake.org/download/).

Building with newer versions of MSVS (2015, 2017) may work too as reported by some of our users.

### Dependencies

On Windows Slic3r is built against statically built libraries.
We provide a prebuilt package of all the needed dependencies.
The package comes in a several variants:

  - [64 bit, Release mode only](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=destdir-64.7z) (41 MB, 546 MB unpacked)
  - [64 bit, Release and Debug mode](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=destdir-64-dev.7z) (88 MB, 1200 MB unpacked)
  - [32 bit, Release mode only](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=destdir-32.7z) (38 MB, 391 MB unpacked)
  - [32 bit, Release and Debug mode](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=destdir-32-dev.7z) (24 MB, 487 MB unpacked)

When unsure, use the _Release mode only_ variant, the _Release and Debug_ variant is only needed for debugging & developement.

If you're unsure where to unpack the package, unpack it into `C:\local\` (but it can really be anywhere).

Alternatively you can also compile the dependencies yourself, see below.

### Building Slic3r PE with Visual Studio

First obtain the Slic3 PE sources via either git or by extracting the source archive.

Then you will need to note down the so-called 'prefix path' to the dependencies, this is the location of the dependencies packages + `\usr\local` appended.
For example on 64 bits this would be `C:\local\destdir-64\usr\local`. The prefix path will need to be passed to CMake.

When ready, open the relevant Visual Studio command line and `cd` into the directory with Slic3r sources.
Use these commands to prepare Visual Studio solution file:

    mkdir build
    cd build
    cmake .. -G "Visual Studio 12 Win64" -DCMAKE_PREFIX_PATH="<insert prefix path here>"

Note that if you're building a 32-bit variant, you will need to change the `"Visual Studio 12 Win64"` to just `"Visual Studio 12"`.

Conversely, if you're using Visual Studio version other than 2013, the version number will need to be changed accordingly.

If `cmake` has finished without errors, go to the build directory and open the `Slic3r.sln` solution file in Visual Studio.
Before building, make sure you're building the right project (use one of those starting with `slic3r_app_...`) and that you're building
with the right configuration, ie. _Release_ vs. _Debug_. When unsure, choose _Release_.
Note that you won't be able to build a _Debug_ variant against a _Release_-only dependencies package.

TODO: Install paragraph from @supermerill (?), also credit

### Building from the command line

There are several options for building from the command line:

- [msbuild](https://docs.microsoft.com/en-us/visualstudio/msbuild/msbuild-reference?view=vs-2017&viewFallbackFrom=vs-2013)
- [Ninja](https://ninja-build.org/)
- [nmake](https://docs.microsoft.com/en-us/cpp/build/nmake-reference?view=vs-2017)

To build with msbuild, use the same CMake command as in previous paragraph and then build using

    msbuild /P:Configuration=Release ALL_BUILD.vcxproj

To build with Ninja or nmake, replace the `-G` option in the CMake call with `-G Ninja` or `-G "NMake Makefiles"`, respectively.
Then use either `ninja` or `nmake` to start the build.

### Building the dependencies package yourself

The dependencies package is built using CMake scripts inside the `deps` subdirectory of Slic3r PE sources.
(This is intentionally not interconnected with the CMake scripts in the rest of the sources.)

Open the preferred Visual Studio command line and `cd` into the directory with Slic3r sources.
Then `cd` into the `deps` directory and use these commands to build:

    mkdir build
    cd build
    cmake .. -G "Visual Studio 12 Win64" -DDESTDIR="C:\local\destdir-custom"
    msbuild ALL_BUILD.vcxproj

You can also use the Visual Studio GUI or other generators as mentioned above.

The `DESTDIR` option is the location where the bundle will be installed.
This may be customized. If you leave it empty, the `DESTDIR` will be places inside the same `build` directory.

Note that the build variant that you may choose using Visual Studio (ie. _Release_ or _Debug_ etc.) when building the dependency package is **not relevant**.
The dependency build will by default build _both_ the _Release_ and _Debug_ variants regardless of what you choose in Visual Studio.
You can disable building of the debug variant by passing the `-DDEP_DEBUG=OFF` option to CMake, this will only produce a _Release_ build.

Refer to the CMake scripts inside the `deps` directory to see which dependencies are built in what versions and how this is done.
