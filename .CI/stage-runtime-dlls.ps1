param (
    [string] $BuildBin = "bin",
    [string] $InstallDir = "Alpharino"
)

$ErrorActionPreference = "Stop"

# Conan copies libssl-3-x64.dll and libcrypto-3-x64.dll into the build bin.
# windeployqt --force-openssl only looks beside the Qt installation, so the
# portable folder never received the OpenSSL 3 runtimes Alpharino.exe loads.
$required = @("libssl-3-x64.dll", "libcrypto-3-x64.dll")

function Find-RuntimeDll([string] $Name) {
    $direct = Join-Path $BuildBin $Name
    if (Test-Path -PathType Leaf $direct) {
        return (Resolve-Path $direct).Path
    }

    $fallback = Join-Path "Chatterino2" $Name
    if (Test-Path -PathType Leaf $fallback) {
        return (Resolve-Path $fallback).Path
    }

    $found = Get-ChildItem -Recurse -File -Filter $Name -Path . -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]Alpharino[\\/]' } |
        Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    $conanHome = if ($Env:CONAN_HOME) { $Env:CONAN_HOME } else { Join-Path $Env:USERPROFILE ".conan2" }
    if (Test-Path $conanHome) {
        $found = Get-ChildItem -Recurse -File -Filter $Name -Path $conanHome -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) {
            return $found.FullName
        }
    }

    return $null
}

foreach ($name in $required) {
    $source = Find-RuntimeDll $name
    if (-not $source) {
        Write-Host "ERROR: Could not find $name in the build output."
        exit 1
    }

    Copy-Item $source (Join-Path $InstallDir $name) -Force
    Write-Host "Staged $name from $source"
}

Get-ChildItem $BuildBin -File -Filter *.dll | ForEach-Object {
    $dest = Join-Path $InstallDir $_.Name
    if (-not (Test-Path -PathType Leaf $dest)) {
        Copy-Item $_.FullName $dest
        Write-Host "Staged extra runtime $($_.Name)"
    }
}
