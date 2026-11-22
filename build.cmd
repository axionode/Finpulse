@echo off
rem Build Finpulse with MSVC (Visual Studio 2022 or Build Tools, workload "Desktop development with C++").
rem Usage: build.cmd [Release|Debug]
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "%VSWHERE%" (
    echo Visual Studio 2022 not found. Install "Build Tools for Visual Studio 2022" with C++ workload.
    exit /b 1
  )
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
  call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
)

cmake -S . -B build\%CFG% -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
cmake --build build\%CFG% || exit /b 1
if not exist dist mkdir dist
copy /y build\%CFG%\Finpulse.exe dist\Finpulse.exe >nul
echo.
echo Done: dist\Finpulse.exe
