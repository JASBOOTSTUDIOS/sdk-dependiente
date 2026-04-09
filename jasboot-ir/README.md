<p align="center">
  <img src="../assets/jasboot-icon.png" alt="Jasboot — logo del lenguaje" width="120" height="120">
</p>

# 🧠 jasboot IR Binario

**IR (Intermediate Representation) binario propio para jasboot**

---

## 🎯 Objetivo

Crear un formato binario IR propio que:
- ✅ Reemplaza assembly como formato intermedio
- ✅ Instrucciones de tamaño fijo (5 bytes)
- ✅ 256 registros virtuales
- ✅ Validación de seguridad IA (JASB-SEC)
- ✅ Metadata IA opcional
- ✅ Formato `.jbo` (jasboot object)

---

## 🏗️ Arquitectura

```
.jasb (texto) → Lexer → Parser → AST → Codegen IR → .jbo (binario) → VM/Backend → ejecución
```

### Componentes

1. **Formato Binario** (`src/ir_format.c`): Estructura del IR binario
2. **Codegen IR** (`src/codegen_ir.c`): Genera IR desde AST
3. **Reader IR** (`src/reader_ir.c`): Lee y valida IR binario
4. **VM/Backend** (`src/vm.c`): Ejecuta IR (futuro)

---

## 📚 Documentación

- **[Especificación del Formato](docs/FORMATO_IR.md)**: Estructura completa del IR binario
- **[Opcodes](docs/OPCODES.md)**: Tabla de opcodes y semántica
- **[Guía de Implementación](docs/GUIA_IMPLEMENTACION.md)**: Cómo implementar cada componente

---

## 🚀 Uso

### Compilar

```bash
cd jasboot-ir
make
```

Esto generará los siguientes binarios en `bin/`:
- `ir_test`: Test básico del formato IR
- `jasboot-ir-compiler`: Compilador (genera IR desde código fuente)
- `jasboot-ir-validator`: Validador de archivos IR
- `jasboot-ir-vm`: Máquina virtual para ejecutar IR
- `jasboot-ir-opt`: Optimizador de IR binario

### Generar IR

```bash
# Generar IR desde código fuente (ejemplo básico)
./bin/jasboot-ir-compiler archivo.jasb -o archivo.jbo --opt
```

### Validar IR

```bash
./bin/jasboot-ir-validator archivo.jbo
```

### Ejecutar IR en VM

```bash
./bin/jasboot-ir-vm archivo.jbo
```

### Optimizar IR

```bash
./bin/jasboot-ir-opt archivo.jbo -o archivo_opt.jbo --stats
```

### Ejecutar Tests

```bash
# Test básico
make test

# Tests completos
./bin/ir_test
```

---

## 📊 Estado Actual

| Componente | Estado |
|------------|--------|
| **Formato IR** | ✅ Implementado |
| **Metadata IA** | ✅ Implementado |
| **Codegen IR** | ✅ Implementado (básico) |
| **Reader IR** | ✅ Implementado |
| **Validador IR** | ✅ Implementado |
| **VM** | ✅ Implementado (básico) |
| **Herramientas CLI** | ✅ Implementado |
| **JASB-SEC** | ✅ Implementado (mínimo) |

---

## 🔗 Relación con otros repos (GitHub)

- **Compilador:** repo sugerido **jasboot-compiler** (en monorepo Jasboot: `sdk-dependiente/jas-compiler-c`) — genera `.jbo` con **`jbc`**.
- **JMN:** repo **jasboot-jmn-core** — fuentes C enlazadas al compilar la VM.

### Clonar solo este repo

1. Clona también **`jasboot-jmn-core`** (hermano o submódulo).
2. Define **`JASBOOT_JMN_ROOT`** con la ruta absoluta al directorio raíz de `jasboot-jmn-core` (el que contiene `src/`).
3. Ejecuta **`build_vm.bat`** (Windows) o **`make`** con `JMN_PKG` apuntando a ese clon.

En el monorepo, **`jasboot-jmn-core`** vive en **`sdk-dependiente/jasboot-jmn-core`** (hermano de este directorio); el build lo detecta. En otro layout usa `JASBOOT_JMN_ROOT` / `JMN_PKG`.

---

**Última actualización**: 2026-03-27
