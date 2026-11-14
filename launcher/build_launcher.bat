@echo off
rem Builds build\launcher\Lugaru.exe (the LugaruEX launcher), statically linked. Expects vcvars64 already set up.
setlocal
set L=%~dp0
set OUT=%L%..\build\launcher
set SDL=%L%..\vcpkg\installed\x64-windows-static
if not exist "%OUT%" mkdir "%OUT%"
rc /nologo /fo "%OUT%\launcher.res" "%L%launcher.rc" || exit /b 1
rem /d1trimfile: source file names in the exe (asserts) start here, not at the build folder
cl /nologo "/d1trimfile:%L:\=/%" /O2 /EHsc /W3 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DSDL_MAIN_HANDLED /std:c++17 /I"%SDL%\include\SDL2" /Fo"%OUT%\\" /Fe"%OUT%\Lugaru.exe" ^
  "%L%launcher.cpp" "%L%preview.cpp" "%L%preview_core.cpp" "%L%pad.cpp" "%L%padviz.cpp" "%L%controls.cpp" "%L%local.cpp" "%L%customskins.cpp" "%L%mods.cpp" ^
  "%L%..\shared\fsutil.cpp" "%L%..\shared\modpack.cpp" "%L%..\shared\setup.cpp" "%L%..\shared\update.cpp" "%L%..\shared\miniz\miniz.c" "%OUT%\launcher.res" ^
  /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib ole32.lib oleaut32.lib windowscodecs.lib msimg32.lib shell32.lib shlwapi.lib comdlg32.lib gdiplus.lib opengl32.lib ^
  "%SDL%\lib\SDL2-static.lib" winmm.lib imm32.lib version.lib uuid.lib advapi32.lib setupapi.lib dinput8.lib || exit /b 1
echo LAUNCHER OK
