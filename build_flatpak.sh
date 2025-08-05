#!/bin/bash

# OrcaSlicer Flatpak Build Script
# This script builds and packages OrcaSlicer as a Flatpak package locally
# Based on the GitHub Actions workflow in .github/workflows/build_all.yml

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
ARCH=$(uname -m)
BUILD_DIR="flatpak-build"
CLEANUP=false
INSTALL_RUNTIME=false

# Help function
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Build OrcaSlicer as a Flatpak package"
    echo ""
    echo "Options:"
    echo "  -a, --arch ARCH        Target architecture (x86_64, aarch64) [default: $ARCH]"
    echo "  -d, --build-dir DIR    Build directory [default: $BUILD_DIR]"
    echo "  -c, --cleanup          Clean build directory before building"
    echo "  -i, --install-runtime  Install required Flatpak runtime and SDK"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                     # Build for current architecture"
    echo "  $0 -a x86_64 -c       # Build for x86_64 and cleanup first"
    echo "  $0 -i                  # Install runtime and build"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -d|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -c|--cleanup)
            CLEANUP=true
            shift
            ;;
        -i|--install-runtime)
            INSTALL_RUNTIME=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo -e "${RED}Error: Unknown option $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

# Validate architecture
if [[ "$ARCH" != "x86_64" && "$ARCH" != "aarch64" ]]; then
    echo -e "${RED}Error: Unsupported architecture '$ARCH'. Supported: x86_64, aarch64${NC}"
    exit 1
fi

echo -e "${BLUE}OrcaSlicer Flatpak Build Script${NC}"
echo -e "${BLUE}================================${NC}"
echo -e "Architecture: ${GREEN}$ARCH${NC}"
echo -e "Build directory: ${GREEN}$BUILD_DIR${NC}"
echo ""

# Check available disk space (flatpak builds need several GB)
AVAILABLE_SPACE=$(df . | awk 'NR==2 {print $4}')
REQUIRED_SPACE=$((5 * 1024 * 1024))  # 5GB in KB

if [[ $AVAILABLE_SPACE -lt $REQUIRED_SPACE ]]; then
    echo -e "${YELLOW}Warning: Low disk space detected.${NC}"
    echo -e "Available: $(( AVAILABLE_SPACE / 1024 / 1024 ))GB, Recommended: 5GB+"
    echo -e "Continue anyway? (y/N)"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        echo "Build cancelled by user"
        exit 1
    fi
fi

# Check if flatpak is installed
if ! command -v flatpak &> /dev/null; then
    echo -e "${RED}Error: Flatpak is not installed. Please install it first.${NC}"
    echo "On Ubuntu/Debian: sudo apt install flatpak"
    echo "On Fedora: sudo dnf install flatpak"
    echo "On Arch: sudo pacman -S flatpak"
    exit 1
fi

# Check if flatpak-builder is installed
if ! command -v flatpak-builder &> /dev/null; then
    echo -e "${RED}Error: flatpak-builder is not installed. Please install it first.${NC}"
    echo "On Ubuntu/Debian: sudo apt install flatpak-builder"
    echo "On Fedora: sudo dnf install flatpak-builder"
    echo "On Arch: sudo pacman -S flatpak-builder"
    exit 1
fi

# Check additional build dependencies
echo -e "${YELLOW}Checking build dependencies...${NC}"
MISSING_DEPS=()

if ! command -v cmake &> /dev/null; then
    MISSING_DEPS+=("cmake")
fi

if ! command -v ninja &> /dev/null && ! command -v make &> /dev/null; then
    MISSING_DEPS+=("ninja or make")
fi

if ! command -v pkg-config &> /dev/null; then
    MISSING_DEPS+=("pkg-config")
fi

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo -e "${RED}Error: Missing required build dependencies: ${MISSING_DEPS[*]}${NC}"
    echo "On Ubuntu/Debian: sudo apt install cmake ninja-build pkg-config"
    echo "On Fedora: sudo dnf install cmake ninja-build pkgconfig"
    exit 1
fi

echo -e "${GREEN}All required dependencies found${NC}"

# Install runtime and SDK if requested
if [[ "$INSTALL_RUNTIME" == true ]]; then
    echo -e "${YELLOW}Installing GNOME runtime and SDK...${NC}"
    flatpak install --user -y flathub org.gnome.Platform//47
    flatpak install --user -y flathub org.gnome.Sdk//47
fi

