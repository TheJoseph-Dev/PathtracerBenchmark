@echo off
setlocal EnableDelayedExpansion

:: ---- Settings ----
set "ARCHITECTURE=x64"
set "BUILD_TYPE=Release"
set "CMAKE_EXE=cmake"

choice /C YN /N /M "Enable CUDA support? [Y/N]: "
if errorlevel 2 (
    set "ENABLE_CUDA=OFF"
) else (
    set "ENABLE_CUDA=ON"
)

:: ----------------------------------------
:: Locate Visual Studio (if available)
:: ----------------------------------------

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        set "VS_INSTALL=%%i"
    )
)

if defined VS_INSTALL (
    set "VS_DEVENV=%VS_INSTALL%\Common7\IDE\devenv.exe"
    set "VS_CMAKE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

:: ----------------------------------------
:: Detect Generator
:: ----------------------------------------

echo [Detect] Searching for available generators...

if defined VS_DEVENV if exist "%VS_DEVENV%" (
    set "GENERATOR=Visual Studio 17 2022"
    set "GENERATOR_TYPE=multi"
    if defined VS_CMAKE if exist "%VS_CMAKE%" set "CMAKE_EXE=%VS_CMAKE%"
    goto generator_found
)

where devenv >nul 2>nul
if %ERRORLEVEL%==0 (
    set "GENERATOR=Visual Studio 17 2022"
    set "GENERATOR_TYPE=multi"
    goto generator_found
)

where ninja >nul 2>nul
if %ERRORLEVEL%==0 (
    set "GENERATOR=Ninja"
    set "GENERATOR_TYPE=single"
    goto generator_found
)

where mingw32-make >nul 2>nul
if %ERRORLEVEL%==0 (
    set "GENERATOR=MinGW Makefiles"
    set "GENERATOR_TYPE=single"
    goto generator_found
)

echo [Error] No supported generator found.
exit /b 1

:generator_found

echo [Detect] Using generator: %GENERATOR%

:: ----------------------------------------
:: Create folders
:: ----------------------------------------

if not exist src mkdir src
if not exist vendor mkdir vendor

:: ----------------------------------------
:: Clone GLFW
:: ----------------------------------------

if exist vendor\glfw rmdir /s /q vendor\glfw

echo [Clone] GLFW...
git clone --depth 1 https://github.com/glfw/glfw vendor/glfw || exit /b 1

:: ----------------------------------------
:: Clone GLM
:: ----------------------------------------

if exist vendor\glm rmdir /s /q vendor\glm

echo [Clone] GLM...
git clone --depth 1 https://github.com/g-truc/glm vendor/glm || exit /b 1

:: ----------------------------------------
:: Clone Vulkan Headers (NO SDK)
:: ----------------------------------------

if exist vendor\Vulkan-Headers rmdir /s /q vendor\Vulkan-Headers

echo [Clone] Vulkan-Headers...
git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers vendor/Vulkan-Headers || exit /b 1

:: ----------------------------------------
:: Build directory
:: ----------------------------------------

if not exist build mkdir build
cd build

:: ----------------------------------------
:: Configure
:: ----------------------------------------

echo [CMake] Configuring...

if "%GENERATOR_TYPE%"=="multi" (
    if "%ENABLE_CUDA%"=="ON" (
        "%CMAKE_EXE%" .. -G "%GENERATOR%" -A %ARCHITECTURE% -DENABLE_CUDA=%ENABLE_CUDA% -T "cuda=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
    ) else (
        "%CMAKE_EXE%" .. -G "%GENERATOR%" -A %ARCHITECTURE% -DENABLE_CUDA=%ENABLE_CUDA%
    )
) else (
    "%CMAKE_EXE%" .. -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DENABLE_CUDA=%ENABLE_CUDA%
)

if errorlevel 1 exit /b 1

:: ----------------------------------------
:: Build
:: ----------------------------------------

echo [Build] Building...

if "%GENERATOR_TYPE%"=="multi" (
    "%CMAKE_EXE%" --build . --config %BUILD_TYPE%
) else (
    "%CMAKE_EXE%" --build .
)

if errorlevel 1 exit /b 1

echo.
echo [Done] Release build complete
echo.
echo Output is in:
echo   build/

pause