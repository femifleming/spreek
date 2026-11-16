#!/usr/bin/env bash
# (Re-)render the Spreekbuis voice bank.
#   tools/render.sh [--female] [--force] [--stories] [--engine piper|say|sapi|espeak]
# Engine order: Voxtral-4B-TTS (Apple Silicon, --setup-voxtral) > Piper neural voices (nl_NL-pim-medium / nl_BE-nathalie-medium; `python3 tools/render.py --setup-piper`
# installs them on any OS) > Apple Premium/Enhanced/compact voices via tools/render.swift (exact word onsets) > SAPI
# (Windows) / espeak-ng (Linux). Without any of these the plug-in speaks with its built-in formant voice.
# Re-run after installing a better voice; --force re-renders everything.
set -e
cd "$(dirname "$0")/.."
if [ "$(uname)" = "Darwin" ] && command -v swiftc >/dev/null 2>&1 && ! python3 tools/render.py --probe "$@" >/dev/null 2>&1 \
   && ! printf '%s\n' "$@" | grep -q -- '--engine'; then
    # no Piper: the Apple voices through AVSpeech (word onsets straight from the engine)
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
