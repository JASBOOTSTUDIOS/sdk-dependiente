@echo off
setlocal
echo ==========================================================
echo    INSTALADOR RAPIDO DE JASBOOT v0.0.1 (WINDOWS)
echo ==========================================================
echo.
echo Este script configurara Jasboot SDK v0.0.1 en tu equipo.
echo Incluye compilador, runtime, extension VSCode y ejemplos.
echo.

:: 1. Definir rutas
set "INSTALL_DIR=%PROGRAMFILES%\Jasboot"
set "BIN_DIR=%INSTALL_DIR%\bin"
set "RUNTIME_DIR=%INSTALL_DIR%\runtime"
set "IMG_DIR=%INSTALL_DIR%\img"
set "DOCS_DIR=%INSTALL_DIR%\docs"
set "EXAMPLES_DIR=%INSTALL_DIR%\examples"
set "VSCODE_DIR=%INSTALL_DIR%\vscode"

:: 2. Pedir confirmacion
set /p confirm="Deseas instalar Jasboot SDK v0.0.1 en %INSTALL_DIR%? (S/N): "
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

:: 4. Crear estructura de directorios
echo [+] Creando estructura de directorios...
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
if not exist "%RUNTIME_DIR%" mkdir "%RUNTIME_DIR%"
if not exist "%IMG_DIR%" mkdir "%IMG_DIR%"
if not exist "%DOCS_DIR%" mkdir "%DOCS_DIR%"
if not exist "%EXAMPLES_DIR%" mkdir "%EXAMPLES_DIR%"
if not exist "%VSCODE_DIR%" mkdir "%VSCODE_DIR%"

:: 5. Copiar archivos del compilador
echo [+] Copiando compilador Jasboot...
copy /y "bin\jbc.exe" "%BIN_DIR%\" >nul
copy /y "bin\jbc-c.exe" "%BIN_DIR%\" >nul
copy /y "bin\jbc-next.exe" "%BIN_DIR%\" >nul
copy /y "bin\jbc-cursor.exe" "%BIN_DIR%\" >nul

:: 6. Copiar runtime
echo [+] Copiando runtime Jasboot IR...
copy /y "runtime\jasboot-ir-vm.exe" "%RUNTIME_DIR%\" >nul
copy /y "runtime\jasboot-ir-vm-trace.exe" "%RUNTIME_DIR%\" >nul

:: 7. Copiar documentación y recursos
echo [+] Copiando documentación y recursos...
copy /y "docs\README.md" "%DOCS_DIR%\" >nul
copy /y "docs\SDK_README.md" "%DOCS_DIR%\" >nul
copy /y "img\jasboot-icon.ico" "%IMG_DIR%\" >nul
copy /y "img\jasboot-icon.png" "%IMG_DIR%\" >nul
copy /y "README.md" "%INSTALL_DIR%\" >nul

:: 8. Copiar ejemplos
echo [+] Copiando ejemplos y plantillas...
xcopy /E /I /Y "examples\*" "%EXAMPLES_DIR%\" >nul 2>&1

:: 9. Copiar extensión VSCode
echo [+] Copiando extensión VSCode...
copy /y "jasboot-0.0.7.vsix" "%VSCODE_DIR%\" >nul

:: 10. Configurar variables de entorno (OBLIGATORIO)
echo [+] Configurando variables de entorno del sistema...

:: Obtener PATH actual del sistema
for /f "tokens=*" %%a in ('powershell -Command "[Environment]::GetEnvironmentVariable('Path', 'Machine')"') do set CURRENT_PATH=%%a

:: Añadir bin al PATH con prioridad alta (al inicio)
echo %CURRENT_PATH% | findstr /i /c:"%BIN_DIR%" >nul
if %errorlevel% neq 0 (
    echo [+] Añadiendo %BIN_DIR% al PATH (prioridad alta)...
    setx /M PATH "%BIN_DIR%;%CURRENT_PATH%"
    set NEW_PATH="%BIN_DIR%;%CURRENT_PATH%"
) else (
    echo [!] %BIN_DIR% ya existe en el PATH
    set NEW_PATH="%CURRENT_PATH%"
)

