@echo off
setlocal enabledelayedexpansion
echo ==========================================================
echo    VERIFICADOR DE INSTALACION JASBOOT v0.0.1
echo ==========================================================
echo.

echo [+] Verificando instalacion de Jasboot SDK...
echo.

:: 1. Verificar directorio de instalacion
if exist "%PROGRAMFILES%\Jasboot" (
    echo [OK] Directorio de instalacion encontrado: %PROGRAMFILES%\Jasboot
) else (
    echo [ERROR] Directorio de instalacion no encontrado
    goto :error
)

:: 2. Verificar archivos principales
set "missing_files=0"

if exist "%PROGRAMFILES%\Jasboot\bin\jbc.exe" (
    echo [OK] Compilador jbc.exe encontrado
) else (
    echo [ERROR] Compilador jbc.exe NO encontrado
    set /a missing_files+=1
)

if exist "%PROGRAMFILES%\Jasboot\bin\jasboot.exe" (
    echo [OK] Alias jasboot.exe encontrado
) else (
    echo [ADVERTENCIA] Alias jasboot.exe NO encontrado (opcional)
)

if exist "%PROGRAMFILES%\Jasboot\runtime\jasboot-ir-vm.exe" (
    echo [OK] Runtime jasboot-ir-vm.exe encontrado
) else (
    echo [ERROR] Runtime jasboot-ir-vm.exe NO encontrado
    set /a missing_files+=1
)

:: 3. Verificar variables de entorno
echo.
echo [+] Verificando variables de entorno...

:: Verificar PATH
set "path_found=0"
echo %PATH% | findstr /i /c:"%PROGRAMFILES%\Jasboot\bin" >nul
if !errorlevel! equ 0 (
    echo [OK] PATH del sistema contiene bin\Jasboot
    set "path_found=1"
) else (
    echo [ERROR] PATH del sistema NO contiene bin\Jasboot
)

echo %PATH% | findstr /i /c:"%PROGRAMFILES%\Jasboot\runtime" >nul
if !errorlevel! equ 0 (
    echo [OK] PATH del sistema contiene runtime\Jasboot
) else (
    echo [ADVERTENCIA] PATH del sistema NO contiene runtime\Jasboot
)

:: Verificar JASBOOT_HOME
if defined JASBOOT_HOME (
    echo [OK] Variable JASBOOT_HOME definida: %JASBOOT_HOME%
) else (
    echo [ERROR] Variable JASBOOT_HOME NO definida
)

:: 4. Probar ejecución de comandos
echo.
echo [+] Probando ejecucion de comandos...

:: Probar jbc --version
where jbc >nul 2>&1
if !errorlevel! equ 0 (
    echo [OK] Comando 'jbc' accesible globalmente
    echo     Version:
    jbc --version 2>nul
    if !errorlevel! neq 0 (
        echo     (No se pudo obtener la version)
    )
) else (
    echo [ERROR] Comando 'jbc' NO accesible globalmente
    set /a missing_files+=1
)

:: Probar jasboot --version
where jasboot >nul 2>&1
if !errorlevel! equ 0 (
    echo [OK] Comando 'jasboot' accesible globalmente
) else (
    echo [ADVERTENCIA] Comando 'jasboot' NO accesible globalmente
)

:: Probar jasboot-ir-vm
where jasboot-ir-vm >nul 2>&1
if !errorlevel! equ 0 (
    echo [OK] Comando 'jasboot-ir-vm' accesible globalmente
) else (
    echo [ERROR] Comando 'jasboot-ir-vm' NO accesible globalmente
    set /a missing_files+=1
)

:: 5. Crear archivo de prueba
echo.
echo [+] Creando archivo de prueba...
set "test_file=%TEMP%\test_jasboot.jasb"

(
echo // Archivo de prueba para Jasboot v0.0.1
echo clase Test
echo     funcion principal() retorna
echo         imprimir "¡Jasboot funciona correctamente!"
echo         imprimir "Version: 0.0.1"
echo         imprimir "Instalacion: Global PATH"
echo     fin_funcion
echo fin_clase
echo 
echo principal
) > "%test_file%"

echo [+] Archivo de prueba creado: %test_file%

:: 6. Probar compilación y ejecución
echo.
echo [+] Probando compilacion y ejecucion...

where jbc >nul 2>&1
if !errorlevel! equ 0 (
    echo [+] Compilando archivo de prueba...
    jbc "%test_file%" -o "%TEMP%\test_jasboot.jir" 2>nul
    if exist "%TEMP%\test_jasboot.jir" (
        echo [OK] Compilacion exitosa
        
        where jasboot-ir-vm >nul 2>&1
        if !errorlevel! equ 0 (
            echo [+] Ejecutando programa...
            jasboot-ir-vm "%TEMP%\test_jasboot.jir" 2>nul
            echo [OK] Ejecucion completada
        ) else (
            echo [ERROR] No se puede ejecutar (jasboot-ir-vm no encontrado)
        )
        
        :: Limpiar archivos temporales
        del "%TEMP%\test_jasboot.jir" >nul 2>&1
    ) else (
        echo [ERROR] Compilacion fallida
    )
) else (
    echo [ERROR] No se puede probar (jbc no encontrado)
)

:: Limpiar archivo de prueba
del "%test_file%" >nul 2>&1

:: 7. Resumen final
echo.
echo ==========================================================
echo               RESUMEN DE VERIFICACION
echo ==========================================================

if %missing_files% equ 0 (
    echo [EXITO] Jasboot SDK v0.0.1 instalado correctamente!
    echo.
    echo Puedes usar los siguientes comandos desde cualquier terminal:
    echo   - jbc --help
    echo   - jasboot --help  
    echo   - jasboot-ir-vm --help
    echo.
    echo Para crear un nuevo proyecto:
    echo   jbc mi_programa.jasb -e
    echo.
) else (
    echo [ERROR] Se encontraron %missing_files% problemas criticos
    echo.
    echo Soluciones sugeridas:
    if !path_found! equ 0 (
        echo   - Reinstala Jasboot con privilegios de administrador
        echo   - Verifica que las variables de entorno se configuraron
    )
    echo   - Ejecuta este script como administrador
    echo   - Revisa la instalacion manualmente
    echo.
)

echo Variables de entorno actuales:
echo   PATH: contiene Jasboot = !path_found!
echo   JASBOOT_HOME: %JASBOOT_HOME%
echo.

pause
