$repo = Split-Path -Parent $MyInvocation.MyCommand.Path

# Force this repo to resolve `cmake` to your wrapper first
$env:PATH = Join-Path $repo "tools\cmake3" | ForEach-Object { "$_;$env:PATH" }

Write-Host "=== cmake resolution ==="
where.exe cmake
cmake --version
Write-Host ""

# Find Visual Studio (Community/Pro/Enterprise) via vswhere
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) {
  throw "vswhere not found at: $vswhere"
}

$devenv = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property productPath
if ([string]::IsNullOrWhiteSpace($devenv) -or !(Test-Path $devenv)) {
  # If you have VS Preview only, try prerelease
  $devenv = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -property productPath
}

if ([string]::IsNullOrWhiteSpace($devenv) -or !(Test-Path $devenv)) {
  throw "Could not locate devenv.exe via vswhere."
}

Write-Host "Launching VS: $devenv"
Start-Process -FilePath $devenv -ArgumentList @("$repo")
