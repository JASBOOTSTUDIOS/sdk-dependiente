<p align="center">
  <img src="img/jasboot-icon.png" alt="Jasboot SDK" width="120" height="120">
</p>

# Jasboot SDK - Guía de Instalación

¡Bienvenido a Jasboot! Este paquete contiene todo lo necesario para compilar y ejecutar aplicaciones Jasboot de forma global en tu sistema Windows.

## Contenido del Paquete

- **jbc.exe**: Compilador y CLI de Jasboot.
- **jasboot-ir-vm.exe**: Máquina Virtual (VM) para ejecutar el bytecode generado.
- **instalar_jasboot.bat**: Script de instalación automatizada.
- **JasbootSetup.exe**: Instalador profesional con interfaz gráfica.

## Instrucciones de Instalación

### Opción 1: Instalador Visual (Recomendado)
1. Ejecuta el archivo `JasbootSetup.exe`.
2. Sigue las instrucciones del asistente.
3. Asegúrate de marcar la opción para **Añadir al PATH** si deseas usar Jasboot desde cualquier terminal.

### Opción 2: Script de Consola (Rápido)
1. Haz clic derecho sobre `instalar_jasboot.bat` y selecciona **Ejecutar como administrador**.
2. Sigue las instrucciones en la consola.
3. El script copiará los archivos a `C:\Program Files\Jasboot` y configurará las variables de entorno automáticamente.

## Verificación de la Instalación

Una vez instalado, abre una nueva terminal (PowerShell o CMD) y escribe:

```bash
jbc --help
```

Si ves la ayuda del compilador, ¡Jasboot está listo para usarse!

## Uso Básico

Para compilar y ejecutar un archivo `.jasb`:

```bash
jbc mi_programa.jasb -e
```

---
*Jasboot SDK - Potenciado por el motor OSIA*
