#!/usr/bin/env bash
# (Re-)render the Spreekbuis voice bank with the system's Dutch text-to-speech.
#   tools/render.sh [--female] [--force] [--stories]
# macOS: tools/render.swift (AVSpeechSynthesizer: the best installed Dutch voice, Premium > Enhanced > compact, with
#        exact word onsets). Re-run after installing a better voice (System Settings > Accessibility > Spoken Content >
#        Manage Voices > Dutch: Xander (Enhanced), Claire (Premium)). --force re-renders everything.
# Windows (Git Bash / WSL) and Linux: tools/render.py (SAPI, piper or espeak-ng).
# Without any of these the plug-in speaks with its built-in formant voice.
set -e
cd "$(dirname "$0")/.."
if [ "$(uname)" = "Darwin" ] && command -v swiftc >/dev/null 2>&1; then
    mkdir -p build
    if [ -d "$HOME/tongval/web/data" ] && command -v node >/dev/null 2>&1; then
        node tools/export_items.js > build/items.json
    else
        cp data/items.json build/items.json
    fi
    swiftc -O -swift-version 5 tools/render.swift -o build/render 2>/dev/null
    build/render build/items.json "$@"
else
    exec python3 tools/render.py "$@"
fi
