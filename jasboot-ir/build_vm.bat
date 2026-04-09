@echo off
setlocal EnableExtensions
set "IR_DIR=%~dp0"
pushd "%IR_DIR%" >nul

REM sdk-dependiente (hermano de jasboot-jmn-core) y raíz del monorepo
pushd "%IR_DIR%\.." >nul
set "SDK_DEPENDIENTE=%CD%"
popd >nul
pushd "%IR_DIR%\..\.." >nul
set "REPO_ROOT=%CD%"
popd >nul

if defined JASBOOT_JMN_ROOT (
  set "JMN_PKG=%JASBOOT_JMN_ROOT%"
) else if exist "%SDK_DEPENDIENTE%\jasboot-jmn-core\src\memoria_neuronal\memoria_neuronal.h" (
  set "JMN_PKG=%SDK_DEPENDIENTE%\jasboot-jmn-core"
) else if exist "%REPO_ROOT%\jasboot-jmn-core\src\memoria_neuronal\memoria_neuronal.h" (
  set "JMN_PKG=%REPO_ROOT%\jasboot-jmn-core"
) else if exist "%REPO_ROOT%\core\src\memoria_neuronal\memoria_neuronal.h" (
  set "JMN_PKG=%REPO_ROOT%\core"
) else (
  echo ERROR: No se encontraron fuentes JMN. Define JASBOOT_JMN_ROOT o coloca sdk-dependiente\jasboot-jmn-core.
  popd >nul
  exit /b 1
)

set BUILD_DIR=build
set SRC_DIR=src
set JMN_DIR=%JMN_PKG%\src\memoria_neuronal
set COMPAT_SRC=%JMN_PKG%\src\platform_compat.c
set BIN_DIR=bin
set CFLAGS=-Wall -Wextra -std=c11 -O3 -flto -march=native -I%SRC_DIR% -I%JMN_PKG%\src -I%JMN_PKG%\src\memoria_neuronal -DJASBOOT_LANG_INTEGRATION
set LDLIBS=
if /I "%OS%"=="Windows_NT" set LDLIBS=-lws2_32

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
if not exist %BIN_DIR% mkdir %BIN_DIR%

echo Cleaning Build...
del /q %BUILD_DIR%\* >nul 2>&1

echo JMN package: %JMN_PKG%

echo Compiling VM Core...
gcc %CFLAGS% -c %SRC_DIR%/vm.c -o %BUILD_DIR%/vm.o || exit /b 1
gcc %CFLAGS% -c %SRC_DIR%/ir_vm.c -o %BUILD_DIR%/ir_vm.o || exit /b 1
gcc %CFLAGS% -c %SRC_DIR%/ir_format.c -o %BUILD_DIR%/ir_format.o || exit /b 1
gcc %CFLAGS% -c %SRC_DIR%/reader_ir.c -o %BUILD_DIR%/reader_ir.o || exit /b 1
gcc %CFLAGS% -c %SRC_DIR%/cognitive_stubs.c -o %BUILD_DIR%/cognitive_stubs.o || exit /b 1

echo Compiling JMN...
for %%f in (%JMN_DIR%\*.c) do (
    echo Compiling %%~nxf
    gcc %CFLAGS% -c "%%f" -o "%BUILD_DIR%/%%~nf.o" || exit /b 1
)

echo Compiling Compat...
gcc %CFLAGS% -c %COMPAT_SRC% -o %BUILD_DIR%/jmn_compat.o || exit /b 1

echo Linking...
gcc -O3 -flto -march=native %BUILD_DIR%/*.o -o %BIN_DIR%/jasboot-ir-vm-trace.exe %LDLIBS% || exit /b 1
copy /Y %BIN_DIR%\jasboot-ir-vm-trace.exe %BIN_DIR%\jasboot-ir-vm.exe >nul
if errorlevel 1 (
  echo AVISO: no se pudo copiar a jasboot-ir-vm.exe ^(posible bloqueo^). Ejecutable actualizado: jasboot-ir-vm-trace.exe
)

echo Build Successful.
popd >nul
endlocal
