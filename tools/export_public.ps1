# Copies the publishable part of the project into a separate folder that can
# become its own git repository (this project lives inside a private monorepo).
#   powershell -File tools\export_public.ps1 -Dest C:\path\to\SmartDisplay
# Internal notes (CLAUDE.md) and build output are left out.
param([Parameter(Mandatory = $true)][string]$Dest)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
New-Item -ItemType Directory -Force $Dest | Out-Null

$include = @('src', 'tools', 'docs', 'platformio.ini', 'README.md', 'CHANGELOG.md', 'LICENSE', '.gitignore')
foreach ($item in $include) {
    $src = Join-Path $root $item
    if (Test-Path $src) { Copy-Item $src -Destination $Dest -Recurse -Force }
}
Write-Host "Exported to $Dest"
Write-Host 'Next: cd there, git init, review, commit. Nothing is pushed by this script.'
