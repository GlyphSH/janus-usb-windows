@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0janus.ps1" %*
exit /b %ERRORLEVEL%
