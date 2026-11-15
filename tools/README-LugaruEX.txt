LugaruEX 0.5.3 - engine update for Lugaru (Windows and Linux)
=============================================================

What it is
  A newer engine built from the open-source Lugaru code (GPLv2+), running on
  your own Lugaru HD "Data" folder, or on the free open-source game data.
  Nothing in Data is modified.

Install (Windows)
  Run LugaruEX-<version>-Setup.exe. It finds your Lugaru (Steam or other
  copies; Browse... for anywhere else; "Download game free" if you have
  none - it asks where to put it), and with Install puts LugaruEX in. Steam's Play button then opens
  the LugaruEX launcher; there is also a desktop / Start menu shortcut.
  (The win64 zip holds the same files, for installing by hand or updating.)
  Uninstall: run LugaruEX\Uninstall.bat, then in Steam use "Verify integrity
  of game files" to get the original Lugaru.exe back.

Install (Linux, Steam Deck)
  Unpack LugaruEX-<version>-linux-x64.tar.gz anywhere and run
  LugaruEX/install.sh: the same installer as on Windows. It lists your game
  folders and which LugaruEX version each one has:
  - Play starts a folder's LugaruEX as it is (nothing is installed again).
  - Install / Update copies this version in (settings, mods and saves stay).
  - Add to Steam puts "Lugaru EX" in your Steam library (Non-Steam): it opens
    the launcher, which keeps LugaruEX up to date. Steam closes for a moment
    while it is added and starts again by itself. The launcher has the same
    button until it is in Steam.
  - Remove... takes LugaruEX out of a folder (its settings, mods and
    screenshots go with it), or deletes a whole free copy of the game that
    LugaruEX downloaded.
  For Steam's own Play button on Lugaru HD, paste the launch option it shows
  into Steam (right-click Lugaru HD > Properties > General).
  Saves and the log: ~/.local/share/LugaruEX/.
  If Steam runs your Lugaru HD with Proton, install this Linux version into
  it the same way. Linux and Windows PCs play online together.
  Both launchers work with a controller alone: LB / RB switch tabs, D-pad /
  stick move, A selects, B goes back, Start plays.

Updates
  With Auto Update ticked (bottom left of the launcher), the launcher looks
  for a new LugaruEX when it starts and asks before updating. OK downloads
  and installs it, then the launcher opens again. Settings, mods, presets and
  saves are kept.

Mods
  The Mods tab installs mods: drag mod .zip files onto the launcher (or Add
  mod...). Any layout works: a Data folder, the game's folders, folders like
  "This goes into Maps", or loose files - each file is sorted by what it is
  (maps, campaigns, pictures, sounds, models), and Mac / Windows leftovers
  are dropped. A campaign named main.txt (made to replace the game's own)
  becomes the mod's own campaign next to the main one, with its world map.
  Only game data is unpacked, never programs.
  Tick a mod to switch it on; the higher one in the list wins when two change
  the same file (Up / Down). Right-click a mod (X on a controller) to delete
  it. New campaigns from mods appear in the game's campaign list; a mod with
  only maps gets one made for it, the maps in order.
  Online: with "Mod share" ticked on the Multiplayer tab, the host's mods download to
  everyone who joins and they play with exactly the host's mods (yours come
  back afterwards). It happens before you join the level ("Downloading the
  host's mods").
  Mods can bring characters: skins in Textures\Custom\rabbit\ and
  Textures\Custom\wolf\, clothes in Textures\Custom\clothes\ (PNG or JPG), and
  models in Models\Custom\rabbit\<Name>\ or Models\Custom\wolf\<Name>\: any
  of the creature's own files by their names (Body.solid .. Body7.solid,
  BodyLow.solid, Belt.solid and the Skeleton files BasicFigure,
  BasicFigureLow, RabbitBelt; Wolf*.solid and BasicFigureWolf* for wolves)
  plus an optional Skin.png (its Default skin). Missing files come from the
  creature, and a model moves like its creature. While the mod is ticked they
  show up on the Character tab ("Name (Mod)"); untick it and they are gone.
  The same folders in LugaruEX\Data work without a mod.

