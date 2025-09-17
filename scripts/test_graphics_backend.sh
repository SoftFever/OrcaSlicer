#!/bin/bash

# Test script for GraphicsBackendManager functionality
# This script tests the graphics backend detection and configuration

echo "=== OrcaSlicer Graphics Backend Test ==="
echo "Testing graphics backend detection and configuration..."
echo

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to get environment variable safely
get_env_var() {
    local var_name="$1"
    local value="${!var_name}"
    if [ -n "$value" ]; then
        echo "$value"
    else
        echo "not set"
    fi
}

echo "1. Session Type Detection:"
echo "   XDG_SESSION_TYPE: $(get_env_var XDG_SESSION_TYPE)"
echo "   WAYLAND_DISPLAY: $(get_env_var WAYLAND_DISPLAY)"
echo "   DISPLAY: $(get_env_var DISPLAY)"
echo

echo "2. Graphics Driver Detection:"
if command_exists glxinfo; then
    echo "   glxinfo available: YES"
    RENDERER=$(glxinfo 2>/dev/null | grep "OpenGL renderer string:" | sed 's/.*: //' | head -1)
    if [ -n "$RENDERER" ]; then
        echo "   OpenGL Renderer: $RENDERER"
    else
        echo "   OpenGL Renderer: Could not detect"
    fi
else
    echo "   glxinfo available: NO"
fi

if command_exists eglinfo; then
    echo "   eglinfo available: YES"
    EGL_RENDERER=$(eglinfo 2>/dev/null | grep "EGL vendor" | head -1)
    if [ -n "$EGL_RENDERER" ]; then
        echo "   EGL Vendor: $EGL_RENDERER"
    else
        echo "   EGL Vendor: Could not detect"
    fi
else
    echo "   eglinfo available: NO"
fi
echo

echo "3. NVIDIA Driver Detection:"
if command_exists nvidia-smi; then
    echo "   nvidia-smi available: YES"
    DRIVER_VERSION=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -n1)
    if [ -n "$DRIVER_VERSION" ]; then
        echo "   NVIDIA Driver Version: $DRIVER_VERSION"
        DRIVER_MAJOR=$(echo "$DRIVER_VERSION" | cut -d. -f1)
        echo "   Driver Major Version: $DRIVER_MAJOR"
        if [ "$DRIVER_MAJOR" -gt 555 ]; then
            echo "   Status: Newer driver (555+) - Zink recommended"
        else
            echo "   Status: Older driver - DRI backend recommended"
        fi
    else
        echo "   NVIDIA Driver Version: Could not detect"
    fi
else
    echo "   nvidia-smi available: NO"
fi
echo

echo "4. Current Graphics Environment Variables:"
echo "   GBM_BACKEND: $(get_env_var GBM_BACKEND)"
echo "   MESA_LOADER_DRIVER_OVERRIDE: $(get_env_var MESA_LOADER_DRIVER_OVERRIDE)"
echo "   GALLIUM_DRIVER: $(get_env_var GALLIUM_DRIVER)"
echo "   __GLX_VENDOR_LIBRARY_NAME: $(get_env_var __GLX_VENDOR_LIBRARY_NAME)"
echo "   __EGL_VENDOR_LIBRARY_FILENAMES: $(get_env_var __EGL_VENDOR_LIBRARY_FILENAMES)"
echo "   WEBKIT_DISABLE_DMABUF_RENDERER: $(get_env_var WEBKIT_DISABLE_DMABUF_RENDERER)"
echo "   LIBGL_ALWAYS_SOFTWARE: $(get_env_var LIBGL_ALWAYS_SOFTWARE)"
echo "   MESA_GL_VERSION_OVERRIDE: $(get_env_var MESA_GL_VERSION_OVERRIDE)"
echo

echo "5. System Information:"
echo "   OS: $(lsb_release -d 2>/dev/null | cut -f2 || echo "Unknown")"
echo "   Kernel: $(uname -r)"
echo "   Architecture: $(uname -m)"
echo

echo "6. Graphics Backend Test:"
echo "   This test simulates what the GraphicsBackendManager would detect:"

# Simulate session type detection
if [ "$XDG_SESSION_TYPE" = "wayland" ] || [ -n "$WAYLAND_DISPLAY" ]; then
    SESSION_TYPE="Wayland"
elif [ -n "$DISPLAY" ]; then
    SESSION_TYPE="X11"
else
    SESSION_TYPE="Unknown"
fi

# Simulate driver detection
if command_exists glxinfo; then
    GLX_OUTPUT=$(glxinfo 2>/dev/null | grep -E "OpenGL vendor|OpenGL renderer" | head -5)
    if echo "$GLX_OUTPUT" | grep -qi "nvidia"; then
        DETECTED_DRIVER="NVIDIA"
    elif echo "$GLX_OUTPUT" | grep -qi "amd\|radeon"; then
        DETECTED_DRIVER="AMD"
    elif echo "$GLX_OUTPUT" | grep -qi "intel"; then
        DETECTED_DRIVER="Intel"
    elif echo "$GLX_OUTPUT" | grep -qi "mesa"; then
        DETECTED_DRIVER="Mesa"
    else
        DETECTED_DRIVER="Unknown"
    fi
else
    DETECTED_DRIVER="Unknown (glxinfo not available)"
fi

echo "   Detected Session Type: $SESSION_TYPE"
echo "   Detected Graphics Driver: $DETECTED_DRIVER"

# Simulate configuration recommendation
if [ "$DETECTED_DRIVER" = "NVIDIA" ]; then
    if [ "$SESSION_TYPE" = "Wayland" ]; then
        if command_exists nvidia-smi; then
            DRIVER_MAJOR=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -n1 | cut -d. -f1)
            if [ "$DRIVER_MAJOR" -gt 555 ]; then
                RECOMMENDED_CONFIG="NVIDIA Wayland with Zink (newer driver)"
            else
                RECOMMENDED_CONFIG="NVIDIA Wayland with DRI backend (older driver)"
            fi
        else
            RECOMMENDED_CONFIG="NVIDIA Wayland with DRI backend"
        fi
    else
        RECOMMENDED_CONFIG="NVIDIA X11 with native drivers"
    fi
elif [ "$DETECTED_DRIVER" = "AMD" ]; then
    RECOMMENDED_CONFIG="AMD with Mesa radeonsi driver"
elif [ "$DETECTED_DRIVER" = "Intel" ]; then
    RECOMMENDED_CONFIG="Intel with Mesa i965 driver"
elif [ "$DETECTED_DRIVER" = "Mesa" ]; then
    RECOMMENDED_CONFIG="Mesa software rendering"
else
    RECOMMENDED_CONFIG="Fallback DRI configuration"
fi

echo "   Recommended Configuration: $RECOMMENDED_CONFIG"
echo

echo "7. Test Results Summary:"
if [ "$SESSION_TYPE" != "Unknown" ] && [ "$DETECTED_DRIVER" != "Unknown" ]; then
    echo "   ✓ Graphics environment detected successfully"
    echo "   ✓ Driver detection working"
    echo "   ✓ Configuration recommendation generated"
    echo "   Status: READY for automatic configuration"
else
    echo "   ⚠ Graphics environment detection incomplete"
    echo "   ⚠ Some detection methods unavailable"
    echo "   Status: MAY NEED manual configuration"
fi
echo

echo "=== Test Complete ==="
echo "If you see 'READY for automatic configuration', the GraphicsBackendManager"
echo "should work correctly on this system. If you see 'MAY NEED manual configuration',"
echo "the system will use fallback settings." 