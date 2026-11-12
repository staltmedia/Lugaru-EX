@echo off
rem LugaruEX build script: engine (CMake/Ninja/MSVC) + launcher, both statically linked.
rem Needs Visual Studio 2022 or its Build Tools (C++ workload, includes CMake and Ninja)
rem and vcpkg in .\vcpkg with these libraries:
rem   vcpkg\vcpkg install --triplet x64-windows-static sdl2 openal-soft libpng libjpeg-turbo libogg libvorbis zlib jsoncpp fmt dirent opengl-registry
rem   vcpkg\vcpkg install --triplet x64-windows pkgconf
rem Usage: build.bat [clean]. Then tools\package.ps1 makes dist\ (the zip and Setup.exe).
setlocal
set ROOT=%~dp0
set VCPKG=%ROOT%vcpkg
set TRIPLET=x64-windows-static
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set VS=%%i
if not defined VS (echo Visual Studio / Build Tools not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set PKG_CONFIG_PATH=%VCPKG%\installed\%TRIPLET%\lib\pkgconfig
if "%1"=="clean" rmdir /s /q "%ROOT%build\engine"
cmake -S "%ROOT%lugaru" -B "%ROOT%build\engine" -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=%TRIPLET% -DVCPKG_MANIFEST_MODE=OFF ^
  -DPKG_CONFIG_EXECUTABLE="%VCPKG%\installed\x64-windows\tools\pkgconf\pkgconf.exe" || exit /b 1
cmake --build "%ROOT%build\engine" || exit /b 1
if exist "%ROOT%launcher\build_launcher.bat" call "%ROOT%launcher\build_launcher.bat" || exit /b 1
echo BUILD OK
