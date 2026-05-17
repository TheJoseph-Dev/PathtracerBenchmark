@echo off
setlocal EnableDelayedExpansion

:: ---- Settings ----
set "ARCHITECTURE=x64"
set "BUILD_TYPE=Debug"

:: ----------------------------------------
:: Detect Generator
:: ----------------------------------------

echo [Detect] Searching for available generators...

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

where devenv >nul 2>nul
if %ERRORLEVEL%==0 (
    set "GENERATOR=Visual Studio 17 2022"
    set "GENERATOR_TYPE=multi"
    goto generator_found
)

echo [Error] No supported generator found.
echo.
echo Install one of:
echo   - Ninja
echo   - MinGW
echo   - Visual Studio Build Tools
exit /b 1

:generator_found

echo [Detect] Using generator: %GENERATOR%

:: ----------------------------------------
:: Create project folders
:: ----------------------------------------

echo [Setup] Creating project folders...

if not exist src (
    mkdir src || (
        echo [Error] Failed to create src directory.
        exit /b 1
    )
)

if not exist vendor (
    mkdir vendor || (
        echo [Error] Failed to create vendor directory.
        exit /b 1
    )
)

:: ----------------------------------------
:: Download GLFW
:: ----------------------------------------
if exist vendor\glm (
    echo [Cleanup] Removing existing GLM folder...

    rmdir /s /q vendor\glm

    if exist vendor\glm (
        echo [Error] Failed to remove vendor\glm
        exit /b 1
    )
)

echo [Clone] Cloning GLM...

git clone --depth 1 https://github.com/g-truc/glm vendor/glm || (
    echo [Error] Failed to clone GLM.
    exit /b 1
)

:: ----------------------------------------
:: Create build directory
:: ----------------------------------------

if not exist build (
    mkdir build || (
        echo [Error] Failed to create build directory.
        exit /b 1
    )
)

cd build || (
    echo [Error] Failed to change to build directory.
    exit /b 1
)

:: ----------------------------------------
:: Configure
:: ----------------------------------------

echo [CMake] Configuring project...

if "%GENERATOR_TYPE%"=="multi" (

    cmake .. ^
        -G "%GENERATOR%" ^
        -A %ARCHITECTURE%

) else (

    cmake .. ^
        -G "%GENERATOR%" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

)

if errorlevel 1 (
    echo [Error] CMake configuration failed.
    exit /b 1
)

:: ----------------------------------------
:: Build
:: ----------------------------------------

echo [Build] Building...

if "%GENERATOR_TYPE%"=="multi" (

    cmake --build . --config %BUILD_TYPE%

) else (

    cmake --build .

)

if errorlevel 1 (
    echo [Error] Build failed.
    exit /b 1
)

echo.
echo [Done] Vulkan app built successfully!
echo.
echo Executable should be inside:
echo   build/
echo.
pause