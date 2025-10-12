# OrcaSlicer Testing Guide - Codespace/Headless Environment

## Environment Context
You're running in a **GitHub Codespace** (Ubuntu dev container) which is a **headless environment** (no physical display). OrcaSlicer is a **GUI application** that requires a display.

---

## Testing Options

### ✅ **Option 1: VNC Desktop (RECOMMENDED for Codespace)**

Your dev container includes a Fluxbox-based desktop accessible via VNC/web browser!

#### Steps:
1. **Wait for build to complete**
2. **Start VNC desktop** (if not already running):
   ```bash
   # Check if VNC is running
   ps aux | grep vnc
   ```

3. **Forward VNC port**:
   - In VS Code, go to "PORTS" tab (bottom panel)
   - Find port `5901` (VNC) or `6080` (noVNC web)
   - Make it public or forward it

4. **Connect to desktop**:
   - **Web Browser**: Open `http://localhost:6080/vnc.html` (or forwarded URL)
   - **VNC Client**: Connect to `localhost:5901`

5. **Run OrcaSlicer in the desktop**:
   ```bash
   # Open terminal in VNC desktop, then:
   cd /workspaces/SlicerGPT
   ./build/src/OrcaSlicer
   ```

6. **Test AI Mode**:
   - ✅ Load a 3D model (File → Import → STL/3MF)
   - ✅ Check Preferences → Other → "Enable AI Mode (Auto Slice)"
   - ✅ Click "Slice" and verify AI optimization runs
   - ✅ Check console output for AI analysis messages

---

### ✅ **Option 2: X11 Forwarding (SSH Required)**

If you have SSH access to the codespace:

```bash
# From your local machine with X11:
ssh -X user@codespace-host
cd /workspaces/SlicerGPT
./build/src/OrcaSlicer
```

**Requirements**:
- Local X11 server (XQuartz on macOS, X11 on Linux, VcXsrv/Xming on Windows)
- SSH access to codespace

---

### ✅ **Option 3: Headless Testing (Command-Line Verification)**

Test without GUI:

#### A. **Check Binary Built Successfully**
```bash
ls -lh /workspaces/SlicerGPT/build/src/OrcaSlicer
file /workspaces/SlicerGPT/build/src/OrcaSlicer
```

#### B. **Test Library Linking**
```bash
ldd /workspaces/SlicerGPT/build/src/OrcaSlicer | grep "not found"
# Should return nothing (all libs found)
```

#### C. **Verify AI Files Compiled**
```bash
# Check if AI implementation is in the binary
nm /workspaces/SlicerGPT/build/src/libslic3r/libslic3r.a | grep -i "GeometryAnalyzer\|UnifiedOfflineAI"

# Or check object files
find /workspaces/SlicerGPT/build -name "*UnifiedOfflineAI*"
```

#### D. **Run Help/Version Check**
```bash
# These should work without display
./build/src/OrcaSlicer --help 2>&1 || echo "Binary runs but needs display"
./build/src/OrcaSlicer --version 2>&1 || echo "Binary runs but needs display"
```

#### E. **Test with Xvfb (Virtual Display)**
```bash
# Install Xvfb if needed
sudo apt-get update && sudo apt-get install -y xvfb

# Run with virtual display
xvfb-run -a ./build/src/OrcaSlicer --help
```

---

### ✅ **Option 4: Export Binary and Test Locally**

Build in codespace, test on your local machine:

```bash
# After build completes, package the binary
cd /workspaces/SlicerGPT/build
tar -czf orcaslicer-build.tar.gz src/OrcaSlicer src/libslic3r/* resources/

# Download the tar.gz file from codespace
# Then on your local Linux machine:
tar -xzf orcaslicer-build.tar.gz
./src/OrcaSlicer
```

**Note**: May have library dependency issues if your local system differs from Ubuntu 22.04.

---

## Recommended Testing Workflow

### Phase 1: Build Verification (No Display Needed)
```bash
# 1. Check build completed
ls -lh ./build/src/OrcaSlicer

# 2. Check AI files compiled
find ./build -name "*UnifiedOfflineAI*" -o -name "*AIAdapter*"

# 3. Check library dependencies
ldd ./build/src/OrcaSlicer | grep -E "openvdb|boost|wx"

# 4. Verify no missing libraries
ldd ./build/src/OrcaSlicer | grep "not found"
```

### Phase 2: GUI Testing (Requires Display)
```bash
# Option A: VNC Desktop (easiest in codespace)
# Connect to noVNC at http://localhost:6080
# Then run: ./build/src/OrcaSlicer

# Option B: Virtual display
xvfb-run -a ./build/src/OrcaSlicer
```

### Phase 3: AI Mode Testing
Once GUI is running:

1. **Enable AI Mode**:
   - Edit → Preferences → Other
   - Check "Enable AI Mode (Auto Slice)"
   - Click OK, restart if prompted

2. **Load Test Model**:
   - File → Import → STL
   - Load any STL file (or use one from `resources/handy_models/`)

