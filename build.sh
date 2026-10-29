#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FOOYIN_SOURCE="$(cd "${FOOYIN_SOURCE:-$SCRIPT_DIR/../fooyin}" 2>/dev/null && pwd)"
FOOYIN_FLATPAK_LIB="$(find /var/lib/flatpak/app/org.fooyin.fooyin/x86_64/stable -maxdepth 1 -mindepth 1 -type d 2>/dev/null | head -1)/files/lib/fooyin"

if [ ! -d "$FOOYIN_SOURCE/include/core" ]; then
    echo "fooyin source not found at $FOOYIN_SOURCE"
    echo "Clone it:  git clone --branch v0.14.1 --depth 1 https://github.com/fooyin/fooyin.git $SCRIPT_DIR/../fooyin"
    exit 1
fi

if [ ! -f "$FOOYIN_FLATPAK_LIB/libfooyin_core.so.0.0.0" ]; then
    echo "fooyin Flatpak not installed or libraries not found at $FOOYIN_FLATPAK_LIB"
    exit 1
fi

echo "Source:    $SCRIPT_DIR"
echo "fooyin:    $FOOYIN_SOURCE"
echo "Libraries: $FOOYIN_FLATPAK_LIB"
echo ""

flatpak run --share=network --filesystem="$SCRIPT_DIR" --filesystem="$FOOYIN_SOURCE:ro" --filesystem="$FOOYIN_FLATPAK_LIB:ro" --command=bash org.kde.Sdk//6.11 -c "
    set -euo pipefail
    cd \"$SCRIPT_DIR\"
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFOOYIN_SOURCE=\"$FOOYIN_SOURCE\" -DFOOYIN_RUNTIME=\"$FOOYIN_FLATPAK_LIB\"
    cmake --build build
    echo ''
    echo 'Build complete: build/fyplugin_fileops.so'
    cd build && ctest --output-on-failure
"
