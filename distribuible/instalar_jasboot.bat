@echo off
setlocal
echo ==========================================================
echo       INSTALADOR RAPIDO DE JASBOOT (WINDOWS)
echo ==========================================================
echo.
echo Este script configurara Jasboot en tu equipo de manera global.
echo.

:: 1. Definir rutas
set "INSTALL_DIR=%PROGRAMFILES%\Jasboot"
set "BIN_DIR=%INSTALL_DIR%\bin"
set "IMG_DIR=%INSTALL_DIR%\img"

:: 2. Pedir confirmacion
set /p confirm="Deseas instalar Jasboot en %INSTALL_DIR%? (S/N): "
if /i "%confirm%" neq "S" goto cancel

:: 3. Verificar permisos de administrador
openfiles >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Se requieren permisos de administrador para instalar en Archivos de Programa.
    echo Por favor, ejecuta este instalador como administrador.
    pause
    exit /b 1
)

:: 4. Crear directorios
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
if not exist "%IMG_DIR%" mkdir "%IMG_DIR%"

:: 5. Copiar archivos (usando rutas relativas desde el paquete)
echo [+] Copiando binarios...
copy /y "bin\jbc.exe" "%BIN_DIR%\" >nul
copy /y "bin\jasboot-ir-vm.exe" "%BIN_DIR%\" >nul
echo [+] Copiando recursos visuales...
copy /y "img\jasboot-icon.png" "%IMG_DIR%\" >nul

:: 6. Añadir al PATH del sistema si el usuario lo desea
set /p addpath="Deseas añadir Jasboot al PATH general del equipo? (S/N): "
if /i "%addpath%" == "S" (
    echo [+] Añadiendo %BIN_DIR% al PATH...
    :: Usamos setx /M para el PATH del sistema (requiere admin)
    :: Comprobar si ya existe en el PATH para evitar duplicados
    for /f "tokens=*" %%a in ('powershell -Command "[Environment]::GetEnvironmentVariable('Path', 'Machine')"') do set CURRENT_PATH=%%a
    echo %CURRENT_PATH% | findstr /i /c:"%BIN_DIR%" >nul
    if %errorlevel% neq 0 (
        setx /M PATH "%CURRENT_PATH%;%BIN_DIR%"
    ) else (
        echo [!] La ruta ya existe en el PATH.
    )
)

echo.
echo ==========================================================
echo       INSTALACION COMPLETADA EXITOSAMENTE
echo ==========================================================
echo Ahora puedes usar 'jbc' desde cualquier terminal.
echo.
pause
exit /b 0

:cancel
echo.
echo Instalacion cancelada por el usuario.
pause
exit /b 0
