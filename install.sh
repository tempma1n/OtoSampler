#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# Usage:
#   ./install.sh             install VST3 and AU for the current user (~/Library/Audio/Plug-Ins)
#   ./install.sh --system    install system-wide (/Library/Audio/Plug-Ins, may require sudo)
#   ./install.sh --app       also install Standalone app to ~/Applications (or /Applications with --system)

SYSTEM=0
INSTALL_APP=0

for arg in "$@"; do
    case "$arg" in
        --system) SYSTEM=1 ;;
        --app)    INSTALL_APP=1 ;;
        -h|--help)
            sed -n '6,9p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 1
            ;;
    esac
done

DIST="$(pwd)/dist"
VST3_SRC="$DIST/Oto Sampler.vst3"
AU_SRC="$DIST/Oto Sampler.component"
APP_SRC="$DIST/Oto Sampler.app"

if [ ! -d "$VST3_SRC" ] && [ ! -d "$AU_SRC" ]; then
    echo "No built plugins found in \"$DIST\". Build first with ./build.sh" >&2
    exit 1
fi

if [ "$SYSTEM" -eq 1 ]; then
    PLUG_BASE="/Library/Audio/Plug-Ins"
    APP_DIR="/Applications"
else
    PLUG_BASE="$HOME/Library/Audio/Plug-Ins"
    APP_DIR="$HOME/Applications"
fi

VST3_DST="$PLUG_BASE/VST3"
AU_DST="$PLUG_BASE/Components"

install_bundle() {
    local src="$1"
    local dst_dir="$2"
    local name
    name="$(basename "$src")"
    mkdir -p "$dst_dir"
    rm -rf "$dst_dir/$name"
    cp -R "$src" "$dst_dir/$name"
    xattr -cr "$dst_dir/$name" 2>/dev/null || true
    echo "Installed: $dst_dir/$name"
}

if [ -d "$VST3_SRC" ]; then
    install_bundle "$VST3_SRC" "$VST3_DST"
fi
if [ -d "$AU_SRC" ]; then
    install_bundle "$AU_SRC" "$AU_DST"
fi
if [ "$INSTALL_APP" -eq 1 ] && [ -d "$APP_SRC" ]; then
    install_bundle "$APP_SRC" "$APP_DIR"
fi
