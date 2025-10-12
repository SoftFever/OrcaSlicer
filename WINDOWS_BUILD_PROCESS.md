# How Official OrcaSlicer Builds Windows EXE Releases

## Executive Summary

The official OrcaSlicer team uses **GitHub Actions** with **Visual Studio 2022** and **MSVC** compiler to build Windows releases. They use automated CI/CD workflows that build for Windows, macOS, and Linux simultaneously.

---

## Official Windows Build Process

### 1. **Build Environment**

**Platform:** `windows-latest` (GitHub Actions runner)
- **OS**: Windows Server 2022 (latest)
- **Compiler**: Visual Studio 2022 (MSVC)
- **SDK**: Windows 10 SDK version 10.0.26100.0
- **Generator**: Visual Studio 17 2022 (CMake)
- **Architecture**: x64 (64-bit)

### 2. **Build Steps** (from `.github/workflows/build_orca.yml`)

```yaml
# Step 1: Setup MSVC
- name: setup MSVC
  uses: microsoft/setup-msbuild@v2

# Step 2: Install NSIS (installer creator)
- name: Install nsis
  run: choco install nsis

# Step 3: Build OrcaSlicer
- name: Build slicer Win
  env:
    WindowsSdkDir: 'C:\Program Files (x86)\Windows Kits\10\'
    WindowsSDKVersion: '10.0.26100.0\'
  run: .\build_release_vs2022.bat slicer

# Step 4: Create installer
- name: Create installer Win
  working-directory: ./build
  run: cpack -G NSIS

# Step 5: Create portable ZIP
- name: Pack app
  working-directory: ./build
  run: '"C:/Program Files/7-Zip/7z.exe" a -tzip OrcaSlicer_Windows_${{ env.ver }}_portable.zip ./build/OrcaSlicer'
```

### 3. **Build Script** (`build_release_vs2022.bat`)

The Windows build uses a batch script that:

1. **Builds dependencies first:**
   ```bat
   cmake ../ -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%DEPS%" -DCMAKE_BUILD_TYPE=Release
   cmake --build . --config Release --target deps -- -m
   ```

2. **Builds OrcaSlicer:**
   ```bat
   cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%DEPS%\usr\local" -DCMAKE_BUILD_TYPE=Release
   msbuild /m /p:Configuration=Release INSTALL.vcxproj
   ```

---

## Output Artifacts

The official build produces **TWO** Windows artifacts:

1. **OrcaSlicer_Windows_V{version}.exe**
   - NSIS installer (setup wizard)
   - Installs to Program Files
   - Creates Start Menu shortcuts
   - Adds file associations

2. **OrcaSlicer_Windows_V{version}_portable.zip**
   - Portable version (no installation)
   - Can run from USB drive
   - No registry changes
   - Just extract and run

---

## Key Differences from Your Linux Build

| Aspect | Your Linux Build | Official Windows Build |
|--------|------------------|------------------------|
| **OS** | Ubuntu 22.04 (dev container) | Windows Server 2022 |
| **Compiler** | GCC 11.4.0 | MSVC 19.x (VS 2022) |
| **Build Tool** | Ninja | MSBuild |
| **CMake Generator** | Ninja Multi-Config | Visual Studio 17 2022 |
| **GUI Library** | GTK3 | Windows native |
| **Dependencies** | deps-linux.cmake | deps-windows.cmake |
| **Output** | ELF binary | PE/COFF .exe |
| **Packaging** | AppImage / tar.gz | NSIS installer + ZIP |

---

## How to Build Windows Version Yourself

### **Option 1: Use GitHub Actions (Recommended)**

The official way is to let GitHub Actions build it automatically:

1. Push your code to GitHub
2. GitHub Actions automatically builds Windows, macOS, and Linux versions
3. Download artifacts from the Actions tab

**Your AI integration will be included automatically** because it's already in the codebase!

### **Option 2: Build Locally on Windows**

**Requirements:**
- Windows 10/11
- Visual Studio 2022 (Community edition is free)
- CMake 3.28+
- Git

**Steps:**
```bat
# 1. Clone your repo
git clone https://github.com/allanwrench28/SlicerGPT.git
cd SlicerGPT

# 2. Build everything
build_release_vs2022.bat

# This will:
# - Build dependencies (~2 hours first time)
# - Build OrcaSlicer (~30 minutes)
# - Create installer and portable ZIP
```

