@echo off
setlocal

REM Put this repo's CMake-3 wrapper first on PATH for this VS instance only
set "PATH=%~dp0tools\cmake3;%PATH%"

REM Show what will be used
echo === cmake resolution ===
where cmake
cmake --version
echo.

REM Find Visual Studio's devenv.exe via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere not found at "%VSWHERE%".
  echo Install/repair Visual Studio Installer.
  pause
  exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property productPath`) do set "DEVENV=%%I"

if not exist "%DEVENV%" (
  echo ERROR: devenv.exe not found via vswhere.
  echo Got: "%DEVENV%"
  pause
  exit /b 1
)

echo Launching:
echo "%DEVENV%" "%~dp0"
start "" "%DEVENV%" "%~dp0"

endlocal
