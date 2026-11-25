#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# Usage:
#   ./build.sh                 configure + build Release (Universal arm64 + x86_64)
#   ./build.sh clean           delete the build folder first
#   ./build.sh debug           build Debug instead of Release
#   ./build.sh native          build only for the current Mac architecture
#   ./build.sh arm64           build only for Apple Silicon (arm64)
#   ./build.sh x86_64          build only for Intel (x86_64)
#   ./build.sh universal       build Universal Binary (arm64 + x86_64, default)
#   ./build.sh xcode           use the Xcode generator instead of Ninja/Make
#   ./build.sh juce=8.0.14     use another JUCE tag

CONFIG="Release"
CLEAN=0
ARCHS="arm64;x86_64"
USE_XCODE=0
JUCE_TAG=""

for arg in "$@"; do
    case "$arg" in
        clean)     CLEAN=1 ;;
        debug)     CONFIG="Debug" ;;
        release)   CONFIG="Release" ;;
        native)    ARCHS="$(uname -m)" ;;
        arm64)     ARCHS="arm64" ;;
        x86_64)    ARCHS="x86_64" ;;
        universal) ARCHS="arm64;x86_64" ;;
        xcode)     USE_XCODE=1 ;;
        juce=*)    JUCE_TAG="${arg#juce=}" ;;
        -h|--help)
            sed -n '6,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake was not found in PATH." >&2
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    echo "Git was not found in PATH. JUCE is fetched with Git." >&2
    exit 1
fi

echo "CMake version:"
cmake --version | head -n 1

if [ "$USE_XCODE" -eq 1 ]; then
    GEN="Xcode"
elif command -v ninja >/dev/null 2>&1; then
    GEN="Ninja"
else
    GEN="Unix Makefiles"
fi

BDIR="$(pwd)/build"
LOG="$(pwd)/build_log.txt"
DIST="$(pwd)/dist"
ART="$BDIR/OtoSampler_artefacts/$CONFIG"

if [ "$CLEAN" -eq 1 ] && [ -d "$BDIR" ]; then
    rm -rf "$BDIR"
fi

if [ -f "$BDIR/CMakeCache.txt" ]; then
    PREV_GEN="$(grep -m1 '^CMAKE_GENERATOR:INTERNAL=' "$BDIR/CMakeCache.txt" | cut -d= -f2- || true)"
    if [ "$PREV_GEN" != "$GEN" ]; then
        echo "The generator changed ($PREV_GEN -> $GEN). Removing the old build folder."
        rm -rf "$BDIR"
    fi
fi

SDK_PATH="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"

CMAKE_ARGS=(
    -S .
    -B "$BDIR"
    -G "$GEN"
    -DCMAKE_BUILD_TYPE="$CONFIG"
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS"
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
)

if [ -n "$SDK_PATH" ]; then
    CMAKE_ARGS+=(-DCMAKE_OSX_SYSROOT="$SDK_PATH")
fi
if [ -n "$JUCE_TAG" ]; then
    CMAKE_ARGS+=(-DOTO_JUCE_TAG="$JUCE_TAG")
fi

rm -f "$LOG"

on_fail() {
    echo ""
    echo "===== Build failed ====="
    if [ -f "$LOG" ]; then
        echo "Full log: $LOG"
        echo "---- error lines ----"
        grep -iE "error:|fatal error:|CMake Error|ld: " "$LOG" | tail -n 30 || true
    fi
}
trap on_fail ERR

echo ""
echo "[1/2] Configure: $GEN  (archs: $ARCHS, build folder: $BDIR)"
echo "      JUCE is downloaded with Git on the first run. This can take a few minutes."
cmake "${CMAKE_ARGS[@]}" 2>&1 | tee "$LOG"

echo ""
echo "[2/2] Build: $CONFIG"
cmake --build "$BDIR" --config "$CONFIG" --parallel 2>&1 | tee -a "$LOG"

if [ ! -d "$ART/VST3/Oto Sampler.vst3" ] && [ ! -d "$ART/AU/Oto Sampler.component" ]; then
    echo "The build finished but no plugin bundles were found in \"$ART\"." >&2
    exit 1
fi

rm -f "$LOG"
trap - ERR

rm -rf "$DIST"
mkdir -p "$DIST"

if [ -d "$ART/VST3/Oto Sampler.vst3" ]; then
    cp -R "$ART/VST3/Oto Sampler.vst3" "$DIST/Oto Sampler.vst3"
    codesign --force --deep --sign - "$DIST/Oto Sampler.vst3"
fi
if [ -d "$ART/AU/Oto Sampler.component" ]; then
    cp -R "$ART/AU/Oto Sampler.component" "$DIST/Oto Sampler.component"
    codesign --force --deep --sign - "$DIST/Oto Sampler.component"
fi
if [ -d "$ART/Standalone/Oto Sampler.app" ]; then
    cp -R "$ART/Standalone/Oto Sampler.app" "$DIST/Oto Sampler.app"
    codesign --force --deep --sign - "$DIST/Oto Sampler.app"
fi

# ---- license texts for distribution, collected from the sources that were actually built ----
JSRC="$BDIR/_deps/juce-src"
LIC="$DIST/licenses"
mkdir -p "$LIC"
cp "LICENSE" "$LIC/OtoSampler-AGPL-3.0.txt"
[ -f "THIRD_PARTY_NOTICES.md" ] && cp "THIRD_PARTY_NOTICES.md" "$LIC/"
[ -f "$JSRC/LICENSE.md" ] && cp "$JSRC/LICENSE.md" "$LIC/JUCE-LICENSE.md"
if JVER="$(git -C "$JSRC" describe --tags --always 2>/dev/null)"; then
    echo "JUCE $JVER" > "$LIC/JUCE-VERSION.txt"
fi
copylic() {
    local dir="$1" name="$2"
    [ -d "$dir" ] || return 0
    while IFS= read -r -d '' f; do
        cp "$f" "$LIC/${name}-$(basename "$f")"
    done < <(find "$dir" -maxdepth 1 -type f \( -iname '*licen*' -o -iname 'copying*' \) -print0)
}
copylic "$JSRC/modules/juce_audio_processors/format_types/VST3_SDK" VST3_SDK
copylic "$JSRC/modules/juce_audio_processors_headless/format_types/VST3_SDK" VST3_SDK
copylic "$JSRC/modules/juce_audio_formats/codecs/oggvorbis" OggVorbis
copylic "$JSRC/modules/juce_audio_formats/codecs/flac" FLAC
copylic "$JSRC/modules/juce_graphics/image_formats/pnglib" libpng
copylic "$JSRC/modules/juce_graphics/image_formats/jpglib" libjpeg
copylic "$JSRC/modules/juce_core/zip/zlib" zlib
copylic "$JSRC/modules/juce_graphics/fonts/harfbuzz" HarfBuzz
copylic "$JSRC/modules/juce_graphics/unicode/sheenbidi" SheenBidi

echo ""
echo "=== Done ==="
[ -d "$DIST/Oto Sampler.vst3" ]      && echo "VST3       : $DIST/Oto Sampler.vst3"
[ -d "$DIST/Oto Sampler.component" ] && echo "AU         : $DIST/Oto Sampler.component"
[ -d "$DIST/Oto Sampler.app" ]       && echo "Standalone : $DIST/Oto Sampler.app"
echo "Licenses   : $DIST/licenses  (ship this folder with the plugin)"
echo "Run ./install.sh to copy the VST3 and AU plugins to your Audio Plug-Ins folder."
