#!/usr/bin/env bash
# Regenerates the PDF manual from its HTML source with headless Chrome:
#   docs/manual.html -> docs/Spreekbuis Manual.pdf
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
command -v "$CHROME" >/dev/null 2>&1 || CHROME="$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)"
[ -n "$CHROME" ] || { echo "Chrome not found (set CHROME=...)"; exit 1; }
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --print-to-pdf="$ROOT/docs/Spreekbuis Manual.pdf" \
    "file://$ROOT/docs/manual.html" 2>/dev/null
ls -la "$ROOT/docs/Spreekbuis Manual.pdf"
