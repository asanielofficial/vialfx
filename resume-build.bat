@echo off
echo ========================================
echo Resuming Vial Build
echo ========================================

call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64

echo.
echo Resuming build from where it left off...
cd /d C:\Code\Personal\vial\build
cmake --build . --config Debug --target VialPlugin_Standalone

echo.
echo Build complete. Exit code: %ERRORLEVEL%
