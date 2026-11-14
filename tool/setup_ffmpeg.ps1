# Copyright 2026 The surround_encoder authors. MIT license.
#
# Builds a MINIMAL, LGPL-configuration FFmpeg from source (audio-only: AC-3
# encoder, MPEG-TS muxer, file/udp protocols, swresample) for Windows using
# MSYS2 + MinGW gcc. Produces .dll runtime libraries plus MSVC-importable
# .lib files generated from FFmpeg's .def exports, so the plugin can link
# with the standard MSVC toolchain.
#
# Usage:
#   .\tool\setup_ffmpeg.ps1                          # x64-windows
#   .\tool\setup_ffmpeg.ps1 -Msys2Root D:\tools\msys64
#
# Output lands in native/ffmpeg/<triplet>/ which the plugin's CMake finds
# automatically. FFmpeg is never committed or redistributed (see NOTICE.md);
# building from source also satisfies the LGPL source-availability duty.
#
# Requirements:
#   - MSYS2 (https://msys2.org) with packages:
#     make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-nasm diffutils
#     (the script installs them if pacman is available)
#   - Visual Studio Build Tools (for lib.exe, used to make .lib imports)
#   - Set http_proxy/https_proxy before running if GitHub needs a proxy.
param(
  [string]$Triplet = 'x64-windows',
  [string]$Msys2Root = 'C:\msys64',
  [string]$FfmpegTag = 'n8.1'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutputRoot = Join-Path $RepoRoot 'native\ffmpeg'
# The plugin's CMake discovers native/ffmpeg/windows-<arch>.
$LayoutName = if ($Triplet -match '^arm64') { 'windows-arm64' } else { 'windows-x64' }
$Prefix = Join-Path $OutputRoot $LayoutName

if ($Triplet -notmatch '^(x64-windows|arm64-windows)$') {
  throw "Unsupported triplet '$Triplet' - use x64-windows or arm64-windows"
}
if (-not (Test-Path (Join-Path $Msys2Root 'usr\bin\bash.exe'))) {
  throw "MSYS2 not found at $Msys2Root. Install it from https://msys2.org `
    and pass -Msys2Root, or mount it yourself."
}

# --- locate lib.exe (VS toolchain) ---------------------------------------
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) { throw 'vswhere.exe not found - install Visual Studio Build Tools' }
$VsPath = & $VsWhere -latest -property installationPath
$LibExe = Get-ChildItem -Path (Join-Path $VsPath 'VC\Tools\MSVC') -Recurse `
  -Filter lib.exe | Select-Object -First 1 -ExpandProperty FullName
if (-not $LibExe) { throw 'lib.exe not found under the VS installation' }
Write-Host "Using lib.exe: $LibExe"

# --- MSYS2 environment ----------------------------------------------------
$MsysBash = Join-Path $Msys2Root 'usr\bin\bash.exe'
$Pacman = Join-Path $Msys2Root 'usr\bin\pacman.exe'

Write-Host 'Installing MSYS2 toolchain (idempotent)...'
& $MsysBash -lc 'pacman -Sy --noconfirm --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-nasm diffutils' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'pacman install failed' }

# --- source ---------------------------------------------------------------
$SrcDir = Join-Path $OutputRoot 'ffmpeg-src'
if (-not (Test-Path (Join-Path $SrcDir 'configure'))) {
  Write-Host "Cloning FFmpeg $FfmpegTag (shallow)..."
  if (Test-Path $SrcDir) { Remove-Item -Recurse -Force $SrcDir }
  & git clone --depth 1 --branch $FfmpegTag `
    https://github.com/FFmpeg/FFmpeg.git $SrcDir
  if ($LASTEXITCODE -ne 0) { throw 'git clone failed' }
}

# --- configure + build inside MSYS2 (UCRT64 gcc) --------------------------
$WinRepoRoot = $RepoRoot -replace '\\', '/'
$WinPrefix = $Prefix -replace '\\', '/'
$ConfigureFlags = @(
  "--prefix=$WinPrefix",
  '--enable-shared', '--disable-static',
  '--disable-everything',
  '--enable-encoder=ac3',
  '--enable-muxer=mpegts',
  '--enable-protocol=file,udp',
  '--disable-avdevice', '--disable-avfilter',
  '--disable-swscale',
  '--disable-programs', '--disable-doc', '--disable-debug',
  '--disable-autodetect', '--enable-small'
)
if ($Triplet -match '^arm64') {
  $ConfigureFlags += '--disable-x86asm'
}

$BashScript = @"
set -e
export PATH=/ucrt64/bin:`$PATH
cd '$WinRepoRoot/native/ffmpeg/ffmpeg-src'
if [ ! -f config.mak ] || [ ! -f config.h ]; then
  mkdir -p build-win
  cd build-win
  ../configure $($ConfigureFlags -join ' ')
fi
make -j`$(nproc)
make install
"@
$TmpScript = Join-Path $env:TEMP 'surround-ffmpeg-build.sh'
Set-Content -Path $TmpScript -Value $BashScript -Encoding UTF8

# Convert a Windows path to its MSYS2 form (C:\foo\bar -> /c/foo/bar).
function ConvertTo-MsysPath([string]$path) {
  $p = $path -replace '\\', '/'
  if ($p -match '^([A-Za-z]):/(.*)$') {
    return '/' + $Matches[1].ToLower() + '/' + $Matches[2]
  }
  return $p
}

Write-Host 'Configuring and building FFmpeg (this takes a while)...'
& $MsysBash -lc "bash '$(ConvertTo-MsysPath $TmpScript)'"
if ($LASTEXITCODE -ne 0) { throw 'FFmpeg build failed' }

# --- def -> .lib import libraries ----------------------------------------
Write-Host 'Generating MSVC import libraries from .def exports...'
$LibOut = Join-Path $Prefix 'lib'
$Machine = if ($Triplet -match '^arm64') { 'ARM64' } else { 'X64' }
Get-ChildItem -Path $LibOut -Filter '*.def' | ForEach-Object {
  $name = $_.BaseName            # e.g. avcodec-62
  & $LibExe "/def:$($_.FullName)" "/machine:$Machine" "/out:$LibOut\$name.lib"
  if ($LASTEXITCODE -ne 0) { throw "lib.exe failed for $name" }
  # Also provide the unversioned name (avcodec.lib) the plugin's CMake
  # find_library() calls look for.
  $unversioned = ($name -replace '-\d+$', '') + '.lib'
  Copy-Item -Force "$LibOut\$name.lib" "$LibOut\$unversioned"
}

Write-Host ''
Write-Host "Minimal FFmpeg installed to $Prefix"
Get-ChildItem $BinDir -Filter '*.dll' | ForEach-Object {
  Write-Host ("  {0}  {1:N0} KB" -f $_.Name, ($_.Length / 1KB))
}
Write-Host ''
Write-Host 'The plugin CMake discovers native/ffmpeg/<triplet> automatically.'
