@echo off
setlocal
cd /d "%~dp0"
where node >nul 2>nul || (echo Node.js 20 or newer is required. & pause & exit /b 1)
if not defined ROCKET_WORKBENCH_PORT set "ROCKET_WORKBENCH_PORT=8732"
node server.mjs --open
if errorlevel 1 pause