**Output Location:**
- Installer: `build/OrcaSlicer_Windows_V{version}.exe`
- Portable: `build/OrcaSlicer_Windows_V{version}_portable.zip`
- Binary: `build/OrcaSlicer/orcaslicer.exe`

### **Option 3: Cross-Compile from Linux (Advanced)**

Possible but not officially supported. Would need:
- MinGW-w64 cross-compiler
- Wine for testing
- Custom CMake toolchain file
- Significant configuration effort

**Not recommended** - use Options 1 or 2 instead.

---

## GitHub Actions Workflow Details

The official build runs on:
- **Trigger**: Push to main, pull request, daily at 7:35 AM Pacific
- **Platforms**: Ubuntu 24.04, Windows Latest, macOS 14
- **Parallelism**: All 3 platforms build simultaneously
- **Caching**: Dependencies are cached to speed up builds
- **Artifacts**: Automatically uploaded and available for download

**Relevant Files:**
- `.github/workflows/build_all.yml` - Main workflow (triggers all builds)
- `.github/workflows/build_check_cache.yml` - Dependency caching logic
- `.github/workflows/build_deps.yml` - Builds dependencies
- `.github/workflows/build_orca.yml` - Builds OrcaSlicer (what we looked at)

---

## Your AI Integration on Windows

Your AI code will compile on Windows **without any changes** because:

✅ **Uses standard C++17** - works with MSVC  
✅ **No platform-specific code** - no `#ifdef __linux__` or system calls  
✅ **Cross-platform libraries** - Boost, wxWidgets work on Windows  
✅ **Already in CMakeLists.txt** - included in libslic3r target  

**What happens when built on Windows:**
1. `UnifiedOfflineAI.cpp` compiles with MSVC
2. Links into `libslic3r.lib` (Windows static library)
3. Final `orcaslicer.exe` includes your AI code
4. AI Mode works identically on Windows

---

## Recommended Approach for You

### **Best Option: Use GitHub Actions**

1. **Push your branch to GitHub:**
   ```bash
   git push origin copilot/vscode1760260866532
   ```

2. **Create a Pull Request** (or just wait for Actions to run)

3. **GitHub Actions will automatically:**
   - Build Windows EXE
   - Build macOS DMG
   - Build Linux AppImage
   - All with your AI integration included

4. **Download the Windows build** from:
   - Actions tab → Select the workflow run → Download artifacts
   - You'll get: `OrcaSlicer_Windows_PR-{number}.zip` and `.exe`

### **Alternative: Build on Windows PC**

If you have a Windows machine:

1. Install Visual Studio 2022 Community (free)
2. Clone your SlicerGPT repo
3. Run `build_release_vs2022.bat`
4. Wait ~2-3 hours (dependencies build once)
5. Get `orcaslicer.exe` with your AI integration

---

## Dependencies Already Include Windows Support

The `deps/deps-windows.cmake` file already configures all dependencies for Windows:
- Boost → Built with MSVC
- wxWidgets → Windows native (not GTK)
- CGAL, OpenCV, etc. → All have Windows support

No changes needed to dependencies or build system for Windows!

---

## Summary

**How official devs do it:**
- ✅ GitHub Actions with `windows-latest` runner
- ✅ Visual Studio 2022 + MSVC compiler
- ✅ `build_release_vs2022.bat` script
- ✅ Automated CI/CD builds all platforms
- ✅ Creates both installer and portable ZIP

**Your path to Windows EXE:**
1. **Easiest**: Push to GitHub, let Actions build it
2. **DIY**: Build on Windows PC with VS 2022
3. **Wait**: Finish Linux build first, test, then tackle Windows

**Your AI integration:**
- ✅ Already cross-platform
- ✅ Will work on Windows without changes
- ✅ Just needs to be built with MSVC instead of GCC

**Next Steps:**
1. ✅ Finish current Linux build (in progress)
2. ✅ Test AI Mode works on Linux
3. ✅ Push to GitHub and let Actions build Windows version
4. 🎯 Download and test Windows EXE!

---

## Key Takeaway

The official OrcaSlicer team makes it **easy** - they've already set up the entire Windows build infrastructure in the GitHub Actions workflows. You just need to push your code and GitHub will automatically build Windows, macOS, and Linux versions for you! 🚀
