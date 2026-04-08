# Script de Instalación de Jasboot para Windows
# Requiere NSIS (Nullsoft Scriptable Install System) para compilar

!include "MUI2.nsh"
!include "EnvVarUpdate.nsh"

Name "Jasboot SDK"
OutFile "JasbootSetup.exe"
InstallDir "$PROGRAMFILES64\Jasboot"
RequestExecutionLevel admin
Icon "img\jasboot-icon.ico" # Se asume conversion o PNG si NSIS lo soporta con plugin

!define MUI_ABORTWARNING
!define MUI_ICON "img\jasboot-icon.ico"

# Paginas del instalador
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

# Idioma
!insertmacro MUI_LANGUAGE "Spanish"

Section "Instalar Jasboot"
    SetOutPath "$INSTDIR\bin"
    File "bin\jbc.exe"
    File "bin\jasboot-ir-vm.exe"
    
    SetOutPath "$INSTDIR\img"
    File "img\jasboot-icon.png"

    # Registrar en el PATH (Pregunta implícita al ser un SDK, o checkbox si se desea)
    # Por defecto lo añadimos al PATH del sistema para que jbc sea global
    ${EnvVarUpdate} $0 "PATH" "A" "HKLM" "$INSTDIR\bin"

    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\bin\jbc.exe"
    Delete "$INSTDIR\bin\jasboot-ir-vm.exe"
    Delete "$INSTDIR\img\jasboot-icon.png"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR\bin"
    RMDir "$INSTDIR\img"
    RMDir "$INSTDIR"

    ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR\bin"
SectionEnd
