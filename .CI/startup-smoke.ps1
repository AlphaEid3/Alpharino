param (
    [Parameter(Mandatory = $true)]
    [string] $InstallDir
)

$ErrorActionPreference = "Stop"

$source = (Resolve-Path $InstallDir).Path
$root = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$dest = Join-Path $root "alpharino-startup-smoke"
if (Test-Path $dest) {
    Remove-Item -Recurse -Force $dest
}
Copy-Item -Recurse $source $dest

$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText((Join-Path $dest "modes"), "portable`r`n", $utf8)

$settingsDir = Join-Path $dest "Settings"
New-Item -ItemType Directory -Force -Path $settingsDir | Out-Null

# Two non-anonymous accounts and two visible Twitch splits. That is the
# startup path that shows the per-split "as name" button.
$settings = @'
{
    "accounts": {
        "current": "smokealice",
        "uid900001": {
            "username": "smokealice",
            "userID": "900001",
            "clientID": "smokeclient",
            "oauthToken": "oauth:smokealice"
        },
        "uid900002": {
            "username": "smokebob",
            "userID": "900002",
            "clientID": "smokeclient",
            "oauthToken": "oauth:smokebob"
        }
    }
}
'@
$layout = @'
{
    "windows": [
        {
            "type": "main",
            "x": 40,
            "y": 40,
            "width": 1100,
            "height": 760,
            "tabs": [
                {
                    "title": "smoke",
                    "selected": true,
                    "highlightsEnabled": true,
                    "splits2": {
                        "type": "vertical",
                        "flexh": 1,
                        "flexv": 1,
                        "items": [
                            {
                                "type": "split",
                                "flexh": 1,
                                "flexv": 1,
                                "data": {
                                    "type": "twitch",
                                    "name": "twitch"
                                }
                            },
                            {
                                "type": "split",
                                "flexh": 1,
                                "flexv": 1,
                                "sendAccount": "smokebob",
                                "data": {
                                    "type": "twitch",
                                    "name": "qa_stream"
                                }
                            }
                        ]
                    }
                }
            ]
        }
    ]
}
'@

[System.IO.File]::WriteAllText((Join-Path $settingsDir "settings.json"), $settings.Trim() + "`n", $utf8)
[System.IO.File]::WriteAllText((Join-Path $settingsDir "window-layout.json"), $layout.Trim() + "`n", $utf8)

$exe = Join-Path $dest "Alpharino.exe"
if (-not (Test-Path -PathType Leaf $exe)) {
    Write-Host "ERROR: $exe was not found"
    exit 1
}

$stdout = Join-Path $dest "smoke-stdout.txt"
$stderr = Join-Path $dest "smoke-stderr.txt"
# The GitHub-hosted runner has a desktop session. The windows platform runs
# the same Qt GUI path that overflows on launch; offscreen is not deployed.
$env:QT_QPA_PLATFORM = "windows"

$proc = Start-Process -FilePath $exe -WorkingDirectory $dest -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
Write-Host "Launched $($exe) pid $($proc.Id)"

$stillRunning = -not $proc.WaitForExit(15000)
if ($stillRunning) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Get-Process -Name Alpharino -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Write-Host "Startup smoke test passed: Alpharino was still running after 15s with two splits and two accounts."
    exit 0
}

$proc.Refresh()
$code = $proc.ExitCode
if ($null -eq $code) {
    $code = -1
}
$hex = "0x{0:X8}" -f ([uint32]$code)
Write-Host "ERROR: Alpharino exited during startup. Exit code $code ($hex)."
foreach ($log in @($stdout, $stderr)) {
    if (Test-Path $log) {
        Write-Host "----- $log -----"
        Get-Content $log -Tail 120
    }
}
exit 1
