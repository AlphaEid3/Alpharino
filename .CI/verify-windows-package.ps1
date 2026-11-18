param (
    [string] $InstallDir = "Alpharino"
)

$ErrorActionPreference = "Stop"
# dumpbin writes its banner to stderr. Don't treat that as a failed step.
$PSNativeCommandUseErrorActionPreference = $false

$required = @("libssl-3-x64.dll", "libcrypto-3-x64.dll")
$missingRequired = @()
foreach ($name in $required) {
    $path = Join-Path $InstallDir $name
    if (-not (Test-Path -PathType Leaf $path)) {
        $missingRequired += $name
        continue
    }

    $item = Get-Item $path
    Write-Host "Package contains $($item.Name) ($($item.Length) bytes) at $($item.FullName)"
}

if ($missingRequired.Count -gt 0) {
    Write-Host ("ERROR: Portable package is missing OpenSSL runtime DLL(s) next to Alpharino.exe: " + ($missingRequired -join ", "))
    exit 1
}

if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: dumpbin was not found. The Visual Studio developer environment is required."
    exit 1
}

$packaged = @{}
Get-ChildItem -Recurse -File -Include *.dll, *.exe $InstallDir | ForEach-Object {
    $packaged[$_.Name.ToLowerInvariant()] = $true
}

$redist = @{}
if ($Env:VCToolsRedistDir -and (Test-Path $Env:VCToolsRedistDir)) {
    Get-ChildItem $Env:VCToolsRedistDir -Recurse -File -Filter *.dll -ErrorAction SilentlyContinue | ForEach-Object {
        $redist[$_.Name.ToLowerInvariant()] = $true
    }
}

$systemDirs = @()
foreach ($dir in @("$Env:SystemRoot\System32", "$Env:SystemRoot\SysWOW64", $Env:SystemRoot)) {
    if ($dir -and (Test-Path $dir)) {
        $systemDirs += $dir
    }
}

function Test-SystemDll([string] $Name) {
    if ($Name -match '^(?i)(api-ms-|ext-ms-)') {
        return $true
    }

    foreach ($dir in $systemDirs) {
        if (Test-Path -PathType Leaf (Join-Path $dir $Name)) {
            return $true
        }
    }

    return $false
}

$failures = @()
$targets = Get-ChildItem -Recurse -File -Include *.dll, *.exe $InstallDir
foreach ($target in $targets) {
    $lines = & dumpbin /DEPENDENTS $target.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: dumpbin failed for $($target.FullName)"
        exit 1
    }

    $deps = $lines |
        Where-Object { $_ -match '^(?!Dump of).+\.dll\s*$' } |
        ForEach-Object { $_.Trim() }

    foreach ($dep in $deps) {
        $key = $dep.ToLowerInvariant()
        if ($packaged.ContainsKey($key)) {
            continue
        }

        if ($redist.ContainsKey($key)) {
            $failures += "$($target.Name) depends on $dep, a Visual C++ redistributable that is not in the package"
            continue
        }

        if (Test-SystemDll $dep) {
            continue
        }

        $failures += "$($target.Name) depends on $dep, which is not in the package or the Windows system directory"
    }
}

if ($failures.Count -gt 0) {
    $failures | Sort-Object -Unique | ForEach-Object { Write-Host $_ }
    Write-Host "ERROR: Windows package is missing non-system DLL dependencies."
    exit 1
}

Write-Host "Windows package dependency check passed."
