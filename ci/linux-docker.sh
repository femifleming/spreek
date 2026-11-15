#!/usr/bin/env bash
# Builds and tests the bundle on Ubuntu 22.04 in Docker, the way CI does (use -j2: more runs out of memory).
#   ci/linux-docker.sh [/path/to/JUCE]     (JUCE is fetched when no path is given)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE="${1:-}"
MOUNT=""; JARG=""
if [ -n "$JUCE" ]; then MOUNT="-v $JUCE:/juce:ro"; JARG="-DJUCE_DIR=/juce"; fi
docker run --rm -v "$ROOT":/src:ro $MOUNT -w /work ubuntu:22.04 bash -c "
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build git ca-certificates pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libegl-dev libgl1-mesa-dev libglu1-mesa-dev mesa-common-dev xvfb fonts-dejavu-core \
  libcurl4-openssl-dev p7zip-full > /dev/null
cp -r /src /work/spreek && cd /work/spreek && rm -rf build JUCE
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPREEK_LTO=OFF $JARG
cmake --build build --parallel 2 --target spreek_all SBTest SpraakTest SpraakFull SpraakShot SpreekHostCheck
mkdir -p out
find_exe() { find build -type f -name \"\$1\" -path '*_artefacts*' | head -n 1; }
xvfb-run -a \"\$(find_exe SBTest)\"
xvfb-run -a \"\$(find_exe SpraakTest)\" out
xvfb-run -a \"\$(find_exe SpraakFull)\"
xvfb-run -a \"\$(find_exe SpraakShot)\" out
xvfb-run -a \"\$(find_exe SpreekHostCheck)\" \"\$(find build -name Spreekbuis.vst3 | head -n 1)\" \"\$(find build -name Spraakmaker.vst3 | head -n 1)\"
ci/package.sh Linux build && ls -la dist
"
