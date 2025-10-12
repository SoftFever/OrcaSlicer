#!/bin/bash
# OrcaSlicer Launcher Script
# Launches OrcaSlicer with proper environment setup

set -e

echo "=== OrcaSlicer Launcher ==="
echo ""

# Set display if not already set
if [ -z "$DISPLAY" ]; then
    export DISPLAY=:1
    echo "✅ Set DISPLAY=:1"
fi

# Check if we're in the right directory
if [ ! -f "./build/src/Release/orca-slicer" ]; then
    echo "❌ Error: orca-slicer not found!"
    echo "Please run this from /workspaces/SlicerGPT"
    exit 1
fi

echo "✅ Binary found: $(ls -lh ./build/src/Release/orca-slicer | awk '{print $5}')"
echo ""
echo "🚀 Launching OrcaSlicer with AI Mode enabled..."
echo ""
echo "Tips:"
echo "  - Check Edit → Preferences → Other → 'Enable AI Mode'"
echo "  - Load a model: File → Import → STL"
echo "  - Click 'Slice' to see AI optimization"
echo ""

# Launch OrcaSlicer
cd /workspaces/SlicerGPT
./build/src/Release/orca-slicer "$@"
