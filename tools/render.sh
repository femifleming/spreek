#!/bin/bash
# (Re-)render all Tongval items for Spreekbuis. Re-run after installing a better Dutch voice
# (System Settings → Accessibility → Spoken Content → Manage Voices → Dutch). --force re-renders everything.
set -e
cd "$(dirname "$0")/.."
mkdir -p build
node tools/export_items.js > build/items.json
swiftc -O -swift-version 5 tools/render.swift -o build/render 2>/dev/null
build/render build/items.json "$@"