:: Añadir runtime al PATH si no existe
echo %NEW_PATH% | findstr /i /c:"%RUNTIME_DIR%" >nul
if %errorlevel% neq 0 (
    echo [+] Añadiendo %RUNTIME_DIR% al PATH...
    setx /M PATH "%NEW_PATH%;%RUNTIME_DIR%"
) else (
    echo [!] %RUNTIME_DIR% ya existe en el PATH
)

:: Crear variable JASBOOT_HOME
setx /M JASBOOT_HOME "%INSTALL_DIR%"
echo [+] Variable JASBOOT_HOME configurada: %INSTALL_DIR%

:: Crear alias jasboot.exe
if exist "%BIN_DIR%\jbc.exe" (
    if not exist "%BIN_DIR%\jasboot.exe" (
        echo [+] Creando alias jasboot.exe...
        copy /y "%BIN_DIR%\jbc.exe" "%BIN_DIR%\jasboot.exe" >nul
    )
)

:: Forzar actualización de variables de entorno en el sistema
echo [+] Notificando al sistema sobre cambios en variables de entorno...
powershell -Command "
    [System.Environment]::SetEnvironmentVariable('Path', '%NEW_PATH%;%RUNTIME_DIR%', 'Machine')
    [System.Environment]::SetEnvironmentVariable('JASBOOT_HOME', '%INSTALL_DIR%', 'Machine')
    $sig = Get-EventLog -LogName Application -Source 'Jasboot Installer' -Newest 1 -ErrorAction SilentlyContinue
    if (-not $sig) { Write-EventLog -LogName Application -Source 'Jasboot Installer' -EventId 1001 -EntryType Information -Message 'Jasboot SDK v0.0.1 variables configuradas' } else { Write-EventLog -LogName Application -Source 'Jasboot Installer' -EventId 1002 -EntryType Information -Message 'Jasboot SDK variables actualizadas' }
" 2>nul

:: 11. Crear accesos directos en el menú inicio
echo [+] Creando accesos directos...
powershell -Command "
    $WshShell = New-Object -comObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut('%PROGRAMDATA%\Microsoft\Windows\Start Menu\Programs\Jasboot SDK\Jasboot Compiler.lnk')
    $Shortcut.TargetPath = '%BIN_DIR%\jbc.exe'
    $Shortcut.WorkingDirectory = '%INSTALL_DIR%'
    $Shortcut.IconLocation = '%IMG_DIR%\jasboot-icon.ico'
    $Shortcut.Save()
"

powershell -Command "
    $WshShell = New-Object -comObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut('%PROGRAMDATA%\Microsoft\Windows\Start Menu\Programs\Jasboot SDK\Jasboot VM.lnk')
    $Shortcut.TargetPath = '%RUNTIME_DIR%\jasboot-ir-vm.exe'
    $Shortcut.WorkingDirectory = '%INSTALL_DIR%'
    $Shortcut.IconLocation = '%IMG_DIR%\jasboot-icon.ico'
    $Shortcut.Save()
"

powershell -Command "
    $WshShell = New-Object -comObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut('%PROGRAMDATA%\Microsoft\Windows\Start Menu\Programs\Jasboot SDK\Ejemplos.lnk')
    $Shortcut.TargetPath = '%EXAMPLES_DIR%'
    $Shortcut.WorkingDirectory = '%EXAMPLES_DIR%'
    $Shortcut.Save()
"

:: 12. Escribir información de instalación
echo [+] Registrando instalación...
echo Jasboot SDK v0.0.1 > "%INSTALL_DIR%\.version"
echo Instalado: %date% %time% >> "%INSTALL_DIR%\.version"
echo Instalador: Batch Script >> "%INSTALL_DIR%\.version"

echo.
echo ==========================================================
echo       INSTALACION COMPLETADA EXITOSAMENTE
echo ==========================================================
echo Version: Jasboot SDK v0.0.1
echo Ubicacion: %INSTALL_DIR%
echo.
echo Componentes instalados:
echo   - Compilador Jasboot (jbc.exe)
echo   - Runtime Jasboot IR (jasboot-ir-vm.exe)
echo   - Extension VSCode
echo   - Ejemplos y plantillas
echo   - Documentacion
echo.
echo Para verificar la instalacion, abre una NUEVA terminal y ejecuta:
echo   jbc --version
echo.
pause
exit /b 0

:cancel
echo.
echo Instalacion cancelada por el usuario.
pause
exit /b 0
