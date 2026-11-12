#!/bin/bash
# LugaruEX for Linux: remove LugaruEX from this Lugaru HD folder.
# Your saves and settings in ~/.local/share/LugaruEX and the game's Data folder are not touched.
EX="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "$EX/lugaruex" ]; then
    echo "Run this from the LugaruEX folder inside your Lugaru HD folder."
    exit 1
fi
echo "This removes LugaruEX from:"
echo "  $(dirname "$EX")"
echo "(including skins made with \"New skin...\" and anything in LugaruEX/Overrides)."
read -r -p "Continue? [y/N] " ok
case "$ok" in
    y | Y | yes) ;;
    *) exit 0 ;;
esac
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/lugaruex.desktop"
rm -rf "$EX"
echo "LugaruEX removed. If you set a Steam launch option for it, clear it in"
echo "Steam: right-click Lugaru HD > Properties > General > Launch options."
