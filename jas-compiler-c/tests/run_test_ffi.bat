@echo off
REM Test completo FFI: compila test_ffi.jasb y ejecuta con la VM.
REM Ejecutar desde la raiz del repo: sdk-dependiente\jas-compiler-c\tests\run_test_ffi.bat
REM O desde aqui: run_test_ffi.bat (requiere que bin\jbc y VM esten construidos)

setlocal
set "ROOT=%~dp0..\..\.."
set "JBC=%ROOT%\bin\jbc.exe"
set "VM=%ROOT%\sdk-dependiente\jasboot-ir\bin\jasboot-ir-vm-trace.exe"
set "TEST_JASB=%~dp0test_ffi.jasb"
set "TEST_JBO=%~dp0test_ffi.jbo"

if not exist "%JBC%" (
    echo [ERROR] Compilador no encontrado: %JBC%
    echo Ejecute primero: sdk-dependiente\jas-compiler-c\build.bat
    exit /b 1
)
if not exist "%VM%" (
    echo [ERROR] VM no encontrada: %VM%
    echo Ejecute primero: sdk-dependiente\jasboot-ir\build_vm.bat
    exit /b 1
)

echo === Test FFI (ffi_cargar, ffi_simbolo, ffi_llamar) ===
echo Compilando %TEST_JASB% ...
"%JBC%" "%TEST_JASB%" -o "%TEST_JBO%"
if errorlevel 1 (
    echo [FALLO] Compilacion
    exit /b 1
)
echo Ejecutando VM ...
"%VM%" "%TEST_JBO%"
set EXIT=%errorlevel%
del "%TEST_JBO%" 2>nul
echo.
if %EXIT% equ 0 (echo [OK] Test FFI pasado.) else (echo [FALLO] VM exit code %EXIT%.)
exit /b %EXIT%
