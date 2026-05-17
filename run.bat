@echo off
setlocal

echo.
echo [Run] Starting PathtracerBenchmark...

if not defined BUILD_TYPE set "BUILD_TYPE=Release"

set "EXE_PATH="

if /i "%GENERATOR_TYPE%"=="multi" (
    if exist "build\%BUILD_TYPE%\PathtracerBenchmark.exe" set "EXE_PATH=build\%BUILD_TYPE%\PathtracerBenchmark.exe"
)

if not defined EXE_PATH if exist "build\%BUILD_TYPE%\PathtracerBenchmark.exe" set "EXE_PATH=build\%BUILD_TYPE%\PathtracerBenchmark.exe"
if not defined EXE_PATH if exist "build\x64\%BUILD_TYPE%\PathtracerBenchmark.exe" set "EXE_PATH=build\x64\%BUILD_TYPE%\PathtracerBenchmark.exe"
if not defined EXE_PATH if exist "build\PathtracerBenchmark.exe" set "EXE_PATH=build\PathtracerBenchmark.exe"

if not defined EXE_PATH (
    echo [Error] Executable not found. Expected one of:
    echo   build\%BUILD_TYPE%\PathtracerBenchmark.exe
    echo   build\x64\%BUILD_TYPE%\PathtracerBenchmark.exe
    echo   build\PathtracerBenchmark.exe
    echo [Hint] Run auto-setup.bat to configure and build.
    exit /b 1
)

"%EXE_PATH%"

pause