What goes into the game folder
  Lugaru.exe        (Steam) the LugaruEX launcher, so Play opens it
  LugaruEX\         everything else:
    LugaruEX Launcher.exe   the launcher (shortcuts point here)
    LugaruEX.exe      the game engine (one file, no DLLs)
    LugaruEX.ini      settings written by the launcher
    Mods\             installed mods (Mods tab)
    Overrides\        put higher-resolution textures here (see below)
    Presets\          controller / key presets from the Controls tab
    Data\             one small free font texture, and your custom skins
    backup\           the game's own Lugaru.exe
    README.txt, COPYING.txt, licenses\   this file and the licenses
    Uninstall.bat     removes LugaruEX

Texture overrides
  Files in LugaruEX\Overrides replace files in Data\ without touching Data\.
  Mirror the Data folder layout, e.g. LugaruEX\Overrides\Textures\Snow.jpg
  replaces Data\Textures\Snow.jpg. .png and .jpg are interchangeable, any size
  works (powers of two are best). Character skins and clothing are painted
  with blood on the CPU and are resized to 512x512.

Saves, the in-game config and a log file live in %APPDATA%\LugaruEX\
(Linux: ~/.local/share/LugaruEX/). LugaruEX.log is the first thing to look
at if something goes wrong; the previous run is LugaruEX.prev.log, and a
crash also writes crash.dmp on Windows (on Linux the log lists where).

