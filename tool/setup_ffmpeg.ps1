# Copyright 2026 The surround_encoder authors. MIT license.
#
# Builds an FFmpeg development layout (include/ + lib/ + bin/) for one or
# more Windows triplets using vcpkg, with an LGPL-only configuration.
#
# Usage:
#   .\tool\setup_ffmpeg.ps1 -Triplet x64-windows              # single
#   .\tool\setup_ffmpeg.ps1 -Triplet arm64-windows            # Windows ARM64
#   .\tool\setup_ffmpeg.ps1                                   # all Windows triplets
#
# The output lands in native/ffmpeg/<triplet>/ which the plugin's CMakeLists
# picks up via SURROUND_ENCODER_FFMPEG_ROOT. FFmpeg is NOT redistributed in
# this repository; every consumer builds their own (see NOTICE.md).
param(
  [string[]]$Triplet = @('x64-windows', 'arm64-windows')
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutputRoot = Join-Path $RepoRoot 'native\ffmpeg'

# vcpkg location: VCPKG_ROOT env, or install a local copy on first use.
$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) {
  $VcpkgRoot = Join-Path $RepoRoot 'native\.vcpkg'
  if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
    Write-Host "Cloning vcpkg into $VcpkgRoot ..."
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat')
  }
}
$Vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'

foreach ($triplet in $Triplet) {
  $portDir = Join-Path $OutputRoot "vcpkg-build-$triplet"
  New-Item -ItemType Directory -Force -Path $portDir | Out-Null

  Write-Host "Building FFmpeg for $triplet (this takes a while)..."
  Push-Location $portDir
  try {
    & $Vcpkg install `
      --triplet $triplet `
      "--x-manifest-root=$PSScriptRoot" `
      "--x-install-root=$portDir\vcpkg_installed"
    if ($LASTEXITCODE -ne 0) {
      throw "vcpkg install failed for $triplet"
    }
  } finally {
    Pop-Location
  }

  # Collect the dev layout the plugin's CMake expects.
  $installed = Join-Path $portDir "vcpkg_installed\$triplet"
  $out = Join-Path $OutputRoot $triplet
  New-Item -ItemType Directory -Force -Path (Join-Path $out 'lib') | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $out 'bin') | Out-Null
  Copy-Item -Recurse -Force (Join-Path $installed 'include') (Join-Path $out 'include')
  Copy-Item -Recurse -Force (Join-Path $installed 'lib') (Join-Path $out 'lib')
  Copy-Item -Recurse -Force (Join-Path $installed 'bin') (Join-Path $out 'bin')

  Write-Host "FFmpeg for $triplet ready: $out"
}

Write-Host ''
Write-Host 'Set SURROUND_ENCODER_FFMPEG_ROOT to one of the output directories'
Write-Host 'or copy its contents into native/ffmpeg/<triplet>/ before building.'
