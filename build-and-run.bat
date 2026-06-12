@echo off
echo ========================================
echo Vial Build Script for JUCE 8 Testing
echo ========================================
echo.

REM Clean old build
echo Cleaning build directory...
if exist build rmdir /s /q build

REM Setup Visual Studio environment
echo Initializing Visual Studio 2026 environment...
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Configure with CMake
echo Configuring CMake with Ninja generator...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo Building Vial standalone...
cmake --build build --config Debug --target VialPlugin_Standalone -j8
if errorlevel 1 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build complete!
echo Standalone executable should be at:
echo build\VialPlugin_Standalone_artefacts\Debug\Vial.exe
echo ========================================
echo.
pause
