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
Steam=0
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
(DISPLAY=:91 XDG_DATA_HOME=/tmp/xdg-engine ALSOFT_DRIVERS=null LIBGL_ALWAYS_SOFTWARE=1 LUGARUEX_STDOUT=1 LUGARUEX_TEST_ROTCHECK=1 \
    timeout 300 /tmp/game/LugaruEX/lugaruex --exlevel=$LEVEL --exquit=60 > "$OUT/engine.log" 2>&1; echo "engine: exit $?") &
GP=$!
sleep 50
DISPLAY=:91 import -window root "$OUT/engine.png" 2>/dev/null || echo "FAIL: no picture of the game"
wait $GP
kill $XV 2>/dev/null
grep -q "EXTEST done" "$OUT/engine.log" || { echo "FAIL: game test did not finish"; fail=1; }
# the fast main-axis rotation must give the general formula's numbers exactly (GCC too)
grep "rotcheck" "$OUT/engine.log" || echo "FAIL: no rotation check"
grep -q "rotcheck: [0-9]* cases, 0 differ" "$OUT/engine.log" || { echo "FAIL: the fast rotation differs"; fail=1; }
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

# 3b. a guest of another build (modified / rebuilt) must be refused
run host2 120 /tmp/host/LugaruEX/lugaruex --exlevel=$LEVEL --exquit=30 &
sleep 8
LUGARUEX_TEST_BUILDID=0000modified0000 run guest2 120 /tmp/guest/LugaruEX/lugaruex --exquit=15
wait
grep -q "can't join: LugaruEX" "$OUT/host2.log" && grep -q "the host runs LugaruEX" "$OUT/guest2.log" &&
    ! grep -q "connected to" "$OUT/guest2.log" && echo "version lock: a modified build was refused" ||
    { echo "FAIL: a guest of another build was not refused"; fail=1; }

# 3c. Steam: the installed launcher adds itself as a non-Steam game (a pretend Steam account)
FAKEHOME=/tmp/fakehome
rm -rf $FAKEHOME && mkdir -p $FAKEHOME/.local/share/Steam/userdata/12345/config
for n in 1 2; do
    (cd /tmp/game/LugaruEX && HOME=$FAKEHOME XDG_DATA_HOME=$FAKEHOME/.local/share XDG_CONFIG_HOME=$FAKEHOME/.config LUGARUEX_TEST_ADDTOSTEAM=1 \
        LUGARUEX_LAUNCHER_SHOT=$OUT/steam-$n.png,0 LIBGL_ALWAYS_SOFTWARE=1 timeout 60 xvfb-run -a ./lugaruex-launcher > "$OUT/steam-$n.log" 2>&1)
    grep "add to Steam" "$OUT/steam-$n.log"
done
grep -q "add to Steam -> 0.*in Steam: 1" "$OUT/steam-1.log" && grep -q "add to Steam -> 1.*in Steam: 1" "$OUT/steam-2.log" &&
    grep -aq "/tmp/game/LugaruEX/lugaruex-launcher" $FAKEHOME/.local/share/Steam/userdata/12345/config/shortcuts.vdf &&
    echo "steam: added once, found again" || { echo "FAIL: Add to Steam"; fail=1; }

# 3d. the unpacked release again: it finds the install (no reinstall needed) - picture of it
(HOME=$FAKEHOME XDG_DATA_HOME=$FAKEHOME/.local/share XDG_CONFIG_HOME=$FAKEHOME/.config LUGARUEX_LAUNCHER_SHOT=$OUT/setup-found.png \
    LIBGL_ALWAYS_SOFTWARE=1 timeout 60 xvfb-run -a "$DIST/LugaruEX/lugaruex-launcher" > "$OUT/setup-found.log" 2>&1)
mkdir -p $FAKEHOME/.local/share/LugaruEX && echo /tmp/game > $FAKEHOME/.local/share/LugaruEX/installs.txt
(HOME=$FAKEHOME XDG_DATA_HOME=$FAKEHOME/.local/share XDG_CONFIG_HOME=$FAKEHOME/.config LUGARUEX_LAUNCHER_SHOT=$OUT/setup-found2.png \
    LIBGL_ALWAYS_SOFTWARE=1 timeout 60 xvfb-run -a "$DIST/LugaruEX/lugaruex-launcher" > "$OUT/setup-found2.log" 2>&1)
[ -f "$OUT/setup-found2.png" ] && echo "setup: picture of the install list saved" || echo "note: no setup picture"

# 4. launcher update: pretend to be 0.4.10, answer OK, the public release goes in and the new
#    launcher starts (pictures of the question and of the launcher after it)
if [ "${LUGARUEX_CI_UPDATE:-1}" = 1 ]; then
    mkgame /tmp/upd off
    printf '[Launcher]\nAutoUpdate=1\n' >> /tmp/upd/LugaruEX/LugaruEX.ini
    # (marked, so replacing it shows even when the release has the very same build as this one)
    printf "old-build-marker" >> /tmp/upd/LugaruEX/lugaruex
    before=$(sha256sum /tmp/upd/LugaruEX/lugaruex | cut -d" " -f1)
    Xvfb :92 -screen 0 1280x800x24 >/dev/null 2>&1 &
    XU=$!
    sleep 2
    (cd /tmp/upd/LugaruEX && DISPLAY=:92 XDG_DATA_HOME=/tmp/xdg-upd XDG_CONFIG_HOME=/tmp/xdg-upd LIBGL_ALWAYS_SOFTWARE=1 \
        LUGARUEX_TEST_UPDATE_FROM=0.4.10 LUGARUEX_TEST_UPDATE_ANSWER=ok,4 \
        timeout 120 ./lugaruex-launcher > "$OUT/update.log" 2>&1) &
    UP=$!
    for i in $(seq 1 30); do grep -q "asking" "$OUT/update.log" && break; sleep 1; done
    sleep 2
    DISPLAY=:92 import -window root "$OUT/update-question.png" 2>/dev/null
    for i in $(seq 1 90); do grep -q "installed, starting\|could not\|failed" "$OUT/update.log" && break; sleep 1; done
    sleep 8
    DISPLAY=:92 import -window root "$OUT/update-after.png" 2>/dev/null
    # the new launcher must be running from the new file, with its window up
    relaunched=0
    for pid in $(pgrep -x lugaruex-launch); do
        exe=$(readlink /proc/$pid/exe)
        echo "update: launcher process $pid runs $exe"
        case "$exe" in /tmp/upd/*" (deleted)") ;; /tmp/upd/*) relaunched=1 ;; esac
    done
    mean=$(convert "$OUT/update-after.png" -colorspace Gray -format "%[fx:mean]" info: 2>/dev/null || echo 0)
    echo "update: screen brightness after the update $mean"
    awk "BEGIN { exit !($mean > 0.02) }" || relaunched=0
    [ $relaunched = 1 ] && echo "update: the new launcher started" || { echo "FAIL: the new launcher did not start"; fail=1; }
    kill $UP 2>/dev/null
    pkill -f /tmp/upd/LugaruEX/lugaruex-launcher 2>/dev/null
    kill $XU 2>/dev/null
    after=$(sha256sum /tmp/upd/LugaruEX/lugaruex | cut -d' ' -f1)
    cat "$OUT/update.log" | grep "LugaruEX launcher" | head
    grep -q "installed, starting" "$OUT/update.log" && [ "$before" != "$after" ] && echo "update: installed (engine binary replaced)" ||
        { echo "FAIL: the launcher update did not go in"; fail=1; }
fi

exit $fail
