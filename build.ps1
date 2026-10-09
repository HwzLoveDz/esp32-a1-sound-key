[CmdletBinding()]
param(
    [ValidateSet('Both', 'A1', 'Korvo')][string]$Target = 'Both',
    [string]$IdfPath = $env:IDF_PATH,
    [string]$Python = 'python',
    [string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'

# Run in an ESP-IDF terminal after exporting the S31-capable IDF environment.
if ([string]::IsNullOrWhiteSpace($IdfPath)) {
    throw 'IDF_PATH is not set. Export your ESP32-S31-capable ESP-IDF environment first, or provide -IdfPath.'
}
$env:IDF_PATH = (Resolve-Path -LiteralPath $IdfPath).Path
$skIdfScript = Join-Path $env:IDF_PATH 'tools/idf.py'
if (-not (Test-Path -LiteralPath $skIdfScript -PathType Leaf)) {
    throw "ESP-IDF tools/idf.py is missing: $skIdfScript"
}
Get-Command $Python -ErrorAction Stop | Out-Null

if ($Target -ne 'Korvo') {
    if ([string]::IsNullOrWhiteSpace($env:ESP32_A1_SDK_ROOT)) {
        throw 'ESP32_A1_SDK_ROOT is required for A1. Set it to your separately obtained esp32_a1_v0.5 SDK directory.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $env:ESP32_A1_SDK_ROOT 'src/esp32_a1_codec.c') -PathType Leaf)) {
        throw "The supplied A1 SDK directory is incomplete: $env:ESP32_A1_SDK_ROOT"
    }
    $env:ESP32_A1_SDK_ROOT = (Resolve-Path -LiteralPath $env:ESP32_A1_SDK_ROOT).Path
}
if (-not [string]::IsNullOrWhiteSpace($env:ESP_CODEC_DEV_ROOT)) {
    $env:ESP_CODEC_DEV_ROOT = (Resolve-Path -LiteralPath $env:ESP_CODEC_DEV_ROOT).Path
}

$skProjects = switch ($Target) {
    'A1' { @('a1_device') }
    'Korvo' { @('korvo_host') }
    default { @('a1_device', 'korvo_host') }
}
foreach ($skProject in $skProjects) {
    Push-Location -LiteralPath (Join-Path $PSScriptRoot $skProject)
    try {
        & $Python $skIdfScript -B $BuildDirectory --preview -DIDF_TARGET=esp32s31 build
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: $skProject ($LASTEXITCODE). Outputs retained."
        }
    } finally {
        Pop-Location
    }
}
