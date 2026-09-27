# Captures every display mode, the menu and the web pages into docs/screens.
#   powershell -File tools\capture_screens.ps1 -Ip 192.168.1.50 [-WebOnly]
# The board must be online. The display mode is restored afterwards.
# Web pages are rendered with ?demo=1 — network name, IP and MAC are placeholders.
param([Parameter(Mandatory = $true)][string]$Ip, [switch]$WebOnly)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot '..\docs\screens'
New-Item -ItemType Directory -Force $out | Out-Null
$base = "http://$Ip"

function Save-Screen([string]$name) {
    $tmp = Join-Path $env:TEMP "sd_$name.bmp"
    Invoke-WebRequest "$base/screen.bmp" -OutFile $tmp -UseBasicParsing
    $src = [System.Drawing.Image]::FromFile($tmp)
    $dst = New-Object System.Drawing.Bitmap 480, 560        # 2x, nearest neighbour
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = 'NearestNeighbor'; $g.PixelOffsetMode = 'Half'
    $g.DrawImage($src, 0, 0, 480, 560)
    $dst.Save((Join-Path $out "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $src.Dispose(); $dst.Dispose(); Remove-Item $tmp
    Write-Host "  $name.png"
}

function Set-Mode([int]$m) {
    Invoke-WebRequest "$base/mode" -Method Post -Body @{ mode = $m; ajax = 1 } -UseBasicParsing | Out-Null
}

if (-not $WebOnly) {
$status = Invoke-RestMethod "$base/api/status"
$was = [int]$status.mode

# (mode, file name, seconds to wait — network modes need time for data)
$modes = @(
    @(0, 'mode_clock', 4), @(1, 'mode_crypto', 25), @(2, 'mode_space', 3), @(3, 'mode_analog', 3),
    @(4, 'mode_ping', 8), @(5, 'mode_cat', 4), @(6, 'mode_weather', 6), @(7, 'mode_markets', 30)
)
Write-Host 'Modes:'
foreach ($m in $modes) { Set-Mode $m[0]; Start-Sleep $m[2]; Save-Screen $m[1] }

Write-Host 'Menu:'
Invoke-WebRequest "$base/api/menu" -Method Post -Body @{ sel = 6 } -UseBasicParsing | Out-Null
Start-Sleep 1
Save-Screen 'menu'
Set-Mode $was
}

Write-Host 'Web:'
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$pages = @{ web_panel = '/'; web_modes = '/mode'; web_wifi = '/wifi'; web_crypto = '/crypto'; web_system = '/system' }
foreach ($p in $pages.GetEnumerator()) {
    $file = Join-Path (Resolve-Path $out) "$($p.Key).png"
    # Edge reports to stderr — Start-Process keeps that from counting as an error
    Start-Process $edge -Wait -WindowStyle Hidden -ArgumentList @('--headless=new', '--disable-gpu', '--hide-scrollbars',
        '--window-size=900,1100', '--virtual-time-budget=6000', "--screenshot=$file", "$base$($p.Value)?demo=1")
    Write-Host "  $($p.Key).png"
}
Write-Host "Done: $out"
