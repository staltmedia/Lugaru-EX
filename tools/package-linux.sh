#!/bin/bash
# Assemble the Linux release: dist/LugaruEX/ (what goes into the game folder) and
# dist/LugaruEX-<version>-linux-x64.tar.gz. Usage: package-linux.sh <build dir> <dist dir>
set -euo pipefail
B=$(realpath "$1")
D=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VER=$(sed -n "s/^\$version = '\(.*\)'.*/\1/p" "$ROOT/tools/package.ps1")
EX="$D/LugaruEX"

rm -rf "$D"
mkdir -p "$EX/Data/Textures" "$EX/licenses"

for bin in "$B/engine/lugaruex" "$B/launcher/lugaruex-launcher"; do
    name=$(basename "$bin")
    # the symbols go into a separate file for crash reports (addr2line -fCe <name>.debug 0xOFFSET)
    objcopy --only-keep-debug "$bin" "$D/$name.debug"
    objcopy --strip-debug --strip-unneeded --add-gnu-debuglink="$D/$name.debug" "$bin" "$EX/$name"
    chmod +x "$EX/$name"
done

cp "$ROOT/tools/linux/install.sh" "$EX/install.sh"
cp "$ROOT/tools/linux/uninstall.sh" "$EX/uninstall.sh"
chmod +x "$EX/install.sh" "$EX/uninstall.sh"
cp "$ROOT/lugaru/Data/Textures/FontMono.png" "$EX/Data/Textures/" # free (CC-BY-SA) OSS Lugaru file the HD data lacks
cp "$ROOT/lugaru/Dist/Linux/lugaru.png" "$EX/lugaruex.png"
cp "$ROOT/lugaru/COPYING.txt" "$EX/COPYING.txt"
cp "$ROOT/lugaru/CONTENT-LICENSE.txt" "$EX/CONTENT-LICENSE.txt"
cp "$ROOT/tools/README-LugaruEX.txt" "$EX/README.txt"
# license texts of the libraries built into the programs
SHARE="$ROOT/vcpkg/installed/x64-linux/share"
for p in sdl2 libpng libjpeg-turbo libogg libvorbis zlib jsoncpp; do
    cp "$SHARE/$p/copyright" "$EX/licenses/$p.txt"
done
if [ -f "$ROOT/deps/openal-soft-COPYING" ]; then
    cp "$ROOT/deps/openal-soft-COPYING" "$EX/licenses/openal-soft.txt"
fi
cp "$ROOT/launcher-sdl/thirdparty/imgui/LICENSE.txt" "$EX/licenses/dear-imgui.txt"
cp "$ROOT/launcher-sdl/thirdparty/stb/LICENSE" "$EX/licenses/stb.txt"
cp "$ROOT/shared/miniz/LICENSE" "$EX/licenses/miniz.txt"

cat > "$EX/SOURCE.txt" <<EOF
LugaruEX $VER is built from the OSS Lugaru source code (GPLv2 or later):
  upstream: https://gitlab.com/osslugaru/lugaru
  LugaruEX source: https://github.com/staltmedia/Lugaru-EX (tag v$VER): lugaru/ is
  the engine, launcher-sdl/ the Linux launcher, shared/ the code both use.
EOF

tar -C "$D" -czf "$D/LugaruEX-$VER-linux-x64.tar.gz" LugaruEX
tar -C "$D" -czf "$D/LugaruEX-$VER-linux-x64-debug.tar.gz" lugaruex.debug lugaruex-launcher.debug
find "$EX" -type f | sed "s|^$EX/||"
echo "PACKAGE OK -> $D/LugaruEX-$VER-linux-x64.tar.gz ($(du -h "$D/LugaruEX-$VER-linux-x64.tar.gz" | cut -f1))"
