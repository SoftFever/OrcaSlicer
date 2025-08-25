# Fix for Flatpak on Wayland with NVIDIA Drivers

## Problem
The graphics backend automation system was failing to detect NVIDIA drivers correctly when running OrcaSlicer in Flatpak on Wayland systems. This resulted in the 3D view not working properly.

## Root Causes
1. **Overly restrictive fallback detection**: The code was preventing fallback detection methods (glxinfo/eglinfo) from running in containers, assuming they wouldn't work.
2. **Missing Flatpak-specific paths**: NVIDIA libraries in Flatpak are exposed through runtime extensions at different paths than on the host system.
3. **Inadequate EGL detection for Wayland**: The EGL detection wasn't properly handling Wayland platform displays in Flatpak.
4. **Missing container-specific configuration**: The system wasn't applying Flatpak-specific settings when needed.

## Fixes Applied

### 1. Allow Fallback Detection in Containers
**File**: `src/slic3r/GUI/GraphicsBackendManager.cpp`

Removed the restriction that prevented glxinfo/eglinfo fallback commands from running in containers. These commands might actually work in Flatpak since it has access to system graphics through runtime extensions.

```cpp
// Before:
if (is_running_in_container()) {
    return "";  // Prevented fallback detection
}

// After:
// Always try fallback, log if it succeeds in container
std::string glxinfo_result = execute_command("glxinfo ...");
```

### 2. Enhanced Flatpak-Specific Detection
Added checks for:
- NVIDIA libraries in Flatpak runtime paths (`/run/host/usr/lib*`)
- Flatpak environment variables (`FLATPAK_GL_DRIVERS`)
- nvidia-smi availability within Flatpak sandbox

### 3. Improved Wayland EGL Detection
Enhanced EGL detection to use Wayland platform display when available:
```cpp
// Try to get Wayland display if we're on Wayland
const char* wayland_display_env = std::getenv("WAYLAND_DISPLAY");
if (wayland_display_env) {
    // Use eglGetPlatformDisplay for Wayland
    egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ...);
}
```

### 4. Flatpak-Specific Configuration
Added special handling for Flatpak environments:
- Always ensure `GBM_BACKEND=dri` is set
- Disable DMABUF renderer for Wayland sessions
- Check multiple paths for EGL vendor files
- Apply safe defaults for unknown drivers

### 5. Enhanced Logging
Added detailed logging for:
- Flatpak environment detection
- Graphics driver detection methods and results
- Configuration being applied

## Testing

### Manual Testing
1. Build OrcaSlicer normally:
   ```bash
   ./build_linux.sh -dsi
   ```

2. Test with simulated Flatpak environment:
   ```bash
   chmod +x test_graphics_backend.sh
   ./test_graphics_backend.sh
   ```

### Flatpak Testing
1. Build the Flatpak package:
   ```bash
   cd scripts/flatpak
   flatpak-builder --force-clean build-dir io.github.softfever.OrcaSlicer.yml
   ```

2. Run and check logs:
   ```bash
   flatpak-builder --run build-dir io.github.softfever.OrcaSlicer.yml orca-slicer 2>&1 | grep GraphicsBackendManager
   ```

## Expected Behavior After Fix

When running in Flatpak on Wayland with NVIDIA drivers:

1. **Detection**: System should detect:
   - "Running in Flatpak: io.github.softfever.OrcaSlicer"
   - "Flatpak with Wayland session detected"
   - "Detected NVIDIA driver via [method]"

2. **Configuration Applied**:
   - `GBM_BACKEND=dri`
   - `WEBKIT_DISABLE_DMABUF_RENDERER=1`
   - For newer NVIDIA drivers (555+): Zink renderer configuration
   - For older NVIDIA drivers: DRI backend configuration

3. **3D View**: Should work correctly without manual environment variable configuration

## Verification

Check the logs for these key messages:
```
GraphicsBackendManager: Running in Flatpak: io.github.softfever.OrcaSlicer
GraphicsBackendManager: Flatpak with Wayland session detected
GraphicsBackendManager: Detected NVIDIA driver via [detection method]
GraphicsBackendManager: Forcing GBM_BACKEND=dri for Flatpak environment
GraphicsBackendManager: Configuration applied successfully
```

## Related Issues
- Flatpak NVIDIA GL workaround: https://gitlab.gnome.org/GNOME/gnome-build-meta/-/issues/754
- BambuStudio issue: https://github.com/bambulab/BambuStudio/issues/3440

## Future Improvements
1. Add support for detecting NVIDIA driver version through Flatpak portal APIs
2. Implement configuration persistence for tested working configurations
3. Add GUI option to manually override graphics backend settings
4. Extend detection to support more container runtimes (Snap, Docker)