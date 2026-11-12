#!/bin/bash
# LugaruEX for Linux: install.
# Unpack the release anywhere and run ./LugaruEX/install.sh (or just ./LugaruEX/lugaruex-launcher):
# the installer finds your Lugaru (Steam, the open-source version, or the free game data
# it can download) and puts LugaruEX into it. Your saves and settings go to
# ~/.local/share/LugaruEX. No game file is changed.
EX="$(cd "$(dirname "$0")" && pwd)"
chmod +x "$EX/lugaruex" "$EX/lugaruex-launcher" "$EX/uninstall.sh" 2>/dev/null
exec "$EX/lugaruex-launcher" "$@"
