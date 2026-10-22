#!/usr/bin/env bash
# Build and run the opc_json dev tool: decode a per-vendor <vendor>.opc preset
# cache to editable JSON, or rebuild the cache from an edited JSON file.
#
#   ./scripts/opc_json.sh decode resources/profiles/BambuLab.opc -o /tmp/BambuLab.json
#   # edit /tmp/BambuLab.json
#   ./scripts/opc_json.sh encode /tmp/BambuLab.json -o /tmp/BambuLab.opc
#
#   ./scripts/opc_json.sh --help
#
# The build tree is the first of build/arm64, build/x86_64 or build that exists,
# or pass -DORCA_TOOLS=ON to CMake. Override with:
#   ORCA_BUILD_DIR=<dir>   build tree holding the tool
#   ORCA_CONFIG=<cfg>      build config for multi-config generators
#   ORCA_NO_BUILD=1        run the tool already in the tree, without rebuilding
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd -P)"
build_dir="${ORCA_BUILD_DIR:-}"
config="${ORCA_CONFIG:-}"

if [ -z "$build_dir" ]; then
    for candidate in "$repo_root/build/arm64" "$repo_root/build/x86_64" "$repo_root/build"; do
        if [ -d "$candidate" ]; then build_dir="$candidate"; break; fi
    done
fi
if [ -z "$build_dir" ] || [ ! -d "$build_dir" ]; then
    echo "ERROR: build tree not found (set ORCA_BUILD_DIR or pass -b to CMake)" >&2
    exit 1
fi

# Newest match wins: multi-config trees keep one binary per config, and a stale
# one silently decodes with an older cache layout.
find_tool() {
    local best="" f
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        if [ -z "$best" ] || [ "$f" -nt "$best" ]; then best="$f"; fi
    done < <(find "$build_dir" -name opc_json -type f 2>/dev/null)
    printf '%s' "$best"
}

tool=$(find_tool)
if [ -z "$config" ]; then
    case "$tool" in
        */Debug/*)          config=Debug ;;
        */Release/*)        config=Release ;;
        */RelWithDebInfo/*) config=RelWithDebInfo ;;
        */MinSizeRel/*)     config=MinSizeRel ;;
        *) config=$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=\(.\+\)$/\1/p' "$build_dir/CMakeCache.txt" 2>/dev/null | head -1 || true) ;;
    esac
fi

if [ "${ORCA_NO_BUILD:-0}" != 1 ]; then
    build_args=(--build "$build_dir" --target opc_json)
    if [ -n "$config" ]; then build_args+=(--config "$config"); fi
    if ! cmake "${build_args[@]}"; then
        echo "ERROR: could not build opc_json - configure the build tree with -DORCA_TOOLS=ON:" >&2
        echo "       cmake -S \"$repo_root\" -B \"$build_dir\" -DORCA_TOOLS=ON" >&2
        exit 1
    fi
    tool=$(find_tool)
fi

if [ -z "$tool" ]; then
    echo "ERROR: opc_json not found under $build_dir - build with -DORCA_TOOLS=ON" >&2
    exit 1
fi

exec "$tool" "$@"
