@echo off
echo Creating Jasboot SDK Installer v0.0.1
echo.

echo Checking required files...
if not exist "bin\jbc.exe" (
    echo ERROR: bin\jbc.exe not found
    pause
    exit /b 1
)

if not exist "img\jasboot-icon.ico" (
    echo ERROR: img\jasboot-icon.ico not found
    pause
    exit /b 1
)

echo Creating ZIP installer...
powershell -Command "Compress-Archive -Path '.' -DestinationPath 'Jasboot-SDK-v0.0.1.zip' -Force"

echo.
echo Installer created: Jasboot-SDK-v0.0.1.zip
echo.
echo Users can:
echo 1. Download and unzip Jasboot-SDK-v0.0.1.zip
echo 2. Run instalar_jasboot.bat as administrator
echo 3. Use jbc from any terminal
echo.
pause
