#!/bin/bash
# Double-click this file to build and install Gain Editor (Audio Unit) on this Mac.
cd "$(dirname "$0")"
clear
echo "======================================"
echo "   Gain Editor - Installer"
echo "======================================"
echo ""
if bash build_mac.sh; then
    echo ""
    echo "Installed. Open Ableton > Settings > Plug-Ins > Rescan."
    open "$HOME/Library/Audio/Plug-Ins/Components" 2>/dev/null
else
    echo ""
    echo "Something went wrong. Take a screenshot of this window and send it."
fi
echo ""
read -n 1 -s -r -p "Press any key to close..."
