#!/bin/bash

# Test script for GraphicsBackendManager in various environments
# This script simulates different container and graphics environments

echo "=== Testing GraphicsBackendManager ==="
echo

# Function to test with specific environment variables
test_environment() {
    local desc="$1"
    shift
    echo "Testing: $desc"
    echo "Environment:"
    for var in "$@"; do
        echo "  $var"
        export $var
    done
    
    # Build and run test
    if [ -f build/src/orca-slicer ]; then
        echo "Running OrcaSlicer with graphics backend detection..."
        timeout 5 build/src/orca-slicer --help 2>&1 | grep -i "GraphicsBackendManager" | head -10
    else
        echo "OrcaSlicer binary not found, please build first"
    fi
    
    # Clean environment
    for var in "$@"; do
        unset ${var%%=*}
    done
    echo
}

# Test 1: Flatpak with Wayland
test_environment "Flatpak with Wayland and NVIDIA" \
    "FLATPAK_ID=io.github.softfever.OrcaSlicer" \
    "XDG_SESSION_TYPE=wayland" \
    "WAYLAND_DISPLAY=wayland-0" \
    "__GLX_VENDOR_LIBRARY_NAME=nvidia"

# Test 2: Flatpak with X11
test_environment "Flatpak with X11 and NVIDIA" \
    "FLATPAK_ID=io.github.softfever.OrcaSlicer" \
    "XDG_SESSION_TYPE=x11" \
    "DISPLAY=:0" \
    "__GLX_VENDOR_LIBRARY_NAME=nvidia"

# Test 3: Flatpak with Wayland and Mesa
test_environment "Flatpak with Wayland and Mesa" \
    "FLATPAK_ID=io.github.softfever.OrcaSlicer" \
    "XDG_SESSION_TYPE=wayland" \
    "WAYLAND_DISPLAY=wayland-0" \
    "__GLX_VENDOR_LIBRARY_NAME=mesa"

# Test 4: Regular system (no Flatpak)
test_environment "Regular system with Wayland" \
    "XDG_SESSION_TYPE=wayland" \
    "WAYLAND_DISPLAY=wayland-0"

echo "=== Test complete ==="
echo
echo "To verify the fix works in actual Flatpak:"
echo "1. Build the Flatpak package:"
echo "   cd scripts/flatpak"
echo "   flatpak-builder --force-clean build-dir io.github.softfever.OrcaSlicer.yml"
echo ""
echo "2. Test the package:"
echo "   flatpak-builder --run build-dir io.github.softfever.OrcaSlicer.yml orca-slicer"
echo ""
echo "3. Check logs for GraphicsBackendManager messages"