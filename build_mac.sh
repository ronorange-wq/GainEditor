#!/bin/bash
# Builds Gain Editor as an Audio Unit for Apple Silicon (M1/M2/M3) and installs it.
set -e
cd "$(dirname "$0")"

echo "== Gain Editor: building the Audio Unit =="

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Apple's command line tools are missing. A window will open: click Install, then run this script again."
    xcode-select --install
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    if command -v brew >/dev/null 2>&1; then
        brew install cmake
    else
        echo "CMake is missing. Install Homebrew from https://brew.sh then run:  brew install cmake"
        exit 1
    fi
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

AU="$HOME/Library/Audio/Plug-Ins/Components/Gain Editor.component"
if [ -d "$AU" ]; then
    echo "Installed: $AU"
else
    echo "Build finished but the plugin was not copied. Look for 'Gain Editor.component' inside the build folder."
    exit 1
fi

# Make macOS re-read Audio Units, then check this one.
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
echo "== Validating with auval =="
auval -v aufx Gedt Gned | tail -5

# Also make a .pkg installer, to install on other Macs with a double-click.
pkgbuild --component "$AU" --install-location "/Library/Audio/Plug-Ins/Components" \
    --identifier com.gaineditor.plugin --version 1.1.0 "Gain Editor Installer.pkg" >/dev/null 2>&1 \
    && echo "Also created: Gain Editor Installer.pkg (for other Macs)"

echo ""
echo "Done. In Ableton: Settings > Plug-Ins > turn on 'Use Audio Units v2', then Rescan."
echo "The plugin is in the browser under Plug-Ins > Audio Units > GainEditor > Gain Editor."
