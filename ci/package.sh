#!/usr/bin/env bash
# Collects the built plug-in (every format), the app, the manual and the voice tools into dist/spreek-<os>/Spreekbuis/ and zips it.
# usage: ci/package.sh <macOS|Windows|Linux> <build dir>
set -euo pipefail
OS="$1"; B="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="spreek"
OUT="$ROOT/dist/$NAME-$OS"
rm -rf "$OUT"; mkdir -p "$OUT"

art() {   # artefact folder of a target (single- or multi-config generators)
    local tgt="$1"
    for d in "$B/${tgt}_artefacts/Release" "$B/${tgt}_artefacts"; do
        [ -d "$d/VST3" ] && { echo "$d"; return; }
    done
    echo "missing artefacts for $tgt" >&2; exit 1
}

# one plug-in: Spreekbuis (VST3, VST2, AU on macOS, the standalone app)
A="$(art Spreekbuis)"
P="$OUT/Spreekbuis"; mkdir -p "$P"
cp -R "$A/VST3/Spreekbuis.vst3" "$P/"
case "$OS" in
    macOS)
        cp -R "$A/AU/Spreekbuis.component" "$A/VST/Spreekbuis.vst" "$A/Standalone/Spreekbuis.app" "$P/"
        for f in "$P"/*.app "$P"/*.component "$P"/*.vst3 "$P"/*.vst; do
            [ -e "$f" ] && codesign --force --deep -s - "$f"
        done ;;
    Windows)
        cp "$A/VST/Spreekbuis.dll" "$A/Standalone/Spreekbuis.exe" "$P/" ;;
    Linux)
        cp "$A/VST/Spreekbuis.so" "$A/Standalone/Spreekbuis" "$P/" ;;
esac
cp "$ROOT/docs/Spreekbuis Manual.pdf" "$OUT/"
mkdir -p "$P/tools" "$P/data"
cp "$ROOT/tools/render.sh" "$ROOT/tools/render.py" "$ROOT/tools/render.swift" "$ROOT/tools/piper_render.py" "$ROOT/tools/voxtral_render.py" "$ROOT/tools/export_items.js" "$P/tools/"
cp "$ROOT/data/items.json" "$P/data/"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$OUT/"

cd "$ROOT/dist"
case "$OS" in
    macOS)   ditto -c -k --keepParent "$NAME-$OS" "$NAME-$OS.zip" ;;
    Windows) 7z a -tzip -r "$NAME-$OS.zip" "$NAME-$OS" > /dev/null ;;
    Linux)   tar -czf "$NAME-$OS.tar.gz" "$NAME-$OS" ;;
esac
ls -la "$ROOT/dist"
