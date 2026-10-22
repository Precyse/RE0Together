@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
set CMAKE_DIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake
cd /d %~dp0
if not exist build\build.ninja "%CMAKE_DIR%\CMake\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%CMAKE_DIR%\Ninja\ninja.exe" 2>&1
"%CMAKE_DIR%\CMake\bin\cmake.exe" --build build 2>&1
if errorlevel 1 exit /b 1
if not defined SR2_LUA_SOURCE goto done
if not defined SR2_LUA_FIXES (echo SR2_LUA_FIXES is not set: the folder with Juiced TU3 Mission Fixes & exit /b 1)
python ..\..\tools\sr2\lua_overrides.py "%SR2_LUA_SOURCE%" "%SR2_LUA_FIXES%" build\coop\scripts
if errorlevel 1 exit /b 1
python ..\..\tools\sr2\lua_delta.py "%SR2_LUA_SOURCE%" "%SR2_LUA_FIXES%" build\coop\scripts scripts.delta.json
if errorlevel 1 exit /b 1
:done
echo BUILD OK
