@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
