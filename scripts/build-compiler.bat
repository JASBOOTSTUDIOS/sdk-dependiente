@echo off
REM Compila jbc y copia a sdk-dependiente\bin\ (via JASBOOT_SDK_ROOT).
setlocal
for %%I in ("%~dp0..") do set "SDK_ROOT=%%~fI\"
set "JASBOOT_SDK_ROOT=%SDK_ROOT%"
pushd "%SDK_ROOT%jas-compiler-c" || exit /b 1
call build.bat
set "ERR=%ERRORLEVEL%"
popd
endlocal & exit /b %ERR%
