param([string]$Destination = 'C:\Users\vr18644\bareclone')
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$target = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
if ($target -eq $source) { Write-Host "Already installed in $target"; exit 0 }
if ($target.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Destination must be outside the source folder.' }
if (Test-Path -LiteralPath $target) {
    if (Get-ChildItem -LiteralPath $target -Force | Select-Object -First 1) { throw "Destination is not empty. Choose an empty folder to avoid overwriting files: $target" }
} else { New-Item -ItemType Directory -Path $target | Out-Null }
Get-ChildItem -LiteralPath $source -Force | Where-Object { $_.Extension -ne '.zip' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse
}
Write-Host "App and source copied to: $target"
Write-Host "Run: $target\dist\BareClone.exe"