Launcher
  Graphics: monitor, any resolution (incl. ultrawide / custom), windowed
  (resizable; a size too big for the desktop opens maximized) / borderless /
  exclusive fullscreen, MSAA, anisotropic
  filtering, V-Sync (off / on / adaptive), FPS cap, field of view, texture
  quality, Shaders (off = only the original 2005 look), High Resolution
  Textures (uses the Overrides folder), colour blind mode (Deuteranopia,
  Protanopia, Tritanopia - also recolours the minimap markers).
  FPS cap matches the display.
  The HUD and menus always keep their proportions on wide screens.
  Character: Random (the default: a skin nobody else in the level wears), a
    skin of your choice for Rabbit or Wolf (a wolf plays as a real wolf), or
    Default (the level's own character). Your choices apply on every level -
    campaign, mod campaigns and online too; anything left on Default (the
    whole character, the skin or the clothes) is what the level gives its
    player. Other players set to Default get the level's character too.
    "New skin..." lets you pick your own image (PNG, JPG, BMP, GIF, TIFF or
    WebP). It is copied as a PNG into LugaruEX\Data\Textures\Custom\ and
    shows up in the skin lists. It has to follow the same layout as the
    game's own skins (start from one of the files in Data\Textures).
    Uninstalling LugaruEX removes them with the LugaruEX folder.
    Clothes: Default (the level's own), Random (a different outfit each
    level), None, 25 outfits put together from what the game's own
    characters wear (shirts, camo, leather, gis, ninja, desert robes...), or
    your own image ("New clothes...", same layout as a skin; saved in
    LugaruEX\Data\Textures\Custom\clothes\). The 3D preview shows the
    character and clothes as they look in the game. Wolves wear none, but
    player wolves carry, draw and put away weapons like rabbits. Online,
    everyone sees your skin, model and clothes; your own images and models
    (from a mod or LugaruEX\Data) are sent to the others when Mod share is on
    for the host and them, else they see the creature's own look.
  Controls: every action with a keyboard/mouse key and a controller button.
    Double-click a cell (or A on a row), then press the key or button (Esc
    cancels). Presets:
    type a name and Save, pick one from the list and Load, Delete, or
    Defaults. A live Xbox 360-style outline shows the controller you last
    touched: buttons sink when pressed, sticks tilt and click, triggers fill.
    "Shaders (next look)" is the key / button that switches the look.
    Vibration on/off and look speed.
    Walking: hold Ctrl (unless Ctrl is bound to something), or push the left
    stick only part way (push it all the way to run). Rabbits and wolves.
    F12 takes a screenshot, F11 starts and stops recording a video. Both
    go to LugaruEX\Screenshots & Recording. Videos are MP4 with the game's
    sound (Windows 10 2004 or newer; older Windows records without sound).
    On Linux, recording needs ffmpeg installed and has no sound yet.
  Local: 1 player by default; each controller switched on adds a player and
    switching it off removes them (their character choice is remembered).
    Each player's first list is their input: player 1 is "Keyboard + <your
    controller>", or "Keyboard and mouse" alone (then the controller is
    player 2). Picking another player's controller swaps the two.
    Player 1 looks as set on the Character tab; the others each pick Default
    / Random / Rabbit - skin / Wolf - skin, or "New rabbit/wolf skin...". The screen picture shows who gets which part of
    the screen; each box only reacts to its own controller so you can tell
    who is who. Layouts are fixed: 1 full screen, 2 top / bottom or side by
    side (drag a screen toward a side or toward the top / bottom), 3 = two
    on top and one wide at the bottom,
    4 = the four corners. With 3 or 4, drop one box onto another to swap.
  Multiplayer: your Name (User if empty), then Solo, Host or Connect; Play
    starts the game that way. Online play goes through Steam (see Online
    co-op below).
    Host: Host options (the arrow) has the Server name (^1 red, ^2 green,
    ^3 yellow, ^4 blue, ^5 cyan, ^6 magenta, ^7 white, ^8 orange, ^9 purple,
    ^0 grey colour the text after them), Password (tick it and type one;
    Show reveals it), Allow custom players (else guests get a random look),
    Max players (yours included, 0 = no limit) and LAN / VPN (no Steam: your
    network or a virtual one like Radmin, Hamachi, ZeroTier) with its UDP
    port.
    Connect: pick a server in the list, or type the host's IP for LAN / VPN.
    A password protected server asks for its password in a small window.
    Mod share: (Only use with players you trust), for hosts and guests.
    Joining a server that shares mods asks first. If the two PCs have
    different mods and either one has Mod share off, the guest can't join
    (it says why).
    Servers: one list of servers on Steam, on your network / VPN, and the
    ones you joined before, with players, status (In game / In menu, locked,
    mods) and ping. Double click a server to see who is there (a password
    protected server hides its players).
    While an online game runs the launcher stays open. Your own server is
    at the top of the list with everyone on it: right click a player to
    Kick them (they can't come back for 5 minutes) or Ban them (until you
    stop hosting: every new game starts with nobody banned). Their
    character stays behind as an enemy. Play works again when the game ends.
    In the game the host can do the same in the console (~): kick <name>,
    ban <name>, unban <name> (the start of a name is enough). Only the host
    can use game commands in an online game (firstperson, thirdperson and
    fps work for everyone).
  With a controller: D-pad / left stick move between the settings, A
  selects (opens a list, ticks a box), B closes a list, LB / RB switch tabs,
  Start plays.

