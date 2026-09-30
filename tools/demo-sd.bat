@echo off
setlocal
set _PORT=%1
if "%_PORT%"=="" set _PORT=COM7
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0demo-sd.ps1" -Port %_PORT%
echo.
pause
exit /b %ERRORLEVEL%
