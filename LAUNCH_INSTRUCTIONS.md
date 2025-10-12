# 🚀 OrcaSlicer Launch Instructions - READY TO TEST!

## ✅ Build Complete & Verified

Your OrcaSlicer with AI integration has been successfully built and verified:

- **Binary**: `./build/src/Release/orca-slicer` (118MB)
- **AI Integration**: ✅ Compiled and linked
- **Dependencies**: ✅ All found, no missing libraries
- **VNC Server**: ✅ Running and ready

---

## 🖥️ Method 1: Access VNC Desktop (EASIEST)

### Step 1: Find the Port Forwarding URL

1. In VS Code, click the **"PORTS"** tab (bottom panel, next to Terminal)
2. Look for port **6080** (noVNC web interface)
3. You should see something like:
   ```
   Port    | Local Address      | Running Process
   6080    | localhost:6080     | noVNC
   ```
4. **Hover over the port** and click the **🌐 globe icon** or the "Open in Browser" icon
5. OR copy the forwarded URL (might look like: `https://scaling-telegram-xxxxx.app.github.dev`)

### Step 2: Open in Browser

- Your browser will open showing a **Fluxbox desktop**
- You'll see a gray/blue desktop with a minimal window manager

### Step 3: Open Terminal in VNC Desktop

- **Right-click** on the desktop
- Select **"Terminal"** or **"Applications → Terminal"**
- A terminal window will open

### Step 4: Launch OrcaSlicer

In the VNC terminal, run:
```bash
cd /workspaces/SlicerGPT
./launch_orcaslicer.sh
```

**OR directly:**
```bash
cd /workspaces/SlicerGPT
export DISPLAY=:1
./build/src/Release/orca-slicer
```

🎉 **OrcaSlicer should launch in the VNC desktop!**

---

## 🧪 Method 2: Quick Test from VS Code Terminal

You can also launch directly from VS Code terminal (it will appear in VNC):

```bash
cd /workspaces/SlicerGPT
export DISPLAY=:1
./launch_orcaslicer.sh
```

Then switch to your browser with the VNC desktop to see it running.

---

## ✅ Testing AI Mode

Once OrcaSlicer is running:

### 1. Enable AI Mode
- Go to **Edit → Preferences**
- Click **"Other"** tab
- Find **"Enable AI Mode (Auto Slice)"**
- ✅ **Check the box**
- Click **OK**
- May need to restart OrcaSlicer

### 2. Load a Test Model
- **File → Import → STL** (or drag & drop)
- Try one from the samples:
  ```bash
  # In the file picker, navigate to:
  /workspaces/SlicerGPT/resources/handy_models/
  ```
- Or load any STL file

### 3. Slice and Watch AI Work
- Click the **"Slice"** button
- Watch the console/terminal for AI messages:
  ```
  AI Mode: Analyzing geometry and optimizing parameters...
  Geometry: width=100.0mm, height=50.0mm, depth=80.0mm
  Overhangs detected: 15.3%
  Printer: Cartesian bed slinger
  Material: PLA
  Optimized layer height: 0.2mm
  Optimized print speed: 50mm/s
  Support structures: ENABLED
  ```

### 4. Verify Parameter Changes
- Check the **Print Settings** panel
- Parameters should be automatically optimized:
  - ✅ Layer height adjusted for detail
  - ✅ Print speed adjusted for kinematics
  - ✅ Temperature set for material
  - ✅ Support enabled/disabled based on geometry
  - ✅ Infill density adjusted

### 5. Compare with AI Off
- Disable AI Mode in Preferences
- Reload the same model
- Slice again
- Notice parameters are NOT auto-adjusted (manual mode)

---

## 🔍 Troubleshooting

### Can't See Port 6080
```bash
# Check if noVNC is running
ps aux | grep noVNC

# Should show process on port 6080
netstat -tlnp | grep 6080
```

### "Cannot Open Display"
```bash
# Set the display variable
export DISPLAY=:1

# Test display access
xdpyinfo | head -5
```

### OrcaSlicer Won't Start
```bash
# Check for errors
./build/src/Release/orca-slicer 2>&1 | tee launch.log

# Check library dependencies
ldd ./build/src/Release/orca-slicer | grep "not found"
```

### AI Mode Not Working
1. Check Preferences → Other → "Enable AI Mode" is checked
2. Restart OrcaSlicer after enabling
3. Check console output for AI messages
4. Verify AI code compiled:
   ```bash
   nm ./build/src/libslic3r/Release/liblibslic3r.a | grep GeometryAnalyzer
   ```

---

## 📊 What to Look For - AI Mode Success Indicators

### Console Output
```
✅ "AI Mode: Analyzing geometry..."
✅ "Detected bed slinger kinematics..."
✅ "Material: [MATERIAL_NAME], recommended temperature: [TEMP]°C"
✅ "Optimized layer height: [HEIGHT]mm"
✅ "Support structures: ENABLED/DISABLED"
```

### Parameter Changes
- **Layer Height**: Adjusted based on model detail (0.1-0.3mm)
- **Print Speed**: Adjusted for kinematics (25-60mm/s)
- **Temperature**: Material-specific (180-270°C)
- **Support**: Auto-enabled if overhangs > 45°
- **Infill**: Scaled with part volume (15-30%)
- **Wall Count**: Maintains ~1.2mm thickness

### No AI Messages = Problem
If you don't see "AI Mode: Analyzing..." messages:
1. AI Mode might not be enabled in Preferences
2. Check the checkbox and restart
3. Look for errors in console

---

## 🎯 Quick Command Summary

### Launch OrcaSlicer (from VS Code terminal):
```bash
cd /workspaces/SlicerGPT
export DISPLAY=:1
./launch_orcaslicer.sh
```

### Access VNC Desktop:
- VS Code → PORTS tab → Port 6080 → Click globe icon 🌐
- Open in browser to see the desktop

### Verify Build:
```bash
# Check binary
ls -lh ./build/src/Release/orca-slicer

# Check AI code
nm ./build/src/libslic3r/Release/liblibslic3r.a | grep GeometryAnalyzer

# Check dependencies
ldd ./build/src/Release/orca-slicer | grep "not found"
```

---

## 🎉 Success Checklist

- [ ] VNC desktop accessible in browser (port 6080)
- [ ] OrcaSlicer launches without errors
- [ ] AI Mode checkbox in Preferences → Other
- [ ] Model loads successfully (STL/3MF)
- [ ] Slice button works
- [ ] Console shows "AI Mode: Analyzing..." messages
- [ ] Parameters auto-adjust based on geometry
- [ ] G-code generates successfully

---

## 📸 Expected Workflow

1. **Browser** → Open port 6080 → See Fluxbox desktop
2. **Right-click** desktop → Terminal
3. **Terminal** → Run `./launch_orcaslicer.sh`
4. **OrcaSlicer** → Opens in VNC desktop
5. **Edit** → Preferences → Enable AI Mode ✅
6. **File** → Import → Load STL
7. **Slice** → Watch AI optimize parameters
8. **Success!** 🎉

---

## 🚀 You're Ready to Test!

Your AI-powered OrcaSlicer is built and ready. Just:
1. Open port 6080 in your browser
2. Launch OrcaSlicer in the VNC desktop
3. Enable AI Mode and test with a 3D model

**Good luck! Your AI integration is in there and ready to optimize!** 🤖✨