# Check if required runtime is available
if ! flatpak info --user org.gnome.Platform//47 &> /dev/null; then
    echo -e "${RED}Error: GNOME Platform 47 runtime is not installed.${NC}"
    echo "Run with -i flag to install it automatically, or install manually:"
    echo "flatpak install --user flathub org.gnome.Platform//47"
    exit 1
fi

if ! flatpak info --user org.gnome.Sdk//47 &> /dev/null; then
    echo -e "${RED}Error: GNOME SDK 47 is not installed.${NC}"
    echo "Run with -i flag to install it automatically, or install manually:"
    echo "flatpak install --user flathub org.gnome.Sdk//47"
    exit 1
fi

# Get version information
echo -e "${YELLOW}Getting version information...${NC}"
if [[ -f "version.inc" ]]; then
    VER_PURE=$(grep 'set(SoftFever_VERSION' version.inc | cut -d '"' -f2)
    VER="V$VER_PURE"
    DATE=$(date +'%Y%m%d')
    echo -e "Version: ${GREEN}$VER${NC}"
    echo -e "Date: ${GREEN}$DATE${NC}"
else
    echo -e "${RED}Error: version.inc not found${NC}"
    exit 1
fi

# Cleanup build directory if requested
if [[ "$CLEANUP" == true ]]; then
    echo -e "${YELLOW}Performing additional cleanup...${NC}"
    rm -rf deps/build

    echo -e "${YELLOW}Cleaning up build directories...${NC}"
    rm -rf "$BUILD_DIR" build .flatpak-builder
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Check if flatpak manifest exists
if [[ ! -f "flatpak/io.github.softfever.OrcaSlicer.yml" ]]; then
    echo -e "${RED}Error: Flatpak manifest not found at flatpak/io.github.softfever.OrcaSlicer.yml${NC}"
    exit 1
fi

# Build the Flatpak
echo -e "${YELLOW}Building Flatpak package...${NC}"
echo -e "This may take a while (30+ minutes depending on your system)..."
echo ""

BUNDLE_NAME="OrcaSlicer-Linux-flatpak_${VER}_${ARCH}.flatpak"

# Remove any existing bundle
rm -f "$BUNDLE_NAME"

# Create necessary directories inside repo
mkdir -p "$BUILD_DIR/cache" "$BUILD_DIR/flatpak-builder"

# Set environment variables to match GitHub Actions
export FLATPAK_BUILDER_N_JOBS=$(nproc)

echo -e "${BLUE}Running flatpak-builder...${NC}"
echo -e "Using $FLATPAK_BUILDER_N_JOBS parallel jobs"

# Check flatpak-builder version to determine available options
FLATPAK_BUILDER_VERSION=$(flatpak-builder --version 2>/dev/null | head -1 | awk '{print $2}' || echo "unknown")
echo -e "flatpak-builder version: $FLATPAK_BUILDER_VERSION"

# Build command without unsupported options
if ! flatpak-builder \
    --arch="$ARCH" \
    --user \
    --install-deps-from=flathub \
    --force-clean \
    --repo="$BUILD_DIR/repo" \
    --verbose \
    "$BUILD_DIR/build-dir" \
    flatpak/io.github.softfever.OrcaSlicer.yml; then
    echo -e "${RED}Error: flatpak-builder failed${NC}"
    echo -e "${YELLOW}Check the build log above for details${NC}"
    exit 1
fi

# Create bundle
echo -e "${YELLOW}Creating Flatpak bundle...${NC}"
if ! flatpak build-bundle \
    "$BUILD_DIR/repo" \
    "$BUNDLE_NAME" \
    io.github.softfever.OrcaSlicer \
    --arch="$ARCH"; then
    echo -e "${RED}Error: Failed to create Flatpak bundle${NC}"
    exit 1
fi

# Success message
echo ""
echo -e "${GREEN}✓ Flatpak build completed successfully!${NC}"
echo -e "Bundle created: ${GREEN}$BUNDLE_NAME${NC}"
echo -e "Size: ${GREEN}$(du -h "$BUNDLE_NAME" | cut -f1)${NC}"
echo ""
echo -e "${BLUE}To install the Flatpak:${NC}"
echo -e "flatpak install --user $BUNDLE_NAME"
echo ""
echo -e "${BLUE}To run OrcaSlicer:${NC}"
echo -e "flatpak run io.github.softfever.OrcaSlicer"
echo ""
echo -e "${BLUE}To uninstall:${NC}"
echo -e "flatpak uninstall --user io.github.softfever.OrcaSlicer"