# Lugaru EX

Lugaru engine update for Lugaru HD and the free open-source game.

## Features

- 64-bit engine update and QoL fixes
- Resolution fixes including ultrawide
- High refresh rate support
- Shaders
- Native controller support and vibration
- Local split screen, split screen online and online co-op with uncapped players
- Online through Steam (relay only, no IP addresses shared), LAN or VPN, with a server list,
  passwords, chat and kick / ban
- Mod loader and multiplayer mod sharing
- Launcher
- Windows, Linux and Steam Deck
- Simple installer and auto update

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

GPL v2 or later. The Steam helper (`steam/`) is a separate program under the MIT licence; it
needs Valve's Steamworks SDK, which is not included (without it the game builds without Steam
play). No Lugaru HD game data is included. Lugaru is a trademark of Wolfire Games;
this is a fan project.
