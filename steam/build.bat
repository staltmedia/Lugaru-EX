@echo off
rem Builds build\steam\lugaruex-steam.exe (the Steam helper) next to Valve's steam_api64.dll.
rem Needs Valve's Steamworks SDK in thirdparty\steamworks_sdk (public\ and redistributable_bin\),
rem which is not part of the source (Valve's licence): without it there is no Steam play.
rem Expects vcvars64 already set up.
setlocal
set S=%~dp0
set SDK=%S%..\thirdparty\steamworks_sdk
set OUT=%S%..\build\steam
if not exist "%SDK%\public\steam\steam_api.h" (
    echo STEAM SKIPPED: no Steamworks SDK in thirdparty\steamworks_sdk
    exit /b 0
)
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo "/d1trimfile:%S:\=/%" /O2 /EHsc /W3 /MT /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I"%SDK%\public" /Fo"%OUT%\\" /Fe"%OUT%\lugaruex-steam.exe" ^
  "%S%lugaruex_steam.cpp" /link "%SDK%\redistributable_bin\win64\steam_api64.lib" || exit /b 1
copy /y "%SDK%\redistributable_bin\win64\steam_api64.dll" "%OUT%\" >nul || exit /b 1
echo STEAM OK
