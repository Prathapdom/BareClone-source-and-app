param([string]$Compiler = 'F:\Software\TDM-GCC-64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!(Test-Path -LiteralPath $Compiler)) { $Compiler = (Get-Command g++ -ErrorAction Stop).Source }
    $compilerDir = Split-Path -Parent $Compiler
    $resourceCompiler = Join-Path $compilerDir 'windres.exe'
    New-Item -ItemType Directory -Path 'dist' -Force | Out-Null
    & (Join-Path $PSScriptRoot 'packaging\Make-Assets.ps1')
    & $resourceCompiler app.rc -O coff -o dist/app-res.o
    if ($LASTEXITCODE) { throw 'Resource compilation failed' }
    & $Compiler '-std=c++17' -O3 -DNDEBUG -Wall -Wextra -Wno-misleading-indentation -municode -mwindows -static '-static-libgcc' '-static-libstdc++' main.cpp dist/app-res.o -o dist/BareClone.exe '-lcomctl32' '-lole32' '-luuid' '-lshell32' '-lcomdlg32' '-lgdi32' '-luxtheme' '-ldwmapi'
    if ($LASTEXITCODE) { throw 'Application compilation failed' }
    & $Compiler '-std=c++17' -O2 -Wall -Wextra -Wno-misleading-indentation -static '-static-libgcc' '-static-libstdc++' tests.cpp -o dist/search-tests.exe
    if ($LASTEXITCODE) { throw 'Test compilation failed' }
    & .\dist\search-tests.exe
    if ($LASTEXITCODE) { throw 'Search tests failed' }
    & $Compiler '-std=c++17' -O2 -municode -static ui-tests.cpp -o dist/ui-tests.exe '-lcomctl32'
    if ($LASTEXITCODE) { throw 'UI test compilation failed' }
    & .\dist\ui-tests.exe (Join-Path $PSScriptRoot 'dist\BareClone.exe')
    if ($LASTEXITCODE) { throw 'UI integration tests failed' }
    Write-Host "Built: $PSScriptRoot\dist\BareClone.exe"
} finally { Pop-Location }
