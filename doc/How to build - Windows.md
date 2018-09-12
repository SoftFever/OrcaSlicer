# Building Slic3r PE on Microsoft Windows

The currently supported way of building Slic3r PE on Windows is with CMake and MS Visual Studio 2013
using our Perl binary distribution (compiled from official Perl sources).
You can use the free [Visual Studio 2013 Community Edition](https://www.visualstudio.com/vs/older-downloads/).
CMake installer can be downloaded from [the official website](https://cmake.org/download/).

Other setups (such as mingw + Strawberry Perl) _may_ work, but we cannot guarantee this will work
and cannot provide guidance.


### Geting the dependencies

First, download and upnack our Perl + wxWidgets binary distribution:

  - 32 bit, release mode: [wperl32-5.24.0-2018-03-02.7z](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=wperl32-5.24.0-2018-03-02.7z)
  - 64 bit, release mode: [wperl64-5.24.0-2018-03-02.7z](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=wperl64-5.24.0-2018-03-02.7z)
  - 64 bit, release mode + debug symbols: [wperl64d-5.24.0-2018-03-02.7z](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=wperl64d-5.24.0-2018-03-02.7z)

It is recommended to unpack this package into `C:\`.

Apart from wxWidgets and Perl, you will also need additional dependencies:

  - Boost
  - Intel TBB
  - libcurl

We have prepared a binary package of the listed libraries:

  - 32 bit: [slic3r-destdir-32.7z](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=2%2Fslic3r-destdir-32.7z)
  - 64 bit: [slic3r-destdir-64.7z](https://bintray.com/vojtechkral/Slic3r-PE/download_file?file_path=2%2Fslic3r-destdir-64.7z)

It is recommended you unpack this package into `C:\local\` as the environment
setup script expects it there.

Alternatively you can also compile the additional dependencies yourself.
There is a [powershell script](./deps-build/windows/slic3r-makedeps.ps1) which automates this process.

### Building Slic3r PE

Once the dependencies are set up in their respective locations,
go to the `wperl*` directory extracted earlier and launch the `cmdline.lnk` file
which opens a command line prompt with appropriate environment variables set up.

In this command line, `cd` into the directory with Slic3r sources
and use these commands to build the Slic3r from the command line:

    perl Build.PL
    perl Build.PL --gui
    mkdir build
    cd build
    cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    nmake
    cd ..
    perl slic3r.pl

The above commands use `nmake` Makefiles.
You may also build Slic3r PE with other build tools:


### Building with Visual Studio

To build and debug Slic3r PE with Visual Studio (64 bits), replace the `cmake` command with:

    cmake .. -G "Visual Studio 12 Win64" -DCMAKE_CONFIGURATION_TYPES=RelWithDebInfo

For the 32-bit variant, use:

    cmake .. -G "Visual Studio 12" -DCMAKE_CONFIGURATION_TYPES=RelWithDebInfo

After `cmake` has finished, go to the build directory and open the `Slic3r.sln` solution file.
This should open Visual Studio and load the Slic3r solution containing all the projects.
Make sure you use Visual Studio 2013 to open the solution.

You can then use the usual Visual Studio controls to build Slic3r (Hit `F5` to build and run with debugger).
If you want to run or debug Slic3r from within Visual Studio, make sure the `XS` project is activated.
It should be set as the Startup project by CMake by default, but you might want to check anyway.
There are multiple projects in the Slic3r solution, but only the `XS` project is configured with the right
commands to run and debug Slic3r.

The above cmake commands generate Visual Studio project files with the `RelWithDebInfo` configuration only.
If you also want to use the `Release` configuration, you can generate Visual Studio projects with:

    -DCMAKE_CONFIGURATION_TYPES=Release;RelWithDebInfo

(The `Debug` configuration is not supported as of now.)

### Building with ninja

To use [Ninja](https://ninja-build.org/), replace the `cmake` and `nmake` commands with:

    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja
