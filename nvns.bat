@echo off
:: Check for admin rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Requesting administrator privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

setlocal

REM ======================================================
REM Configuration
REM ======================================================

set "NCU=C:\Program Files\NVIDIA Corporation\Nsight Compute 2025.1.0\ncu"

REM Project root (where this .bat is located)
set "PROJECT_DIR=%~dp0"

REM Executable directory
set "EXE_DIR=%PROJECT_DIR%out\build\x64-Debug"

set "EXE=PathtracerBenchmark.exe"

REM Reports directory
set "OUTPUT_DIR=%EXE_DIR%\NsightReports"

if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
)

REM Timestamp
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set TS=%%i

set "REPORT=%OUTPUT_DIR%\profile-%TS%"

echo.
echo ==========================================
echo Running Nsight Compute...
echo ==========================================
echo.
echo Project Dir : %PROJECT_DIR%
echo Executable  : %EXE_DIR%\%EXE%
echo Working Dir : %EXE_DIR%
echo Report      : %REPORT%.ncu-rep
echo.

REM Change to the executable directory so relative paths work
pushd "%EXE_DIR%"

"%NCU%" ^
    --force-overwrite ^
    --set full ^
    --target-processes all ^
    --export "%REPORT%" ^
    "%EXE%"

popd

echo.
echo Report written to:
echo %REPORT%.ncu-rep

pause