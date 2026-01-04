@echo off

set preset="ALL"
if NOT "%1" == "" (
    set preset=%1
)

echo Running preset %preset%

cmake -S . --preset=%preset% --check-stamp-file "build\%preset%\CMakeFiles\generate.stamp"
if %ERRORLEVEL% NEQ 0 exit 1
cmake --preset %PRESET% -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
if %ERRORLEVEL% NEQ 0 exit 1
