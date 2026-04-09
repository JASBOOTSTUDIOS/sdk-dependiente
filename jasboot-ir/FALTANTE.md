# ⚠️ Lo Que Falta al IR Binario y al Lenguaje (Hacia el Self-Hosting)

**Análisis de funcionalidades pendientes para alcanzar un lenguaje independiente y autocompilado.**

---

## 🔴 Crítico (Para Funcionalidad Básica & Self-Hosting)

### 1. **Integración Real con jasboot-lang**

- ❌ **Codegen IR no está conectado al parser real**
  - Actualmente es un ejemplo básico independiente.
  - Necesita generar IR desde archivos `.jasb` reales usando el AST de `jasboot-lang`.

### 2. **Control de Procesos (NUEVO v2.0)**

- ❌ **Ejecución de herramientas externas**
  - Implementar intrínseco `__sistema_ejecutar(comando)` para llamar a NASM y GCC.

### 3. **Estructuras de Datos Avanzadas (NUEVO v2.0)**

- ❌ **Mapas y Árboles para Compilador**
  - Soporte para **Tabla de Símbolos** (Mapas) y **AST** (Árboles/estructuras anidadas).
  - Extender `recordar` para permitir objetos anidados o estructuras tipo `struct`.

---

## 🟡 Importante (Para Producción & Generación de Código)

### 4. **Operaciones de Bajo Nivel (NUEVO v2.0)**

- ❌ **Manipulación Bitwise (Binaria)**
  - Añadir operadores `&`, `|`, `^`, `<<`, `>>` para construir opcodes binarios directamente.

### 5. **I/O Binario Puro (NUEVO v2.0)**

- ❌ **Escritura de archivos de objeto**
  - Funciones `__fs_abrir_binario`, `__fs_escribir_byte` y `__fs_leer_byte` para generar archivos `.o` y `.exe` sin pasar por texto.

### 6. **JASB-SEC (Sistema de Seguridad)**

- ❌ **Validación de seguridad avanzada**
  - Control Flow Graph (CFG) y análisis estático para prevenir saltos ilegales.

### 7. **Backend Directo (Compilación Nativa)**

- ⚠️ **Parcial (x86-64)**
  - Falta completar la emisión directa a código de máquina (sustituyendo a NASM a largo plazo).

---

## 🟢 Mejoras (Experiencia de Desarrollo)

### 8. **Sistema de Debugging & Profiling**

- ❌ **Herramientas de diagnóstico**
  - Implementar Step-by-step, inspección de registros y contadores de performance.

### 9. **Sistema de Módulos IR**

- ❌ **Enlace de archivos .jbo**
  - Soporte para importar y enlazar múltiples archivos binarios preservando la tabla de símbolos.

---

## 📋 Resumen de Metas (Roadmap v2.0)

1.  **Independencia del Entorno**: `__sistema_ejecutar` para automatizar builds.
2.  **Poder Expresivo**: Estructuras dinámicas (Mapas) para análisis sintáctico.
3.  **Bajo Nivel**: Operaciones bitwise para generación de binarios crudos.
4.  **Autocompilación**: Una versión de `jasboot.exe` escrita enteramente en `.jasb`.

---

## 🎯 Estado Actual vs. Requisitos Self-Hosting

| Característica       | Propósito        | Estado       |
| -------------------- | ---------------- | ------------ |
| Gestión Procesos     | Llamar Linker    | ❌ Pendiente |
| Mapas / Diccionarios | Tabla Símbolos   | ❌ Pendiente |
| Op. Bitwise          | Generar Binarios | ❌ Pendiente |
| I/O Binario          | Escribir .EXE    | ❌ Pendiente |
| Recursividad         | Parsing AST      | ✅ Funcional |

---

## 💡 Recomendaciones Finales

### Paso 1: Extensión de la VM

Añadir opcodes para operaciones binarias y llamadas al sistema sin modificar los existentes (extensibilidad).

### Paso 2: Prototipo de Lexer en Jasboot

Utilizar las capacidades actuales de texto para intentar tokenizar un archivo `.jasb` simple.

---

**Última actualización:** 2026-02-17
**Versión Objetivo:** 2.0 (Independencia Total)
