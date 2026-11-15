#!/usr/bin/env bash
# Regenerates the two PDF manuals from their HTML sources with headless Chrome:
#   spreekbuis/docs/manual.html  -> docs/Spreekbuis Manual.pdf
#   spraakmaker/docs/manual.html -> docs/Spraakmaker Manual.pdf
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
command -v "$CHROME" >/dev/null 2>&1 || CHROME="$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)"
[ -n "$CHROME" ] || { echo "Chrome not found (set CHROME=...)"; exit 1; }
mkdir -p "$ROOT/docs"
for p in spreekbuis:Spreekbuis spraakmaker:Spraakmaker; do
    src="${p%%:*}"; name="${p##*:}"
    "$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --print-to-pdf="$ROOT/docs/$name Manual.pdf" \
        "file://$ROOT/$src/docs/manual.html" 2>/dev/null
    ls -la "$ROOT/docs/$name Manual.pdf"
done
