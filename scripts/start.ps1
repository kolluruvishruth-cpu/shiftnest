param([switch]$Local, [switch]$Migrate, [switch]$Seed)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$bundledBin = Join-Path (Get-Location) '.tools\msys64\ucrt64\bin'
if (Test-Path $bundledBin) { $env:Path = $bundledBin + ';' + $env:Path }
if ($Local) {
  if (-not (Test-Path '.tools\pgdata\PG_VERSION')) { throw 'Local cluster is missing. See README local PostgreSQL instructions.' }
  & pg_ctl.exe -D .tools/pgdata status *> $null
  if ($LASTEXITCODE) { & pg_ctl.exe -D .tools/pgdata -l .tools/postgres.log -o '-h 127.0.0.1 -p 55432' start }
  $env:DATABASE_URL = 'postgresql://shiftnest@127.0.0.1:55432/shiftnest?sslmode=disable'
  Write-Host 'Using local development PostgreSQL. Neon is not used in this mode.'
} else {
  Remove-Item Env:DATABASE_URL -ErrorAction SilentlyContinue
  Write-Host 'Using DATABASE_URL from the private .env file.'
}
if ($Migrate) { & .\build\shiftnest.exe --migrate; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
if ($Seed) { & .\build\shiftnest.exe --seed; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
& .\build\shiftnest.exe
exit $LASTEXITCODE
