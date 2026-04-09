@echo off
set "HERE=%~dp0"
set "JBC=%HERE%..\jas-compiler-c\bin\jbc.exe"
if exist "%JBC%" (
  "%JBC%" %*
  exit /b %ERRORLEVEL%
)
echo [jbc] No existe: %JBC%
echo       Ejecute: scripts\build-compiler.bat  o  scripts\build-all.bat
exit /b 1
