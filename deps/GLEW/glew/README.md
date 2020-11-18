THIS IS NOT THE COMPLETE GLEW DISTRIBUTION. ONLY FILES NEEDED FOR COMPILING GLEW INTO SLIC3R WERE PUT INTO THE SLIC3R SOURCE DISTRIBUTION.

A CMAKE CONFIG EXPORT IS ADDED TO ENABLE FIND PACKAGE TO FIND DEBUG BUILD ON MSVC

# GLEW - The OpenGL Extension Wrangler Library

![](http://glew.sourceforge.net/glew.png)

http://glew.sourceforge.net/

https://github.com/nigels-com/glew

[![Build Status](https://travis-ci.org/nigels-com/glew.svg?branch=master)](https://travis-ci.org/nigels-com/glew)
[![Gitter](https://badges.gitter.im/nigels-com/glew.svg)](https://gitter.im/nigels-com/glew?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)
[![Download](https://img.shields.io/sourceforge/dm/glew.svg)](https://sourceforge.net/projects/glew/files/latest/download)

## Downloads

Current release is [2.0.0](https://sourceforge.net/projects/glew/files/glew/2.0.0/).
[(Change Log)](http://glew.sourceforge.net/log.html)

Sources available as 
[ZIP](https://sourceforge.net/projects/glew/files/glew/2.0.0/glew-2.0.0.zip/download) or
[TGZ](https://sourceforge.net/projects/glew/files/glew/2.0.0/glew-2.0.0.tgz/download).

Windows binaries for [32-bit and 64-bit](https://sourceforge.net/projects/glew/files/glew/2.0.0/glew-2.0.0-win32.zip/download).

### Recent snapshots

Snapshots may contain new features, bug-fixes or new OpenGL extensions ahead of tested, official releases.

[glew-20160708.tgz](http://sourceforge.net/projects/glew/files/glew/snapshots/glew-20160708.tgz/download)
*GLEW 2.0.0 RC: Core context, EGL support, no MX*

[glew-20160402.tgz](http://sourceforge.net/projects/glew/files/glew/snapshots/glew-20160402.tgz/download)
*GLEW 2.0.0 RC: Core context, EGL support, no MX*

## Build

From a downloaded tarball or zip archive:

### Linux and Mac

#### Using GNU Make

##### Install build tools

Debian/Ubuntu/Mint:    `$ sudo apt-get install build-essential libxmu-dev libxi-dev libgl-dev libosmesa-dev git`

RedHat/CentOS/Fedora:  `$ sudo yum install libXmu-devel libXi-devel libGL-devel git`

##### Build

	$ make
	$ sudo make install
	$ make clean

Targets:    `all, glew.lib, glew.bin, clean, install, uninstall`

Variables:  `SYSTEM=linux-clang, GLEW_DEST=/usr/local, STRIP=`

#### Using cmake

*CMake 2.8.12 or higher is required.*

##### Install build tools

Debian/Ubuntu/Mint:   `$ sudo apt-get install build-essential libXmu-dev libXi-dev libgl-dev git cmake`

RedHat/CentOS/Fedora: `$ sudo yum install libXmu-devel libXi-devel libGL-devel git cmake`

##### Build

	$ cd build
	$ cmake ./cmake 
	$ make -j4

| Target     | Description |
| ---------- | ----------- |
| glew       | Build the glew shared library. |
| glew_s     | Build the glew static library. |
| glewinfo   | Build the `glewinfo` executable (requires `BUILD_UTILS` to be `ON`). |
| visualinfo | Build the `visualinfo` executable (requires `BUILD_UTILS` to be `ON`). |
| install    | Install all enabled targets into `CMAKE_INSTALL_PREFIX`. |
| clean      | Clean up build artifacts. |
| all        | Build all enabled targets (default target). |

| Variables       | Description |
| --------------- | ----------- |
| BUILD_UTILS     | Build the `glewinfo` and `visualinfo` executables. |
| GLEW_REGAL      | Build in Regal mode. |
| GLEW_OSMESA     | Build in off-screen Mesa mode. |
| BUILD_FRAMEWORK | Build as MacOSX Framework.  Setting `CMAKE_INSTALL_PREFIX` to `/Library/Frameworks` is recommended. |

### Windows

#### Visual Studio

Use the provided Visual Studio project file in build/vc12/

Projects for vc6 and vc10 are also provided

#### MSYS/Mingw

Available from [Mingw](http://www.mingw.org/)

Requirements: bash, make, gcc

	$ mingw32-make
	$ mingw32-make install
	$ mingw32-make install.all

Alternative toolchain:  `SYSTEM=mingw-win32`

#### MSYS2/Mingw-w64

Available from [Msys2](http://msys2.github.io/) and/or [Mingw-w64](http://mingw-w64.org/)

Requirements: bash, make, gcc

	$ pacman -S gcc make  mingw-w64-i686-gcc mingw-w64-x86_64-gcc 
	$ make
	$ make install
	$ make install.all

Alternative toolchain:  `SYSTEM=msys, SYSTEM=msys-win32, SYSTEM=msys-win64`

## glewinfo

`glewinfo` is a command-line tool useful for inspecting the capabilities of an
OpenGL implementation and GLEW support for that.  Please include the output of
`glewinfo` with bug reports, as appropriate.	

	---------------------------
	    GLEW Extension Info
	---------------------------

	GLEW version 2.0.0
	Reporting capabilities of pixelformat 3
	Running on a Intel(R) HD Graphics 3000 from Intel
	OpenGL version 3.1.0 - Build 9.17.10.4229 is supported

	GL_VERSION_1_1:                                                OK
	---------------

	GL_VERSION_1_2:                                                OK
	---------------
	  glCopyTexSubImage3D:                                         OK
	  glDrawRangeElements:                                         OK
	  glTexImage3D:                                                OK
	  glTexSubImage3D:                                             OK
	
	...

## Code Generation

A Unix or Mac environment is neded for building GLEW from scratch to
include new extensions, or customize the code generation. The extension
data is regenerated from the top level source directory with:

	make extensions

An alternative to generating the GLEW sources from scratch is to
download a pre-generated (unsupported) snapshot:

https://sourceforge.net/projects/glew/files/glew/snapshots/

Travis-built snapshots are also available:

https://glew.s3.amazonaws.com/index.html

## Authors

GLEW is currently maintained by [Nigel Stewart](https://github.com/nigels-com)
with bug fixes, new OpenGL extension support and new releases.

GLEW was developed by [Milan Ikits](http://www.cs.utah.edu/~ikits/)
and [Marcelo Magallon](http://wwwvis.informatik.uni-stuttgart.de/~magallon/).
Aaron Lefohn, Joe Kniss, and Chris Wyman were the first users and also
assisted with the design and debugging process.  

The acronym GLEW originates from Aaron Lefohn.
Pasi K&auml;rkk&auml;inen identified and fixed several problems with
GLX and SDL.  Nate Robins created the `wglinfo` utility, to
which modifications were made by Michael Wimmer.  

## Copyright and Licensing

GLEW is originally derived from the EXTGL project by Lev Povalahev.
The source code is licensed under the 
[Modified BSD License](http://glew.sourceforge.net/glew.txt), the 
[Mesa 3-D License](http://glew.sourceforge.net/mesa.txt) (MIT) and the
[Khronos License](http://glew.sourceforge.net/khronos.txt) (MIT).

The automatic code generation scripts are released under the 
[GNU GPL](http://glew.sourceforge.net/gpl.txt).
