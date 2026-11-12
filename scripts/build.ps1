$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$bundledBin = Join-Path (Get-Location) '.tools\msys64\ucrt64\bin'
if (Test-Path $bundledBin) { $env:Path = $bundledBin + ';' + $env:Path }
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build build
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir build --output-on-failure
exit $LASTEXITCODE
