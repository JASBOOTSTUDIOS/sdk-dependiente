@echo off
echo ==========================================================
echo    CREADOR DE INSTALADOR JASBOOT v0.0.1
echo ==========================================================
echo.

echo [+] Verificando archivos necesarios...
if not exist "bin\jbc.exe" (
    echo [ERROR] No se encuentra bin\jbc.exe
    pause
    exit /b 1
)

if not exist "img\jasboot-icon.ico" (
    echo [ERROR] No se encuentra img\jasboot-icon.ico
    pause
    exit /b 1
)

echo [+] Creando instalador auto-extraíble...

:: Usar 7-Zip si está disponible, si no crear un simple ZIP
where 7z >nul 2>&1
if %errorlevel% equ 0 (
    echo [+] Usando 7-Zip para crear SFX con icono...
    7z a -sfx7z.sfx -r "Jasboot-SDK-v0.0.1.7z" *.*
    echo [+] Archivo 7z creado: Jasboot-SDK-v0.0.1.7z
    echo [+] NOTA: Para agregar icono, use 7-Zip SFX Maker manualmente
) else (
    echo [+] Creando ZIP simple (sin icono)...
    powershell -Command "Compress-Archive -Path '.' -DestinationPath 'JasbootSetup-v0.0.1.zip' -Force"
    echo [+] ZIP creado: JasbootSetup-v0.0.1.zip (sin icono)
    echo [!] NOTA: Instale 7-Zip para crear instalador con icono
)

echo.
echo ==========================================================
echo               INSTALADOR CREADO
echo ==========================================================
echo.
if exist "Jasboot-SDK-v0.0.1.7z" (
    echo [✓] Instalador SFX: Jasboot-SDK-v0.0.1.7z
) else if exist "JasbootSetup-v0.0.1.zip" (
    echo [✓] Instalador ZIP: JasbootSetup-v0.0.1.zip
) else (
    echo [✗] Error: No se pudo crear el instalador
)

echo.
echo Para distribuir:
echo   - Jasboot-SDK-v0.0.1.7z (auto-extraible)
echo   - JasbootSetup-v0.0.1.zip (requiere descomprimir)
echo.
echo Los usuarios pueden ejecutar el archivo directamente para instalar.
echo.
pause
