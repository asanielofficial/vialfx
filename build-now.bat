@echo off
echo ========================================
echo Vial Build (no pause)
echo ========================================

call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64

echo Configuring CMake...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo CMake configuration failed!
    exit /b 1
)

echo Building Vial standalone...
cmake --build build --config Debug --target VialPlugin_Standalone -j8
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo ========================================
echo Build complete! Launching...
echo ========================================

start "" "build\VialPlugin_Standalone_artefacts\Debug\Vial.exe"
