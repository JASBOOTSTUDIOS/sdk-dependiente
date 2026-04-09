@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist build (
    echo Ejecuta primero build_vm.bat
    exit /b 1
)

set "IR_DIR=%~dp0"
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
  echo ERROR: No se encontraron fuentes JMN.
  exit /b 1
)

set "JMN_INC=-Isrc -I%JMN_PKG%\src -I%JMN_PKG%\src\memoria_neuronal"
gcc -Wall -std=c11 %JMN_INC% src\test_jmn.c build\memoria_neuronal_core.o build\memoria_neuronal_io.o build\memoria_neuronal_nodos.o build\memoria_neuronal_conexiones.o build\memoria_neuronal_texto_fix.o build\memoria_neuronal_utilidades.o build\memoria_neuronal_estructuras.o build\memoria_neuronal_cognitivo.o build\jmn_compat.o -o bin\test_jmn.exe
if %errorlevel% neq 0 (echo Build FAILED & exit /b 1)
echo test_jmn.exe OK
gcc -Wall -std=c11 %JMN_INC% src\test_jmn_sueno.c build\memoria_neuronal_core.o build\memoria_neuronal_io.o build\memoria_neuronal_nodos.o build\memoria_neuronal_conexiones.o build\memoria_neuronal_texto_fix.o build\memoria_neuronal_utilidades.o build\memoria_neuronal_estructuras.o build\memoria_neuronal_cognitivo.o build\jmn_compat.o -o bin\test_jmn_sueno.exe
if %errorlevel% neq 0 (echo test_jmn_sueno build FAILED & exit /b 1)
echo test_jmn_sueno.exe OK
endlocal
