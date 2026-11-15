#!/usr/bin/env bash
# Collects the built plug-ins, apps and manuals into dist/spreek-<os>/<product>/ and zips it.
# usage: ci/package.sh <macOS|Windows|Linux> <build dir>
set -euo pipefail
OS="$1"; B="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="spreek"
OUT="$ROOT/dist/$NAME-$OS"
rm -rf "$OUT"; mkdir -p "$OUT"

art() {   # artefact folder of a target (single- or multi-config generators)
    local tgt="$1"
    for d in "$B/spreekbuis/${tgt}_artefacts/Release" "$B/spreekbuis/${tgt}_artefacts" "$B/spraakmaker/${tgt}_artefacts/Release" "$B/spraakmaker/${tgt}_artefacts" "$B/${tgt}_artefacts/Release" "$B/${tgt}_artefacts"; do
        [ -d "$d/VST3" ] && { echo "$d"; return; }
    done
    echo "missing artefacts for $tgt" >&2; exit 1
}

product() {   # product <folder name> <target> <file name>
    local name="$1" tgt="$2" file="$3"
    local A; A="$(art "$tgt")"
    local P="$OUT/$name"; mkdir -p "$P"
    cp -R "$A/VST3/$file.vst3" "$P/"
    case "$OS" in
        macOS)
            cp -R "$A/AU/$file.component" "$A/Standalone/$file.app" "$P/"
            for f in "$P"/*.app "$P"/*.component "$P"/*.vst3; do
                [ -e "$f" ] && codesign --force --deep -s - "$f"
            done ;;
        Windows)
            cp "$A/Standalone/$file.exe" "$P/" ;;
        Linux)
            cp "$A/Standalone/$file" "$P/" ;;
    esac
    return 0
}

product Spreekbuis Spreekbuis Spreekbuis
product Spraakmaker Spraakmaker Spraakmaker
cp "$ROOT/docs/Spreekbuis Manual.pdf" "$ROOT/docs/Spraakmaker Manual.pdf" "$OUT/"
mkdir -p "$OUT/Spreekbuis/tools"
cp "$ROOT/spreekbuis/tools/render.sh" "$ROOT/spreekbuis/tools/render.py" "$ROOT/spreekbuis/tools/render.swift" "$ROOT/spreekbuis/tools/export_items.js" "$OUT/Spreekbuis/tools/"
mkdir -p "$OUT/Spreekbuis/data"; cp "$ROOT/spreekbuis/data/items.json" "$OUT/Spreekbuis/data/"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$OUT/"

cd "$ROOT/dist"
case "$OS" in
    macOS)   ditto -c -k --keepParent "$NAME-$OS" "$NAME-$OS.zip" ;;
    Windows) 7z a -tzip -r "$NAME-$OS.zip" "$NAME-$OS" > /dev/null ;;
    Linux)   tar -czf "$NAME-$OS.tar.gz" "$NAME-$OS" ;;
esac
ls -la "$ROOT/dist"