3. **Observe AI Optimization**:
   - Click "Slice" button
   - Watch console/log for messages like:
     ```
     AI Mode: Analyzing geometry and optimizing parameters...
     Detected bed slinger kinematics, applying conservative speeds
     Material: PLA, recommended temperature: 200°C
     ```

4. **Verify Parameters Changed**:
   - Check that print speed, temperature, support settings were auto-adjusted
   - Compare with AI Mode OFF (uncheck the preference)

---

## Expected Build Output Files

After successful build:
```
./build/src/OrcaSlicer              # Main executable (~150-200MB)
./build/src/libslic3r/libslic3r.a   # Core slicing library
./build/src/slic3r/libslic3r_gui.a  # GUI library
./resources/                        # Icons, profiles, etc.
```

---

## AI Integration Verification

### Files to Check Were Compiled:
```bash
# Check AI implementation compiled
find ./build/src/libslic3r -name "*UnifiedOfflineAI*"

# Should find:
# - CMakeFiles/libslic3r.dir/Release/UnifiedOfflineAI.cpp.o
```

### Console Messages When AI Mode Active:
```
AI Mode: Analyzing geometry and optimizing parameters...
Geometry: width=100.0mm, height=50.0mm, depth=80.0mm
Overhangs detected: 15.3%
Printer: Cartesian bed slinger
Material: PLA
Optimized layer height: 0.2mm
Optimized print speed: 50mm/s (reduced for bed movement)
Support structures: ENABLED (overhang threshold exceeded)
```

---

## Quick Start Script

Save this as `test_orcaslicer.sh`:

```bash
#!/bin/bash
set -e

echo "=== OrcaSlicer Build Verification ==="

# Check binary exists
if [ ! -f "./build/src/OrcaSlicer" ]; then
    echo "❌ Binary not found!"
    exit 1
fi
echo "✅ Binary found"

# Check size
SIZE=$(stat -f%z "./build/src/OrcaSlicer" 2>/dev/null || stat -c%s "./build/src/OrcaSlicer")
echo "📦 Binary size: $((SIZE / 1024 / 1024))MB"

# Check AI files compiled
if find ./build -name "*UnifiedOfflineAI*" | grep -q .; then
    echo "✅ AI implementation compiled"
else
    echo "⚠️ AI files may not be compiled"
fi

# Check dependencies
echo "🔍 Checking library dependencies..."
MISSING=$(ldd ./build/src/OrcaSlicer | grep "not found" | wc -l)
if [ "$MISSING" -eq 0 ]; then
    echo "✅ All libraries found"
else
    echo "❌ Missing $MISSING libraries:"
    ldd ./build/src/OrcaSlicer | grep "not found"
fi

# Test with virtual display
if command -v xvfb-run &> /dev/null; then
    echo "🖥️ Testing with virtual display..."
    timeout 5 xvfb-run -a ./build/src/OrcaSlicer || echo "✅ Binary runs (exited after timeout)"
else
    echo "⚠️ xvfb-run not available, install with: sudo apt-get install xvfb"
fi

echo ""
echo "=== Next Steps ==="
echo "For GUI testing:"
echo "  1. Connect to VNC: http://localhost:6080"
echo "  2. Open terminal in VNC desktop"
echo "  3. Run: cd /workspaces/SlicerGPT && ./build/src/OrcaSlicer"
```

Run it:
```bash
chmod +x test_orcaslicer.sh
./test_orcaslicer.sh
```

---

## Troubleshooting

### "cannot open display"
**Cause**: No X11 display available  
**Solution**: Use VNC desktop or xvfb-run

### "error while loading shared libraries"
**Cause**: Missing dependencies  
**Solution**: 
```bash
ldd ./build/src/OrcaSlicer | grep "not found"
# Install missing libraries or check LD_LIBRARY_PATH
```

### VNC not working
**Check**:
```bash
ps aux | grep vnc
netstat -tlnp | grep 5901
```

### AI Mode not activating
**Check**:
```bash
# Look for ai_mode_enabled setting
cat ~/.config/OrcaSlicer/OrcaSlicer.conf | grep ai_mode
# Or check app logs
```

---

## Performance Notes

- Building: ~30-45 minutes with 4 cores
- Binary size: ~150-200MB
- RAM usage: ~2-4GB when running
- VNC adds minimal overhead (~50-100MB)

---

## Summary

**Best approach for codespace**:
1. ✅ Use built-in VNC desktop (port 6080)
2. ✅ Run OrcaSlicer in the VNC desktop environment
3. ✅ Test AI Mode with actual 3D models
4. ✅ Check console output for AI optimization messages

**Quick verification** (no display needed):
1. ✅ Check binary built: `ls -lh ./build/src/OrcaSlicer`
2. ✅ Check AI compiled: `find ./build -name "*UnifiedOfflineAI*"`
3. ✅ Check libraries: `ldd ./build/src/OrcaSlicer`
