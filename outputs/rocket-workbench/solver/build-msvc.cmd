@echo off
setlocal
cd /d "%~dp0"
if not defined VCToolsInstallDir (
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_HOME=%%I"
)
if defined VS_HOME call "%VS_HOME%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
where cl >nul 2>nul || (echo Microsoft C++ x64 tools were not found & exit /b 1)
if not exist build\msvc mkdir build\msvc
set "includes=/Isrc/chamber /Isrc/nozzle /Isrc/products /Isrc/presets"
cl /nologo /std:c11 /utf-8 /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS %includes% /Fobuild\msvc\ /c src\chamber\*.c src\nozzle\*.c src\products\*.c src\presets\*.c apps\nozzle_cli.c || exit /b 1
cl /nologo /utf-8 /O2 /W3 /MT /DTHERMO_NO_MAIN %includes% /Fobuild\msvc\ /c src\chamber\thermo_properties.cpp || exit /b 1
link /nologo /OUT:rocket_thermo.exe build\msvc\*.obj || exit /b 1
