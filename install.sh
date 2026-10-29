#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN="$SCRIPT_DIR/build/fyplugin_fileops.so"
if flatpak list --app 2>/dev/null | grep -q org.fooyin.fooyin; then
    PLUGIN_DIR="$HOME/.var/app/org.fooyin.fooyin/.local/lib/fooyin/plugins"
else
    PLUGIN_DIR="$HOME/.local/lib/fooyin/plugins"
fi

if [ ! -f "$PLUGIN" ]; then
    echo "Plugin not built yet. Run ./build.sh first."
    exit 1
fi

mkdir -p "$PLUGIN_DIR"

STOCK_PLUGIN="$(find /var/lib/flatpak/app/org.fooyin.fooyin -name 'fyplugin_fileops.so' 2>/dev/null | head -1)"
if [ -n "$STOCK_PLUGIN" ]; then
    echo "Note: the stock fileops plugin at $STOCK_PLUGIN will be overridden by the user plugin."
fi

if [ -f "$PLUGIN_DIR/fyplugin_fileops.so" ]; then
    cp "$PLUGIN_DIR/fyplugin_fileops.so" "$PLUGIN_DIR/fyplugin_fileops.so.bak"
    echo "Backed up previous plugin to fyplugin_fileops.so.bak"
fi

cp "$PLUGIN" "$PLUGIN_DIR/fyplugin_fileops.so"
echo "Installed to $PLUGIN_DIR/fyplugin_fileops.so"
echo "Restart fooyin to load the new plugin."
