@echo off
REM Toolchain completo: VM + copia a bin\ + compilador + jbc.exe en bin\
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "SDK_ROOT=%%~fI\"

echo === 1/3 VM (jasboot-ir) ===
call "%~dp0build-vm.bat"
if errorlevel 1 exit /b 1

if not exist "%SDK_ROOT%bin" mkdir "%SDK_ROOT%bin"
copy /Y "%SDK_ROOT%jasboot-ir\bin\jasboot-ir-vm.exe" "%SDK_ROOT%bin\jasboot-ir-vm.exe" >nul 2>&1
if errorlevel 1 (
  copy /Y "%SDK_ROOT%jasboot-ir\bin\jasboot-ir-vm-trace.exe" "%SDK_ROOT%bin\jasboot-ir-vm.exe" >nul 2>&1
)
if exist "%SDK_ROOT%bin\jasboot-ir-vm.exe" (
  echo VM: %SDK_ROOT%bin\jasboot-ir-vm.exe
) else (
  echo AVISO: no se pudo copiar la VM a bin\. Use jasboot-ir\bin\jasboot-ir-vm.exe
)

echo === 2/3 Compilador (jas-compiler-c) ===
call "%~dp0build-compiler.bat"
if errorlevel 1 exit /b 1

echo === 3/3 Listo ===
echo.
echo Binarios en: %SDK_ROOT%bin
echo PATH sugerido:  set "PATH=%SDK_ROOT%bin;%SDK_ROOT%jas-compiler-c\bin;%%PATH%%"
echo Probar:  scripts\build-all.bat  ^(desde la raiz del SDK^)
echo           bin\jbc.cmd
echo           bin\jasboot-ir-vm.cmd --continuo salida.jbo
endlocal
exit /b 0
