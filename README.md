# Lugaru EX

An updated engine for Lugaru on Windows and Linux. It plays your copy of Lugaru HD or the
free open-source game, and never changes the game's files.

## Key changes

- 64-bit engine with current libraries and many crash fixes
- Runs at your monitor's refresh rate with smooth motion
- Any resolution, windowed, borderless or fullscreen
- Enhanced lighting with real shadows, press R to switch looks
- Camera no longer clips into rocks

## Additions

- Local split screen for up to 4 players
- Online co-op for any number of players, Windows and Linux together
- Mods: drag and drop zips, shared with players who join your game
- Launcher with graphics, character, controls, co-op and mod settings
- Full controller support, Steam Deck friendly
- Installer and auto updates
- Colour blind filters and a texture override folder

## Install

**Windows:** run `LugaruEX-<version>-Setup.exe` from Releases and press Install.

**Linux:** unpack `LugaruEX-<version>-linux-x64.tar.gz` from Releases, run `LugaruEX/install.sh`
and press Install.

No game? The installer can download the free version.

## Credit

- Lugaru by Wolfire Games
- [OSS Lugaru](https://gitlab.com/osslugaru/lugaru), the open-source engine this is built on
- [SDL2](https://github.com/libsdl-org/SDL), [OpenAL Soft](https://github.com/kcat/openal-soft),
  [libpng](https://github.com/pnggroup/libpng), [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo),
  [Xiph Ogg and Vorbis](https://github.com/xiph), [zlib](https://github.com/madler/zlib),
  [JsonCpp](https://github.com/open-source-parsers/jsoncpp), [fmt](https://github.com/fmtlib/fmt),
  [dr_libs](https://github.com/mackron/dr_libs), [Dear ImGui](https://github.com/ocornut/imgui),
  [stb](https://github.com/nothings/stb), [miniz](https://github.com/richgel999/miniz),
  [vcpkg](https://github.com/microsoft/vcpkg)
- Lighting from published work by Jimenez et al., McGuire et al., Estevez and Kulla,
  Lagarde and de Rousiers, Schlick, Narkowicz, Rákos and Mitchell
- Networking and timing ideas from Glenn Fiedler and Valve
- Colour blind filters after Viénot, Brettel and Mollon, and Fidaner, Lin and Ozguven

## License

GPL v2 or later. No Lugaru HD game data is included. Lugaru is a trademark of Wolfire Games;
this is a fan project.
