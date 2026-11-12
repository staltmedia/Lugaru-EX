# Lugaru EX

An updated engine for **Lugaru** (Windows and Linux, 64-bit), built on the open-source
[OSS Lugaru](https://gitlab.com/osslugaru/lugaru) code. It plays your own copy of
**Lugaru HD** (Steam or others) or the free open-source game data, and never changes the
game's own files.

## What's new

**Engine**
- 64-bit, current libraries, many crash and stutter fixes.
- Simulation at your monitor's refresh rate with smooth (interpolated) rendering.
- Any resolution including ultrawide; windowed, borderless or fullscreen; MSAA,
  anisotropic filtering, V-Sync, FPS cap, field of view.
- Enhanced lighting (press `R` in game): sun shadows, per-pixel light, ambient occlusion,
  bounced light, light shafts, fire as light. The original 2005 look stays available.
- A camera that no longer clips into rocks and follows bodies.

**Co-op**
- Local split screen for up to 4 players (one controller each, drop in any time).
- Online co-op over UDP with any number of players, LAN auto-discovery or IP:port.
  Windows and Linux players can play together.
- Mods: install mod zips by drag and drop; when you host, players who join download your
  mods automatically.

**Quality of life**
- A launcher with Graphics, Character (skins, your own images, 3D preview), Controls
  (bindings, presets, controller view), Local, Multiplayer and Mods tabs, fully usable with
  a controller (Steam Deck friendly).
- An installer that finds your game, and an auto updater (Auto Update box in the launcher).
- Full controller support in game, with vibration.
- Colour blind filters, texture override folder for upscaled packs.

## Install

**Windows**
1. Download `LugaruEX-<version>-Setup.exe` from Releases and run it.
2. Pick your Lugaru folder (it is found for you; no copy? *Get the free game data*) and
   press **Install**. Steam's Play button now opens LugaruEX.

**Linux** (and Steam Deck)
1. Download `LugaruEX-<version>-linux-x64.tar.gz` from Releases and unpack it anywhere.
2. Run `LugaruEX/install.sh`, pick your Lugaru folder and press **Install**. For Steam's
   Play button, paste the launch option it shows into Steam (Lugaru HD > Properties).

To uninstall, run `Uninstall.bat` (Windows) or `uninstall.sh` (Linux) in the game's
`LugaruEX` folder. Playing online over the internet needs the host's UDP port (27015) open,
or everyone on the same virtual LAN (Tailscale, ZeroTier, Radmin VPN, ...).

## Building

- Windows: Visual Studio 2022 Build Tools, [vcpkg](https://github.com/microsoft/vcpkg) in
  `vcpkg\` with the static libraries listed in `build.bat`; run `build.bat`, then
  `tools\package.ps1` for the release files.
- Linux: see `.github/workflows/build.yml` (Ubuntu 22.04, vcpkg static libraries, OpenAL
  Soft from source); `tools/package-linux.sh` makes the release.

## Credits

Lugaru EX is built on the work of:
- **Lugaru** by Wolfire Games, and **OSS Lugaru** by its contributors -
  [gitlab.com/osslugaru/lugaru](https://gitlab.com/osslugaru/lugaru) (GPL v2+, game data CC-BY-SA).
- [SDL2](https://github.com/libsdl-org/SDL), [OpenAL Soft](https://github.com/kcat/openal-soft),
  [libpng](https://github.com/pnggroup/libpng), [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo),
  [libogg / libvorbis](https://github.com/xiph), [zlib](https://github.com/madler/zlib),
  [JsonCpp](https://github.com/open-source-parsers/jsoncpp), [{fmt}](https://github.com/fmtlib/fmt),
  [dr_libs](https://github.com/mackron/dr_libs).
- [Dear ImGui](https://github.com/ocornut/imgui), [stb](https://github.com/nothings/stb) and
  [miniz](https://github.com/richgel999/miniz) (Linux launcher, mods, installer);
  [vcpkg](https://github.com/microsoft/vcpkg) to build.
- Published techniques: GTAO ambient occlusion and bent normals (Jimenez et al., 2016),
  Alchemy ambient obscurance (McGuire et al., 2011), the Charlie sheen (Estevez & Kulla,
  2017), specular occlusion (Lagarde & de Rousiers, 2014), Schlick's Fresnel (1994), ACES
  filmic tone curve (Narkowicz, 2016), linear-sampled Gaussian blur (Rákos, 2010), light
  scattering as a post-process (Mitchell, GPU Gems 3), colour blind simulation and
  correction (Viénot, Brettel & Mollon, 1999; Fidaner, Lin & Ozguven, 2005), "Fix Your
  Timestep!" and snapshot interpolation (Glenn Fiedler), and Valve's "Source Multiplayer
  Networking".
- The launcher's layout follows the classic Unity "Screen Resolution / Graphics Quality"
  dialog. The banner and the in-game logo are made on your PC from your own Lugaru logo.

## License

GPL v2 or later (`LICENSE`). No Lugaru HD game data is included; the one bundled texture
(`FontMono.png`, from OSS Lugaru) is CC-BY-SA (`lugaru/CONTENT-LICENSE.txt`). Lugaru is a
trademark of Wolfire Games; this is a fan project, not affiliated with them.