Local split-screen co-op (up to 4 players)
  Every connected controller is a player; with one controller (or none) the
  game is single player as before. Player 1 also uses keyboard and mouse.
  Each player gets their own camera and part of the screen (the Local tab's
  layout) with their own health and minimap; partners are blue markers. The
  score is shared: it shows once, in the top-left screen. Bonus pop-ups
  ("Solid hit", combos...) show on the screen of the player who earned them.
  Smaller screens are a little more zoomed in.
  - Extra players (split screen and online) start next to player 1, on a
    free spot (never inside a wall or on a level character), and move along
    if the level moves player 1 at the start (opening scenes).
  - Plug a controller in during play and that player drops in next to
    player 1. Unplug one and that player just stops until the same
    controller comes back.
  - Player 1 can play on keyboard and mouse alone (Local tab): then every
    controller is a player of its own, for couch co-op with one keyboard.
  - While you are down, press attack to watch the next player (alive or a
    body), and again to come back to yourself. Your own look controls turn
    the camera. The view cuts straight to the next player. The dark "dead"
    filter only shows while you watch a body. The camera follows a body
    like a living player, wherever it gets knocked around to.
  - Sound: every sound is heard once, from the screen of the player closest
    to it (as if that player were listening), so nothing doubles up.
  - The level restarts only when every player is down. Reaching the exit
    with any player counts.
  - Enemies go after whichever player is closest. Your attacks never lock
    on to a partner, but throws, ragdolls and collisions still hit them.
  - Start on any controller opens the menu (pauses for everyone); any
    controller can move through the menus. Back cycles the lighting look.
  - "Random" picks a rabbit skin nobody else in the level is wearing and
    keeps it while you replay that level. "Default" (under Random in the
    Character tab's Skin list) wears the skin the level gives its player.
  - Story scenes (dialogue) show on every player's screen. Whoever started it
    - talked to the character, walked up to them or into the spot - clicks
    through it with attack; scenes that start on their own (scripted ones)
    are clicked through by player 1 (the host when playing online). Hints
    like "Click to talk to friends!" wait until the scene is over. Partners who are not in the scene are
    hidden until it ends, so nobody stands in the middle of the shot.

Online co-op
  Online play goes through Steam: Steam must be running and logged in on
  every PC (the game itself doesn't have to be on Steam - the free version
  works too). It shows in Steam as "Spacewar", Valve's free test app.
  Nobody sees anyone's IP address: everything goes through Valve's relay
  servers, and nobody has to open ports. The host's Steam profile can be
  found by people who join or browse the server list; guests' profiles only
  by the host.
  Host: pick Host in the launcher's Multiplayer tab, press Play and start
  any level as usual. Your server shows in everyone's server list; set a
  Password to keep it to your friends. Everyone who joins - also in the
  middle of a level - drops in next to you and follows you to every level
  after that, up to Max players (no limit by default).
  Connect: pick Connect (or just click a server in the list) and press
  Play. You wait in the menu ("Waiting for host...") until the host is in
  a level. A wrong password asks again; a full server or different mods
  show on that screen.
  Without Steam: the host ticks LAN / VPN under Host options; the others
  find the server in the list (same network or VPN) or type the host's IP
  (then the people you play with know your address).
  - Chat: press T in a level (unless T is bound to an action), type, Enter
    sends, Escape cancels. The line floats over your character's head for
    everyone for a few seconds and fades; nearer players' text is bigger.
    The console (~) keeps the log as "name: text", with who joined / left.
  - The host's PC runs the game; yours shows it and sends your buttons. Your
    camera, menus, sound and screen are your own, like single player.
  - Escape opens your own menu only: the game keeps going for everyone.
  - Same rules as local co-op: enemies go after everyone, partners are blue
    dots, the level restarts when everyone is down, a down player can watch
    the others with attack.
  - Split screen works online too, on any PC: a guest with two controllers
    plugged in joins with two players and gets split screen on their own
    screen; the host can play split screen at the same time. Each PC only
    splits its own screen.
  - Your Character tab choice is your look online (Random = a skin nobody
    in the level is wearing). A skin you made yourself shows only
    if the other PC has the same file.
  - LAN / VPN over the internet: the host forwards the UDP port (27015 by
    default) on their router, or everyone joins the same virtual LAN (Radmin
    VPN, Hamachi, ZeroTier, Tailscale...) and uses the host's VPN IP. Windows
    asks to allow LugaruEX through the firewall the first time: allow it.
  - It uses about 30-40 KB/s from the host to each guest in a busy level and
    about 3 KB/s back.
  - A guest sees the world a few hundredths of a second behind the host; the
    delay grows and shrinks by itself with how steady the connection is, so
    movement stays smooth over a bumpy internet / VPN link.
  - Blood (sprays and wounds on characters) and controller vibration reach
    guests too.
  - ~ twice (console, then the stats display) shows the connection: ping, lost snapshots, jitter, how far behind the
    host the guest's view is, and frame rate (the host sees each guest's).
    The same numbers go into LugaruEX.log every 10 seconds - send that file
    along if online play feels off. Rough guide: ping under 80 ms and jitter
    under 20 ms feel smooth; with a virtual LAN, a "relayed" connection (in
    Radmin VPN: not a direct one) is usually the cause of high numbers.
  - The host and every guest need the same LugaruEX version, and the same
    build: a modified or rebuilt game can't join (both get told why).
    Windows and Linux (native or Proton) PCs can play together.
  - Safety: the server list never shows anyone's address; names, chat and
    server names are plain text only; passwords are never sent as they are;
    everything that arrives over the network (levels, looks, names, numbers)
    is checked before it is used. Through Steam nobody sees your address;
    with LAN / VPN the people you play with do (that's how the game
    reaches you). Logs only show the first half of other players' addresses.
  - Steam play uses lugaruex-steam, a small separate program (MIT licence)
    with Valve's steam_api library next to it. LugaruEX is not made or
    endorsed by Valve; Steam is a trademark of Valve Corporation.
  - Alt-tab is fine online: the game keeps running in the background.
  - Each PC writes the other's graphics card, screen and frame rate to its
    log. A guest whose copy of a level differs from the host's (Lugaru HD and
    the free game, for example) gets the host's copy when the level starts;
    it is only warned on screen if that can't be done.
  - If the host's PC stalls (a slow PC loading a level), guests wait up to 20
    seconds and carry on.
  - Frame rate matters most: a PC that shows far fewer frames than it
    simulates looks choppy to everyone. LugaruEX lowers the simulation rate by
    itself when that happens (the log says so, and also writes what each
    part of a frame costs there); on a weaker PC also try the Original look
    (R), less antialiasing or a lower resolution.
  Not yet: predicting a guest's own moves (your character answers your
  buttons after the network round trip plus that small delay).

