@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
  echo VCVARS FAILED
  exit /b 1
)
echo === CONFIGURING ===
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
if errorlevel 1 (
  echo CONFIGURE FAILED
  exit /b 1
)
echo === BUILDING ===
cmake --build build --target VialPlugin_Standalone --parallel
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo === DONE ===
