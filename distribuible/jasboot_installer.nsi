# Script de Instalación de Jasboot v0.0.1 para Windows
# Requiere NSIS (Nullsoft Scriptable Install System) para compilar

!include "MUI2.nsh"
!include "EnvVarUpdate.nsh"

# Información del producto
!define PRODUCT_NAME "Jasboot SDK"
!define PRODUCT_VERSION "0.0.1"
!define PRODUCT_PUBLISHER "JASBOOTSTUDIOS"
!define PRODUCT_WEB_SITE "https://github.com/JASBOOTSTUDIOS"

# Constantes para variables de entorno
!define ENV_PATH_SYSTEM "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
!define ENV_PATH_USER   "HKCU\Environment"

# Constantes para mensajes del sistema
!define WM_WININICHANGE 0x001A

Name "${PRODUCT_NAME} v${PRODUCT_VERSION}"
OutFile "JasbootSetup-v0.0.1.exe"
InstallDir "$PROGRAMFILES64\Jasboot"
InstallDirRegKey HKLM "Software\${PRODUCT_NAME}" "InstallPath"
RequestExecutionLevel admin
Icon "img\jasboot-icon.ico"

!define MUI_ABORTWARNING
!define MUI_ICON "img\jasboot-icon.ico"
!define MUI_UNICON "img\jasboot-icon.ico"

# Información de versión
VIProductVersion "0.0.1.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright" "© 2024 ${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "Jasboot SDK v${PRODUCT_VERSION} - Compilador y Runtime"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"

# Paginas del instalador
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

# Idioma
!insertmacro MUI_LANGUAGE "Spanish"

# Desinstalador
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

# Secciones del instalador
Section "Jasboot Compiler (Requerido)" SEC01
    SectionIn RO
    
    SetOutPath "$INSTDIR\bin"
    File "bin\jbc.exe"
    File "bin\jbc-c.exe"
    File "bin\jbc-next.exe"
    File "bin\jbc-cursor.exe"
    
    SetOutPath "$INSTDIR\docs"
    File /nonfatal "docs\README.md"
    
    # Crear accesos directos
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Jasboot Compiler.lnk" "$INSTDIR\bin\jbc.exe" "" "$INSTDIR\img\jasboot-icon.ico"
SectionEnd

Section "Jasboot IR Runtime" SEC02
    SetOutPath "$INSTDIR\runtime"
    File "runtime\jasboot-ir-vm.exe"
    File "runtime\jasboot-ir-vm-trace.exe"
    
    SetOutPath "$INSTDIR\lib"
    File /nonfatal "lib\*.jasb"
    
    # Acceso directo para la VM
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Jasboot VM.lnk" "$INSTDIR\runtime\jasboot-ir-vm.exe" "" "$INSTDIR\img\jasboot-icon.ico"
SectionEnd

Section "VSCode Extension" SEC03
    SetOutPath "$INSTDIR\vscode"
    File "jasboot-0.0.7.vsix"
    
    # Acceso directo para documentación
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\VSCode Extension.lnk" "https://marketplace.visualstudio.com/items?itemName=JASBOOTSTUDIOS.vscode-jasboot"
SectionEnd

Section "Ejemplos y Plantillas" SEC04
    SetOutPath "$INSTDIR\examples"
    File /r "examples\*"
    
    # Acceso directo a ejemplos
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Ejemplos.lnk" "$INSTDIR\examples"
SectionEnd

Section "Configurar PATH del Sistema (Requerido)" SEC05
    SectionIn RO
    
    # Añadir al PATH del sistema (prioridad alta - al inicio)
    ${EnvVarUpdate} $0 "PATH" "X" "HKLM" "$INSTDIR\bin"
    ${EnvVarUpdate} $0 "PATH" "X" "HKLM" "$INSTDIR\runtime"
    
    # Crear variable de entorno JASBOOT_HOME
    WriteRegStr ${ENV_PATH_SYSTEM} "JASBOOT_HOME" "$INSTDIR"
    
    # Crear alias jbc.exe principal si no existe
    IfFileExists "$INSTDIR\bin\jbc.exe" 0 +2
    CopyFiles "$INSTDIR\bin\jbc.exe" "$INSTDIR\bin\jasboot.exe"
    
    # Crear alias para el compilador principal
    IfFileExists "$INSTDIR\bin\jbc.exe" 0 +2
    CreateShortCut "$INSTDIR\bin\jbc.lnk" "$INSTDIR\bin\jbc.exe" "" "$INSTDIR\img\jasboot-icon.ico" 0
    
    # Notificar al sistema sobre cambios en variables de entorno
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    
    # Forzar actualización de variables de entorno en procesos existentes
    System::Call "user32::SendMessageTimeout(i ${HWND_BROADCAST}, i ${WM_WININICHANGE}, i 0, t 'Environment', i 0, i 5000, i *R) .R1"
SectionEnd

# Recursos visuales
Section "-Visual" SEC06
    SetOutPath "$INSTDIR\img"
    File "img\jasboot-icon.ico"
    File "img\jasboot-icon.png"
    
    SetOutPath "$INSTDIR"
    File "README.md"
    
    # Escribir desinstalador
    WriteUninstaller "$INSTDIR\uninstall.exe"
    
    # Escribir claves de registro
    WriteRegStr HKLM "Software\${PRODUCT_NAME}" "InstallPath" "$INSTDIR"
    WriteRegStr HKLM "Software\${PRODUCT_NAME}" "Version" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\${PRODUCT_NAME}" "Publisher" "${PRODUCT_PUBLISHER}"
    
    # Escribir entrada de agregar/quitar programas
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "NoRepair" 1
SectionEnd

# Sección de desinstalación
Section "Uninstall"
    # Eliminar accesos directos
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\*.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
    
    # Eliminar archivos del sistema
    Delete "$INSTDIR\bin\*.exe"
    Delete "$INSTDIR\runtime\*.exe"
    Delete "$INSTDIR\docs\*.md"
    Delete "$INSTDIR\img\*.ico"
    Delete "$INSTDIR\img\*.png"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\uninstall.exe"
    
    # Eliminar directorios
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\runtime"
    RMDir /r "$INSTDIR\docs"
    RMDir /r "$INSTDIR\img"
    RMDir /r "$INSTDIR\vscode"
    RMDir /r "$INSTDIR\examples"
    RMDir /r "$INSTDIR\lib"
    RMDir "$INSTDIR"
    
    # Eliminar variables de entorno
    ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR\bin"
    ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR\runtime"
    DeleteRegValue ${ENV_PATH_SYSTEM} "JASBOOT_HOME"
    
    # Eliminar alias si existen
    Delete "$INSTDIR\bin\jasboot.exe"
    Delete "$INSTDIR\bin\jbc.lnk"
    
    # Notificar al sistema sobre cambios en variables de entorno
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    System::Call "user32::SendMessageTimeout(i ${HWND_BROADCAST}, i ${WM_WININICHANGE}, i 0, t 'Environment', i 0, i 5000, i *R) .R1"
    
    # Eliminar claves de registro
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
    DeleteRegKey HKLM "Software\${PRODUCT_NAME}"
    
    # Mensaje de finalización
    MessageBox MB_OK "Jasboot SDK v${PRODUCT_VERSION} ha sido desinstalado completamente."
SectionEnd