Tutorial together
  Every player - split screen and online - is in the tutorial. Each step
  waits until every player has done it ("2/3 players done"; when you're done
  it says "Done - waiting for the others"). Everyone gets their own knife and
  sword, and the sparring partner goes after whoever still has to do the
  current step. Tab still skips a step for everyone.

Controller in game
  Left stick / D-pad move, right stick looks. Default buttons: A jump,
  LT crouch / block, RT attack, X throw / pick up, Y draw / put away, Back
  next shader look, Start menu. (Setups still on the first defaults - B
  crouch, X attack, RB throw - switch to these automatically.)
  In menus: stick or D-pad moves between buttons, A selects, B goes back.
  Vibration: getting hit, landing a hit and hard landings rumble the
  controller - the harder the hit or the fall, the stronger it is.

Music
  Each area has its own theme; when enemies come after you an alarm sting
  plays and the fight music takes over, fading back once it's over; when
  everyone is down the menu theme plays. (Lugaru HD's fight music and alarm
  are MP3 files named .ogg - LugaruEX reads every sound by its contents, so
  they play.)

Blood
  Blood is on (full, with decals) by default; turn it down in the in-game
  Options if you prefer.

Bodies, score and time
  The game's own moves on bodies work on a dead partner's body too (drop kick
  it when no fight is on, crouch-stab with a knife, ground-stab with a sword,
  smash with a staff, jump down on it), and every player can stomp bodies.
  As in the original, a flying body knocks over enemies but not a standing
  player (a jump kick that lands sends you off in a backflip; a missed one
  drops you), and a thrown knife that hits a partner hurts them like anyone
  (no bonus for it).
  Score works as in the original: going straight on to the next level keeps
  it; dying replays the level with the score you started it with. The end-of-
  level screen shows the level score, time (m:ss) and awards, the same on
  every PC; partners are not counted as enemies for the awards.
  Enemies see, hear and smell every player the way they did player 1.
  If the game ever stops with an error, the message lists where it happened:
  send %APPDATA%\LugaruEX\LugaruEX.log (LugaruEX.prev.log once restarted;
  Linux: ~/.local/share/LugaruEX/).

