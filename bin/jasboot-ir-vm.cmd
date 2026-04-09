@echo off
set "HERE=%~dp0"
set "VM=%HERE%..\jasboot-ir\bin\jasboot-ir-vm.exe"
if exist "%VM%" (
  "%VM%" %*
  exit /b %ERRORLEVEL%
)
set "VM=%HERE%jasboot-ir-vm.exe"
if exist "%VM%" (
  "%VM%" %*
  exit /b %ERRORLEVEL%
)
echo [vm] No existe jasboot-ir-vm.exe en ..\jasboot-ir\bin\ ni en bin\
echo      Ejecute: scripts\build-vm.bat  o  scripts\build-all.bat
exit /b 1
