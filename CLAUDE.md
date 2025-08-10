# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

OrcaSlicer is an open-source 3D slicer application forked from Bambu Studio, built using C++ with wxWidgets for the GUI and CMake as the build system. The project uses a modular architecture with separate libraries for core slicing functionality, GUI components, and platform-specific code.

## Build Commands

### Building on macOS
```bash
# Build everything (dependencies and slicer)
./build_release_macos.sh

# Build only dependencies
./build_release_macos.sh -d

# Build only slicer (after deps are built)
./build_release_macos.sh -s

# Use Ninja generator for faster builds
./build_release_macos.sh -x

# Build for specific architecture
./build_release_macos.sh -a arm64    # or x86_64
```

### Building on Linux
```bash
# Build everything
./build_linux.sh

# Check system resources first (build requires 10GB+ RAM and disk)
./build_linux.sh -r    # skip resource checks
```

### Build System
- Uses CMake with minimum version 3.13
- Primary build directory: `build/`
- Dependencies are built in `deps/build/`
- The build process is split into dependency building and main application building

### Testing
Tests are located in the `tests/` directory and can be run via CMake/CTest after building.

## Architecture

### Core Libraries
- **libslic3r/**: Core slicing engine and algorithms
  - Contains the main slicing logic, geometry processing, G-code generation
  - Platform-independent slicing functionality
  - Key classes: Print, PrintObject, Layer, GCode, Config

- **src/slic3r/**: Main application framework
  - GUI application entry points and main loops
  - Integration between libslic3r and the GUI

### GUI Components
- Built with wxWidgets framework
- Located primarily in `src/slic3r/GUI/`
- Main application class: `GUI_App`
- Uses OpenGL for 3D visualization

### Key Modules
- **3MF Format Support**: `src/libslic3r/Format/3mf.cpp` - 3MF file format reading/writing
- **G-code Processing**: `src/libslic3r/GCode/` - G-code generation and processing
- **Geometry**: `src/libslic3r/Geometry.cpp` - 2D/3D geometry operations
- **Print Configuration**: `src/libslic3r/PrintConfig.cpp` - Print settings and presets
- **Model Handling**: `src/libslic3r/Model.cpp` - 3D model representation and manipulation

### External Dependencies
- **Clipper**: 2D polygon clipping operations
- **libigl**: Geometry processing library
- **OpenVDB**: Voxel data structures (optional)
- **TBB**: Threading Building Blocks for parallelization
- **wxWidgets**: Cross-platform GUI toolkit
- **OpenGL**: 3D graphics rendering

## File Organization

### Configuration and Profiles
- `resources/profiles/`: Printer and material profiles organized by manufacturer
- `resources/printers/`: Printer-specific configurations and G-code templates

### Internationalization
- `localization/i18n/`: Translation files
- `resources/i18n/`: Runtime language resources

### Platform-Specific Code
- Platform abstractions in `src/libslic3r/Platform.cpp`
- macOS-specific utilities in `src/libslic3r/MacUtils.mm`

## Development Workflow

### Code Style
- C++17 standard
- Use existing patterns found in libslic3r for consistency
- Follow existing naming conventions (PascalCase for classes, snake_case for functions)

### Common Tasks
- **Adding new print settings**: Extend `PrintConfig.cpp` and related GUI components
- **Modifying slicing algorithms**: Work in `libslic3r/` core modules
- **GUI changes**: Modify components in `src/slic3r/GUI/`
- **Adding printer support**: Add profiles in `resources/profiles/`

### Dependencies Management
Dependencies are managed through CMake and built separately from the main application. The `deps/` directory contains dependency build configurations.

## Important Notes

- The codebase is large and complex - use search tools to navigate effectively
- Many algorithms are performance-critical - consider computational complexity
- The project maintains backward compatibility with various file formats
- Cross-platform compatibility is important (Windows, macOS, Linux)
- The GUI uses a custom theming system supporting light/dark modes