In game
  R          switch lighting look: Original / Enhanced, for this game
             (the name shows bottom-left - in split screen only in the bottom-left
             view). With Shaders ticked in the launcher every game starts in
             Enhanced.
  ~          cycles: developer console -> stats display (fps; online also
             ping, lost snapshots, jitter, delay) -> off. The first time the
             console opens it lists every command (dev tools are always on)
  Console commands for your own view (everyone, also online guests):
             firstperson - the camera at your eyes (narrower view, nothing can
             block it; turning far turns your body; works while down and
             when watching another player); thirdperson - back to normal;
             fps - a frame counter top right (fps again hides it)
  Shift+K    skip to the next level (challenge or campaign)
  Left Alt + a letter = developer keys, e.g. Alt+M map editor, Alt+B slow
             motion, Alt+H invincible, Alt+N ragdoll, Alt+C free camera

Game changes
  - A weapon left hanging in the air (dropped over a ledge, its rock gone)
    falls to the ground.
  - No profile name screen; all challenge levels unlocked; campaign "Level
    Select" (any level, scores and times still recorded); Shift+K skips
    levels in the campaign too. The campaign map still shows your journey:
    it moves on each time you beat the next level in order (playing others
    from Level Select doesn't move it).

Engine changes
  - Simulation runs at your monitor's refresh rate (240 Hz on a 240 Hz
    screen) with interpolated rendering; gameplay doesn't depend on the
    frame rate and the old 60 fps lock is gone.
  - V-Sync On by default (no tearing); DPI-aware, so scaled displays render
    at their full resolution.
  - Lighting looks (R): Original (the 2005 look) and Enhanced. Enhanced lights
    the world with the sun: real soft shadows from characters, weapons, trees,
    bushes, rocks and boxes (also from trees just off screen), small contact
    shadows where things touch, per-pixel sunlight on characters and objects,
    a soft sheen on fur where the sun hits it, highlights on blades and the
    sky reflected in them, sky light that only reaches what can see the sky
    (horizon-based ambient occlusion: dark under bodies, in corners and where
    limbs meet), light bounced off nearby surfaces in their colour, sunlight
    scattering through the air (light shafts), fire as light (burning characters,
    flaming weapons, campfires, burning plants) and bloom. 3D world only.
  - Character skinning, particles and weapons no longer read matrices back
    from the GPU driver every frame (removes stalls / frame-time spikes).
  - Weapon physics moved from the renderer into the fixed simulation step.
  - Mipmaps + anisotropic filtering on all textures; no 1024px size limit.
  - HD file names, mp3/wav music and old map clothing paths are handled.
  - Wide screens: Hor+ field of view, menus and HUD kept at 4:3 proportions.

Steam
  A Steam update or "Verify integrity of game files" may put the original
  Lugaru.exe back; just copy Lugaru.exe from the zip in again.
  After Play the launcher stays in the background (no window) until the game
  closes, so Steam shows the game as running the whole time.
  Upgrading from LugaruEX 0.2: unzip over it as usual; the launcher moves
  your settings into LugaruEX\ and removes the old files from the game folder.

Credits: Lugaru by Wolfire Games; built on OSS Lugaru (gitlab.com/osslugaru/lugaru)
and on SDL2, OpenAL Soft, libpng, libjpeg-turbo, libogg, libvorbis, zlib, JsonCpp,
{fmt}, dr_libs, Dear ImGui, stb and miniz. This software is based in part on the work
of the Independent JPEG Group.
Licenses: engine GPLv2+ (COPYING.txt); Lugaru content CC-BY-SA
(CONTENT-LICENSE.txt); library licenses in licenses\. Source location: SOURCE.txt.

Camera and effects
  The camera follows with a very light smoothing (it never trails more
  than a step behind). Slow motion and the
  running-fast blur keep their 2005 trails in the Original look (now drawn
  at full resolution and in step with the display); with a shader look on
  they are left out so they don't fight the lighting.
