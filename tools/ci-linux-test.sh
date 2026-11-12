#!/bin/bash
# LugaruEX Linux smoke test (CI): runs the packaged launcher and game on OSS Lugaru's
# free data under a virtual X server with software OpenGL, then a host + guest online
# session on one machine. Usage: ci-linux-test.sh <dist dir> <output dir>
set -uo pipefail
DIST=$(realpath "$1")
mkdir -p "$2"
OUT=$(realpath "$2")
OSS_COMMIT=42f148d033358925d1e31cec68ad02ee03ec64c2 # the OSS Lugaru commit LugaruEX is based on
LEVEL=${LUGARUEX_CI_LEVEL:-campaign:1}
fail=0

if [ ! -d /tmp/oss/Data ]; then
    git init -q /tmp/oss
    git -C /tmp/oss remote add origin https://gitlab.com/osslugaru/lugaru.git
    git -C /tmp/oss fetch -q --depth 1 origin $OSS_COMMIT && git -C /tmp/oss checkout -q FETCH_HEAD ||
        { rm -rf /tmp/oss && git clone -q https://gitlab.com/osslugaru/lugaru.git /tmp/oss && git -C /tmp/oss checkout -q $OSS_COMMIT; }
fi

# a game folder like the Steam one: Data/ plus our LugaruEX/ folder
mkgame() {
    rm -rf "$1"
    mkdir -p "$1"
    cp -r /tmp/oss/Data "$1/Data"
    cp -r "$DIST/LugaruEX" "$1/LugaruEX"
    cat > "$1/LugaruEX/LugaruEX.ini" <<EOF
[Display]
Width=640
Height=360
Mode=windowed
MSAA=0
VSync=0
FPSCap=60
Lighting=1
Shaders=1
[Multiplayer]
Mode=$2
Port=27015
Address=127.0.0.1:27015
EOF
}
run() { # <name> <seconds> <command...>: run under its own X server, own user data
    local name=$1 secs=$2
    shift 2
    XDG_DATA_HOME=/tmp/xdg-$name XDG_CONFIG_HOME=/tmp/xdg-$name ALSOFT_DRIVERS=null LIBGL_ALWAYS_SOFTWARE=1 LUGARUEX_STDOUT=1 \
        timeout $secs xvfb-run -a -s "-screen 0 1280x800x24" "$@" > "$OUT/$name.log" 2>&1
    echo "$name: exit $?"
}

export -f run
mkgame /tmp/game off

# 1. the launcher, every tab (two virtual controllers for Controls / Local)
for t in 0 1 2 3 4; do
    LUGARUEX_FAKE_PADS=2 LUGARUEX_LAUNCHER_SHOT=$OUT/launcher-$t.png,$t run launcher-$t 60 /tmp/game/LugaruEX/lugaruex-launcher
done
[ -f "$OUT/launcher-0.png" ] || { echo "FAIL: launcher made no screenshot"; fail=1; }
[ -f /tmp/game/LugaruEX/Data/Textures/LugaruEX-Logo.png ] && echo "launcher wrote the menu logo" || echo "note: no menu logo (OSS data has no Lugaru.png HD to edit)"

# 2. the game: a level with Enhanced lighting, a picture of the screen, quit (the picture is
#    taken from the X server: software OpenGL keeps no front buffer for the game's own one)
Xvfb :91 -screen 0 1024x600x24 >/dev/null 2>&1 &
XV=$!
sleep 2
(DISPLAY=:91 XDG_DATA_HOME=/tmp/xdg-engine ALSOFT_DRIVERS=null LIBGL_ALWAYS_SOFTWARE=1 LUGARUEX_STDOUT=1 \
    timeout 300 /tmp/game/LugaruEX/lugaruex --exlevel=$LEVEL --exquit=60 > "$OUT/engine.log" 2>&1; echo "engine: exit $?") &
GP=$!
sleep 50
DISPLAY=:91 import -window root "$OUT/engine.png" 2>/dev/null || echo "FAIL: no picture of the game"
wait $GP
kill $XV 2>/dev/null
grep -q "EXTEST done" "$OUT/engine.log" || { echo "FAIL: game test did not finish"; fail=1; }
grep -n "lighting:\|GL \|CRASH\|error" "$OUT/engine.log" | head -20

# 3. online: a host and a guest on this machine
mkgame /tmp/host host
mkgame /tmp/guest join
run host 300 /tmp/host/LugaruEX/lugaruex --exlevel=$LEVEL --exquit=60 &
sleep 8
run guest 300 /tmp/guest/LugaruEX/lugaruex --exquit=45
wait
cp /tmp/xdg-guest/LugaruEX/Screenshots/extest.png "$OUT/guest.png" 2>/dev/null
grep -q "host loaded" "$OUT/guest.log" || { echo "FAIL: the guest was not pulled into the host's level"; fail=1; }
grep -n "net:" "$OUT/host.log" | head -20
grep -n "net:" "$OUT/guest.log" | head -20
grep -n "WARNING\|CRASH" "$OUT/host.log" "$OUT/guest.log" | head

exit $fail
