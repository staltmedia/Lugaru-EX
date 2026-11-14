#!/bin/bash
# LugaruEX on Wine (CI): the Windows release - launcher and game - the way Proton runs it
# on Linux and the Steam Deck, on OSS Lugaru's free data with software OpenGL.
# Usage: ci-wine-test.sh <release zip> <output dir> [<Linux tar.gz> for a Windows host + Linux guest test]
set -uo pipefail
ZIP=$(realpath "$1")
mkdir -p "$2"
OUT=$(realpath "$2")
OSS_COMMIT=42f148d033358925d1e31cec68ad02ee03ec64c2
LEVEL=${LUGARUEX_CI_LEVEL:-campaign:1}
fail=0

if [ ! -d /tmp/oss/Data ]; then
    git init -q /tmp/oss
    git -C /tmp/oss remote add origin https://gitlab.com/osslugaru/lugaru.git
    git -C /tmp/oss fetch -q --depth 1 origin $OSS_COMMIT && git -C /tmp/oss checkout -q FETCH_HEAD ||
        { rm -rf /tmp/oss && git clone -q https://gitlab.com/osslugaru/lugaru.git /tmp/oss && git -C /tmp/oss checkout -q $OSS_COMMIT; }
fi
GAME=/tmp/wgame
rm -rf $GAME
mkdir -p $GAME
cp -r /tmp/oss/Data $GAME/Data
(cd $GAME && unzip -q -o "$ZIP")
ls -la $GAME $GAME/LugaruEX > "$OUT/files.txt"
cat > $GAME/LugaruEX/LugaruEX.ini <<EOF
[Display]
Width=1280
Height=720
Mode=windowed
MSAA=0
VSync=0
FPSCap=60
Lighting=1
Shaders=1
EOF

export WINEPREFIX=$HOME/.wine-lugaruex WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" LIBGL_ALWAYS_SOFTWARE=1 ALSOFT_DRIVERS=null
mkdir -p "$WINEPREFIX"
Xvfb :77 -screen 0 1280x800x24 >/dev/null 2>&1 &
XPID=$!
export DISPLAY=:77
sleep 2
echo "setting up the Wine prefix..."
timeout 240 wineboot -u > "$OUT/wineboot.log" 2>&1; echo "wineboot: exit $?"
timeout 120 wineserver -w; echo "wine is ready"
timeout 30 wine --version | tee "$OUT/wine-version.txt"

# 1. the launcher (Lugaru.exe): screenshot the screen after it has drawn, then close it
(cd $GAME && LUGARUEX_SAVE_BANNER='Z:\tmp\wine-banner.png' timeout 60 wine Lugaru.exe > "$OUT/launcher.log" 2>&1) &
sleep 25
import -window root "$OUT/launcher-screen.png" 2>/dev/null || xwd -root -silent | convert xwd:- "$OUT/launcher-screen.png"
cp /tmp/wine-banner.png "$OUT/launcher-banner.png" 2>/dev/null && echo "launcher drew its banner" || { echo "FAIL: launcher did not draw its banner"; fail=1; }
timeout 30 wineserver -k
sleep 2

# 2. the game (LugaruEX.exe): a level with the Enhanced look, a screenshot, quit
(cd $GAME && LUGARUEX_STDOUT=1 timeout 300 wine LugaruEX/LugaruEX.exe --exlevel=$LEVEL --exquit=25 > "$OUT/engine.log" 2>&1)
echo "game: exit $?"
APPDATA_DIR=$(find $WINEPREFIX/drive_c/users -maxdepth 4 -type d -path "*AppData/Roaming" | head -1)
cp "$APPDATA_DIR/LugaruEX/Screenshots/extest.png" "$OUT/engine.png" 2>/dev/null || { echo "FAIL: game made no screenshot"; fail=1; }
grep -q "EXTEST done" "$OUT/engine.log" || { echo "FAIL: game test did not finish"; fail=1; }
grep -n "lighting:\|GL \|CRASH\|error" "$OUT/engine.log" | head -20
timeout 30 wineserver -k
sleep 2

# 3. cross-play: the Windows build (under Wine) hosts, the native Linux build joins
LINUX_TGZ=${3:-}
if [ -n "$LINUX_TGZ" ] && [ -f "$LINUX_TGZ" ]; then
    printf '[Multiplayer]\r\nMode=host\r\nSteam=0\r\nPort=27015\r\n' >> $GAME/LugaruEX/LugaruEX.ini
    LGAME=/tmp/lgame
    rm -rf $LGAME
    mkdir -p $LGAME
    cp -r /tmp/oss/Data $LGAME/Data
    tar -C $LGAME -xzf "$LINUX_TGZ"
    printf '[Display]\nWidth=960\nHeight=540\nMode=windowed\nMSAA=0\nVSync=0\nFPSCap=60\n[Multiplayer]\nMode=join\nSteam=0\nAddress=127.0.0.1:27015\n' > $LGAME/LugaruEX/LugaruEX.ini
    (cd $GAME && LUGARUEX_STDOUT=1 timeout 300 wine LugaruEX/LugaruEX.exe --exlevel=$LEVEL --exquit=70 > "$OUT/xhost.log" 2>&1) &
    HP=$!
    sleep 15
    XDG_DATA_HOME=/tmp/xdg-xguest LUGARUEX_STDOUT=1 timeout 300 $LGAME/LugaruEX/lugaruex --exquit=45 > "$OUT/xguest.log" 2>&1
    wait $HP # (not a bare wait: that also waits for the X server, forever)
    cp /tmp/xdg-xguest/LugaruEX/Screenshots/extest.png "$OUT/xguest.png" 2>/dev/null
    grep -n "net:" "$OUT/xhost.log" | head -15
    grep -n "net:" "$OUT/xguest.log" | head -15
    if grep -q "host loaded" "$OUT/xguest.log"; then
        echo "cross-play: the Linux guest joined the Windows host's level"
    else
        echo "FAIL: the Linux guest did not join the Windows host"
        fail=1
    fi
    if grep -q "differs from the host" "$OUT/xguest.log"; then
        echo "FAIL: the Linux guest's level or ground differs from the Windows host's"
        fail=1
    fi
fi
timeout 30 wineserver -k
kill $XPID 2>/dev/null
exit $fail
