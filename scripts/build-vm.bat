@echo off
REM Compila la VM (jasboot-ir-vm); salida en jasboot-ir\bin\
setlocal
for %%I in ("%~dp0..") do set "SDK_ROOT=%%~fI\"
call "%SDK_ROOT%jasboot-ir\build_vm.bat" %*
set "ERR=%ERRORLEVEL%"
endlocal & exit /b %ERR%
