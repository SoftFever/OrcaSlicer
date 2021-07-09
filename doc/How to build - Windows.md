# Step by Step Visual Studio 2019 Instructions

### Install the tools

Install Visual Studio Community 2019 from [visualstudio.microsoft.com/vs/](https://visualstudio.microsoft.com/vs/). Older versions are not supported as PrusaSlicer requires support for C++17.
Select all workload options for C++ 

Install git for Windows from [gitforwindows.org](https://gitforwindows.org/)
Download and run the exe accepting all defaults

### Download sources

Clone the respository.  To place it in C:\src\PrusaSlicer, run:
```
c:> mkdir src
c:> cd src
c:\src> git clone https://github.com/prusa3d/PrusaSlicer.git
```

### Compile the dependencies.
Dependencies are updated seldomly, thus they are compiled out of the PrusaSlicer source tree.
Go to the Windows Start Menu and Click on "Visual Studio 2019" folder, then select the ->"x64 Native Tools Command Prompt" to open a command window and run the following:
```
cd c:\src\PrusaSlicer\deps
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -DDESTDIR="c:\src\PrusaSlicer-deps"

msbuild /m ALL_BUILD.vcxproj // This took 13.5 minutes on my machine: core I7-7700K @ 4.2Ghz with 32GB main memory and 20min on a average laptop
```

### Generate Visual Studio project file for PrusaSlicer, referencing the precompiled dependencies.
Go to the Windows Start Menu and Click on "Visual Studio 2019" folder, then select the ->"x64 Native Tools Command Prompt" to open a command window and run the following:
```
cd c:\src\PrusaSlicer\
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -DCMAKE_PREFIX_PATH="c:\src\PrusaSlicer-deps\usr\local"
```

Note that `CMAKE_PREFIX_PATH` must be absolute path. A relative path like "..\..\PrusaSlicer-deps\usr\local" does not work.

### Compile PrusaSlicer. 

Double-click c:\src\PrusaSlicer\build\PrusaSlicer.sln to open in Visual Studio 2019.
OR
Open Visual Studio for C++ development (VS asks this the first time you start it).

Select PrusaSlicer_app_gui as your startup project (right-click->Set as Startup Project).

Run Build->Rebuild Solution once to populate all required dependency modules.  This is NOT done automatically when you build/run.  If you run both Debug and Release variants, you will need to do this once for each.

Debug->Start Debugging or press F5

PrusaSlicer should start. You're up and running!

note: Thanks to @douggorgen for the original guide, as an answer for a issue 


# The below information is out of date, but still useful for reference purposes

We have switched to MS Visual Studio 2019.

We don't use MSVS 2013 any more. At the moment we are in the process of creating new pre-built dependency bundles
and updating this document. In the meantime, you will need to compile the dependencies yourself
[the same way as before](#building-the-dependencies-package-yourself)
except with CMake generators for MSVS 2019 instead of 2013.

Thank you for understanding.

---

# Building PrusaSlicer on Microsoft Windows

~~The currently supported way of building PrusaSlicer on Windows is with CMake and MS Visual Studio 2013.
You can use the free [Visual Studio 2013 Community Edition](https://www.visualstudio.com/vs/older-downloads/).
CMake installer can be downloaded from [the official website](https://cmake.org/download/).~~

~~Building with newer versions of MSVS (2015, 2017) may work too as reported by some of our users.~~

_Note:_ Thanks to [**@supermerill**](https://github.com/supermerill) for testing and inspiration for this guide.

### Dependencies

On Windows PrusaSlicer is built against statically built libraries.
~~We provide a prebuilt package of all the needed dependencies. This package only works on Visual Studio 2013, so~~ if you are using a newer version of Visual Studio, you need to compile the dependencies yourself as per [below](#building-the-dependencies-package-yourself).
The package comes in a several variants:

  - ~~64 bit, Release mode only (41 MB, 578 MB unpacked)~~
  - ~~64 bit, Release and Debug mode (88 MB, 1.3 GB unpacked)~~
  - ~~32 bit, Release mode only (38 MB, 520 MB unpacked)~~
  - ~~32 bit, Release and Debug mode (74 MB, 1.1 GB unpacked)~~

When unsure, use the _Release mode only_ variant, the _Release and Debug_ variant is only needed for debugging & development.

If you're unsure where to unpack the package, unpack it into `C:\local\` (but it can really be anywhere).

Alternatively you can also compile the dependencies yourself, see below.

### Building PrusaSlicer with Visual Studio

First obtain the PrusaSlicer sources via either git or by extracting the source archive.

Then you will need to note down the so-called 'prefix path' to the dependencies, this is the location of the dependencies packages + `\usr\local` appended.
For example on 64 bits this would be `C:\local\destdir-64\usr\local`. The prefix path will need to be passed to CMake.

When ready, open the relevant Visual Studio command line and `cd` into the directory with PrusaSlicer sources.
Use these commands to prepare Visual Studio solution file:

    mkdir build
    cd build
    cmake .. -G "Visual Studio 12 Win64" -DCMAKE_PREFIX_PATH="<insert prefix path here>"

Note that if you're building a 32-bit variant, you will need to change the `"Visual Studio 12 Win64"` to just `"Visual Studio 12"`.

Conversely, if you're using Visual Studio version other than 2013, the version number will need to be changed accordingly.

If `cmake` has finished without errors, go to the build directory and open the `PrusaSlicer.sln` solution file in Visual Studio.
Before building, make sure you're building the right project (use one of those starting with `PrusaSlicer_app_...`) and that you're building
with the right configuration, i.e. _Release_ vs. _Debug_. When unsure, choose _Release_.
Note that you won't be able to build a _Debug_ variant against a _Release_-only dependencies package.

#### Installing using the `INSTALL` project

PrusaSlicer can be run from the Visual Studio or from Visual Studio's build directory (`src\Release` or `src\Debug`),
but for longer-term usage you might want to install somewhere using the `INSTALL` project.
By default, this installs into `C:\Program Files\PrusaSlicer`.
To customize the install path, use the `-DCMAKE_INSTALL_PREFIX=<path of your choice>` when invoking `cmake`.

### Building from the command line

There are several options for building from the command line:

- [msbuild](https://docs.microsoft.com/en-us/visualstudio/msbuild/msbuild-reference?view=vs-2017&viewFallbackFrom=vs-2013)
- [Ninja](https://ninja-build.org/)
- [nmake](https://docs.microsoft.com/en-us/cpp/build/nmake-reference?view=vs-2017)

To build with msbuild, use the same CMake command as in previous paragraph and then build using

    msbuild /m /P:Configuration=Release ALL_BUILD.vcxproj

To build with Ninja or nmake, replace the `-G` option in the CMake call with `-G Ninja` or `-G "NMake Makefiles"` , respectively.
Then use either `ninja` or `nmake` to start the build.

To install, use `msbuild /P:Configuration=Release INSTALL.vcxproj` , `ninja install` , or `nmake install` .

### Building the dependencies package yourself

The dependencies package is built using CMake scripts inside the `deps` subdirectory of PrusaSlicer sources.
(This is intentionally not interconnected with the CMake scripts in the rest of the sources.)

Open the preferred Visual Studio command line (64 or 32 bit variant) and `cd` into the directory with PrusaSlicer sources.
Then `cd` into the `deps` directory and use these commands to build:

    mkdir build
    cd build
    cmake .. -G "Visual Studio 16 2019" -DDESTDIR="C:\local\destdir-custom"
    msbuild /m ALL_BUILD.vcxproj

You can also use the Visual Studio GUI or other generators as mentioned above.

The `DESTDIR` option is the location where the bundle will be installed.
This may be customized. If you leave it empty, the `DESTDIR` will be placed inside the same `build` directory.

Warning: If the `build` directory is nested too deep inside other folders, various file paths during the build
become too long and the build might fail due to file writing errors (\*). For this reason, it is recommended to
place the `build` directory relatively close to the drive root.

Note that the build variant that you may choose using Visual Studio (i.e. _Release_ or _Debug_ etc.) when building the dependency package is **not relevant**.
The dependency build will by default build _both_ the _Release_ and _Debug_ variants regardless of what you choose in Visual Studio.
You can disable building of the debug variant by passing the

    -DDEP_DEBUG=OFF

option to CMake, this will only produce a _Release_ build.

Refer to the CMake scripts inside the `deps` directory to see which dependencies are built in what versions and how this is done.

\*) Specifically, the problem arises when building boost. Boost build tool appends all build options into paths of
intermediate files, which are not handled correctly by either `b2.exe` or possibly `ninja` (?).
