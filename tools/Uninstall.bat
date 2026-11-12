@echo off
rem LugaruEX: remove LugaruEX from this Lugaru HD folder.
rem Your saves and settings in %APPDATA%\LugaruEX and the game's Data folder are not touched.
cd /d "%~dp0.."
if not exist "LugaruEX\LugaruEX.exe" (
  echo Run this from the LugaruEX folder inside your Lugaru HD folder.
  pause & exit /b 1
)
echo This removes LugaruEX from:
echo   %CD%
echo (including skins made with "New skin..." and anything in LugaruEX\Overrides).
choice /m "Continue"
if errorlevel 2 exit /b 0

rem the installer's shortcuts
del "%USERPROFILE%\Desktop\LugaruEX.lnk" 2>nul
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\LugaruEX.lnk" 2>nul
set RESTORED=0
if exist "LugaruEX\backup\Lugaru.exe" (
  copy /y "LugaruEX\backup\Lugaru.exe" "Lugaru.exe" >nul && set RESTORED=1
) else (
  powershell -NoProfile -Command "if ((Get-Item 'Lugaru.exe' -ErrorAction SilentlyContinue).VersionInfo.FileDescription -like 'LugaruEX*') { Remove-Item 'Lugaru.exe' }"
)
echo.
echo LugaruEX removed.
if "%RESTORED%"=="0" (
  echo To get the original Lugaru.exe back: in Steam, right-click Lugaru HD ^> Properties ^>
  echo Installed Files ^> Verify integrity of game files.
)
pause
rem the folder this file is in goes last, once this window has closed
start "" /min cmd /c "timeout /t 2 /nobreak >nul & rmdir /s /q LugaruEX"
