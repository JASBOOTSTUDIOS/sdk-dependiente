@echo off
setlocal
echo ==========================================================
echo    CONSTRUCTOR DE INSTALADOR JASBOOT v0.0.1
echo ==========================================================
echo.

:: Verificar si NSIS está disponible
where makensis >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] NSIS (makensis) no encontrado en el PATH.
    echo Por favor, instala NSIS desde: https://nsis.sourceforge.io/
    pause
    exit /b 1
)

:: Verificar si IExpress está disponible
where iexpress >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] IExpress no encontrado.
    echo IExpress deberia estar incluido en Windows.
    pause
    exit /b 1
)

echo [+] Verificando archivos fuente...

:: Verificar archivos necesarios
if not exist "jasboot_installer.nsi" (
    echo [ERROR] No se encuentra jasboot_installer.nsi
    exit /b 1
)

if not exist "instalar_jasboot.bat" (
    echo [ERROR] No se encuentra instalar_jasboot.bat
    exit /b 1
)

if not exist "package.sed" (
    echo [ERROR] No se encuentra package.sed
    exit /b 1
)

if not exist "README.md" (
    echo [ERROR] No se encuentra README.md
    exit /b 1
)

if not exist "verify_installation.bat" (
    echo [ERROR] No se encuentra verify_installation.bat
    exit /b 1
)

if not exist "img\jasboot-icon.ico" (
    echo [ERROR] No se encuentra img\jasboot-icon.ico
    exit /b 1
)

echo [+] Archivos fuente verificados.

:: Opción de compilación
echo.
echo Seleccione el metodo de compilacion:
echo 1. NSIS (Recomendado - Instalador profesional)
echo 2. IExpress (Auto-extraible simple)
echo.
set /p method="Elija una opcion (1 o 2): "

if "%method%"=="1" goto build_nsis
if "%method%"=="2" goto build_iexpress
goto invalid_option

:build_nsis
echo.
echo [+] Compilando instalador NSIS...
makensis jasboot_installer.nsi

if %errorlevel% equ 0 (
    echo.
    echo [+] Instalador NSIS creado exitosamente!
    echo     Archivo: JasbootSetup-v0.0.1.exe
    echo.
    echo Para probar el instalador, ejecuta:
    echo     JasbootSetup-v0.0.1.exe
) else (
    echo [ERROR] Error en la compilacion NSIS
    pause
    exit /b 1
)
goto end

:build_iexpress
echo.
echo [+] Creando instalador IExpress...
iexpress /N /Q package.sed

if %errorlevel% equ 0 (
    echo.
    echo [+] Instalador IExpress creado exitosamente!
    echo     Archivo: JasbootSetup-v0.0.1.exe
    echo.
    echo Para probar el instalador, ejecuta:
    echo     JasbootSetup-v0.0.1.exe
) else (
    echo [ERROR] Error en la creacion IExpress
    pause
    exit /b 1
)
goto end

:invalid_option
echo [ERROR] Opcion no valida. Por favor, seleccione 1 o 2.
pause
exit /b 1

:end
echo.
echo ==========================================================
echo       CONSTRUCCION COMPLETADA
echo ==========================================================
echo.
echo Archivos generados:
if exist "JasbootSetup-v0.0.1.exe" (
    echo   - JasbootSetup-v0.0.1.exe (Instalador completo)
)

echo.
echo Para distribuir el instalador:
echo 1. Copia JasbootSetup-v0.0.1.exe
echo 2. Incluye el README.md para referencia
echo 3. Incluye verify_installation.bat para verificación post-instalación
echo 4. Asegurate de que los usuarios ejecuten como administrador
echo.
echo Comandos disponibles después de la instalación:
echo   - jbc --help (desde cualquier terminal)
echo   - jasboot --help (alias del compilador)
echo   - jasboot-ir-vm --help (máquina virtual)
echo.
echo Para verificar la instalación:
echo   verify_installation.bat
echo.
pause
