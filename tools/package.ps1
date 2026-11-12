# Assemble dist\LugaruEX (exactly what goes into the game folder) and the release zip.
# Layout: Lugaru.exe (the launcher, so Steam's Play button opens it) + one LugaruEX\ folder.
$ErrorActionPreference = 'Stop'
$version = '0.4.9'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist\LugaruEX'
$ex = Join-Path $dist 'LugaruEX'
$zip = Join-Path $root "dist\LugaruEX-$version-win64.zip"
$eng = Join-Path $root 'build\engine'

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist, "$ex\Data\Textures", "$ex\licenses" | Out-Null

Copy-Item "$root\build\launcher\Lugaru.exe" "$dist\Lugaru.exe"
Copy-Item "$eng\LugaruEX.exe" "$ex\LugaruEX.exe"
# both programs are statically linked: nothing else is needed next to them
$dlls = @(Get-ChildItem $eng -Filter *.dll -ErrorAction SilentlyContinue)
if ($dlls.Count -gt 0) { Write-Warning ("engine build still has DLLs next to it: " + ($dlls.Name -join ', ')) }

Copy-Item "$root\tools\Uninstall.bat" "$ex\Uninstall.bat"
Copy-Item "$root\lugaru\Data\Textures\FontMono.png" "$ex\Data\Textures\"   # free (CC-BY-SA) OSS Lugaru file the HD data lacks
Copy-Item "$root\lugaru\COPYING.txt" "$ex\COPYING.txt"
Copy-Item "$root\lugaru\CONTENT-LICENSE.txt" "$ex\CONTENT-LICENSE.txt"
Copy-Item "$root\tools\README-LugaruEX.txt" "$ex\README.txt"
# license texts of the libraries built into the programs
$share = Join-Path $root 'vcpkg\installed\x64-windows-static\share'
foreach ($p in 'sdl2', 'openal-soft', 'fmt', 'libpng', 'libjpeg-turbo', 'libogg', 'libvorbis', 'zlib', 'jsoncpp') {
    Copy-Item "$share\$p\copyright" "$ex\licenses\$p.txt"
}
Copy-Item "$root\shared\miniz\LICENSE" "$ex\licenses\miniz.txt" # (mods, installer, updates)

@"
LugaruEX $version is built from the OSS Lugaru source code (GPLv2 or later):
  upstream: https://gitlab.com/osslugaru/lugaru
  LugaruEX source: https://github.com/staltmedia/Lugaru-EX (tag v$version): lugaru\ is
  the engine, launcher\ the launcher, shared\ the code both use.
"@ | Set-Content "$ex\SOURCE.txt" -Encoding ascii

if (Test-Path $zip) { Remove-Item $zip -Force }
# entries named with forward slashes (Compress-Archive writes backslashes, which Linux
# unzip tools - Proton users unpack this zip there - turn into odd folders or file names)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$fs = [IO.File]::Open($zip, [IO.FileMode]::CreateNew)
$za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
Get-ChildItem $dist -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($dist.Length + 1).Replace('\', '/')
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $_.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
}
$za.Dispose()
$fs.Dispose()
# the installer: the launcher with the release zip appended, then "LGXPAYLD" + where the zip starts
$setup = Join-Path $root "dist\LugaruEX-$version-Setup.exe"
$exeBytes = [IO.File]::ReadAllBytes("$root\build\launcher\Lugaru.exe")
$zipBytes = [IO.File]::ReadAllBytes($zip)
$out = [IO.File]::Open($setup, [IO.FileMode]::Create)
$out.Write($exeBytes, 0, $exeBytes.Length)
$out.Write($zipBytes, 0, $zipBytes.Length)
$tag = [Text.Encoding]::ASCII.GetBytes('LGXPAYLD')
$out.Write($tag, 0, 8)
$off = [BitConverter]::GetBytes([UInt64]$exeBytes.Length)
$out.Write($off, 0, 8)
$out.Dispose()

Get-ChildItem $dist -Recurse -File | ForEach-Object { $_.FullName.Substring($dist.Length + 1) }
"PACKAGE OK -> $dist"
"ZIP -> $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB)
"SETUP -> $setup ({0:N1} MB)" -f ((Get-Item $setup).Length / 1MB)
