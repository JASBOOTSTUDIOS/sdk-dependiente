<p align="center">
  <img src="img/jasboot-icon.png" alt="Jasboot SDK" width="120" height="120">
</p>

# Jasboot SDK v0.0.1 - Guía de Instalación

¡Bienvenido a Jasboot SDK v0.0.1! Este paquete contiene todo lo necesario para compilar y ejecutar aplicaciones Jasboot de forma global en tu sistema Windows.

## Contenido del Paquete

### Componentes Principales
- **jbc.exe**: Compilador principal de Jasboot.
- **jbc-next.exe**: Compilador de siguiente generación con mejoras.
- **jbc-cursor.exe**: Compilador optimizado para desarrollo interactivo.
- **jasboot-ir-vm.exe**: Máquina Virtual para ejecutar bytecode Jasboot IR.
- **jasboot-ir-vm-trace.exe**: VM con modo depuración/traza.

### Herramientas Adicionales
- **VSCode Extension**: Extensión para Visual Studio Code (jasboot-0.0.7.vsix).
- **Ejemplos y Plantillas**: Proyectos de ejemplo incluyendo Neurixis IA.
- **Documentación**: Guías y referencias completas.
- **instalar_jasboot.bat**: Script de instalación automatizada.
- **JasbootSetup-v0.0.1.exe**: Instalador profesional con interfaz gráfica.

## Instrucciones de Instalación

### Opción 1: Instalador Visual (Recomendado)
1. Ejecuta el archivo `JasbootSetup-v0.0.1.exe`.
2. Sigue las instrucciones del asistente de instalación.
3. Selecciona los componentes que deseas instalar:
   - Compilador Jasboot (Requerido)
   - Runtime Jasboot IR
   - Extensión VSCode
   - Ejemplos y Plantillas
   - Configuración del PATH del Sistema
4. Asegúrate de marcar la opción para **Añadir al PATH** si deseas usar Jasboot desde cualquier terminal.

### Opción 2: Script de Consola (Rápido)
1. Haz clic derecho sobre `instalar_jasboot.bat` y selecciona **Ejecutar como administrador**.
2. Sigue las instrucciones en la consola.
3. El script copiará los archivos a `C:\Program Files\Jasboot` y configurará las variables de entorno automáticamente.

## Verificación de la Instalación

### Método 1: Verificación Automática (Recomendado)
Ejecuta el script de verificación incluido:
```bash
verify_installation.bat
```
Este script verificará automáticamente:
- Archivos de instalación
- Configuración de variables de entorno (PATH y JASBOOT_HOME)
- Accesibilidad global de los comandos
- Compilación y ejecución de prueba

### Método 2: Verificación Manual
Abre una **NUEVA** terminal (PowerShell o CMD) y ejecuta:

```bash
# Verificar versión del compilador
jbc --version

# Verificar ayuda del compilador
jbc --help

# Verificar alias del compilador
jasboot --version

# Verificar que la VM funciona
jasboot-ir-vm --version

# Verificar variable de entorno
echo %JASBOOT_HOME%
```

### Método 3: Verificación de PATH
```bash
# Verificar que Jasboot está en el PATH
where jbc
where jasboot
where jasboot-ir-vm

# Verificar contenido del PATH
echo %PATH% | findstr Jasboot
```

**Si todos los comandos anteriores funcionan correctamente, ¡Jasboot SDK v0.0.1 está listo para usarse globalmente!**

### Solución de Problemas Comunes

#### Si `jbc` no es reconocido:
1. **Reinicia la terminal** - Las variables de entorno se actualizan en nuevas sesiones
2. **Ejecuta como administrador** - Reinicia el terminal con privilegios de administrador
3. **Verifica la instalación** - Ejecuta `verify_installation.bat`
4. **Reinstala si es necesario** - Ejecuta el instalador nuevamente como administrador

#### Si los comandos funcionan pero lentamente:
- Esto es normal la primera vez, el sistema está configurando el entorno
- Las ejecuciones posteriores serán más rápidas

## Uso Básico

### Compilar y Ejecutar
```bash
# Compilar y ejecutar en un paso
jbc mi_programa.jasb -e

# Solo compilar
jbc mi_programa.jasb -o mi_programa.jir

# Ejecutar bytecode compilado
jasboot-ir-vm mi_programa.jir
```

### Modo Depuración
```bash
# Compilar con información de depuración
jbc mi_programa.jasb -d -o mi_programa_debug.jir

# Ejecutar con modo traza
jasboot-ir-vm-trace mi_programa_debug.jir
```

### Proyectos de Ejemplo
Los ejemplos se instalan en `C:\Program Files\Jasboot\examples\`:

```bash
# Ejecutar ejemplo de Neurixis IA
jbc "%JASBOOT_HOME%\examples\neurixis_IA\inicio.jasb" -e

# Listar todos los ejemplos disponibles
dir "%JASBOOT_HOME%\examples\"
```

## Configuración de VSCode

1. Abre VSCode
2. Ve a Extensiones (Ctrl+Shift+X)
3. Haz clic en el ícono de extensiones y selecciona "Instalar desde VSIX..."
4. Selecciona el archivo `jasboot-0.0.7.vsix` en `%JASBOOT_HOME%\vscode\`
5. Reinicia VSCode

### Extensiones de Archivo Soportadas
- `.jasb` - Archivos fuente de Jasboot
- `.jir` - Bytecode Jasboot IR
- `.jmn` - Archivos de memoria neuronal

## Variables de Entorno

El instalador configura las siguientes variables de entorno:

- **PATH**: Añade `%JASBOOT_HOME%\bin` y `%JASBOOT_HOME%\runtime`
- **JASBOOT_HOME**: Directorio de instalación de Jasboot SDK

## Estructura de Directorios

```
C:\Program Files\Jasboot\
    bin\                    # Compiladores y herramientas
        jbc.exe
        jbc-next.exe
        jbc-cursor.exe
    runtime\                # Máquinas virtuales
        jasboot-ir-vm.exe
        jasboot-ir-vm-trace.exe
    docs\                   # Documentación
    examples\               # Proyectos de ejemplo
        neurixis_IA\
        otros_proyectos\
    vscode\                 # Extensión VSCode (jasboot-0.0.7.vsix)
    img\                    # Recursos visuales
```

## Soporte y Documentación

- **Documentación completa**: `%JASBOOT_HOME%\docs\`
- **Ejemplos**: `%JASBOOT_HOME%\examples\`
- **Repositorio**: https://github.com/JASBOOTSTUDIOS/jasboot
- **Issues**: https://github.com/JASBOOTSTUDIOS/jasboot/issues

---
*Jasboot SDK v0.0.1 - Potenciado por el motor OSIA*  
*© 2024 JASBOOTSTUDIOS*
