# 🔍 Aclaración: ¿Qué es jasboot-ir?

**Explicación clara de qué hace este proyecto y cómo se relaciona con el compilador**

---

## ❓ La Confusión

**Pensabas que:**
- `jasboot-ir` es el compilador que compila código jasboot
- Este proyecto compila el lenguaje jasboot core

**La realidad:**
- `jasboot-ir` **NO es el compilador**
- `jasboot-ir` es solo la **implementación del formato IR binario**
- El compilador real está en `jasboot-lang`

---

## 📊 Proyectos en el Repositorio

### 1. **jasboot-lang** (El Compilador Real) ✅

**Ubicación:** `jasboot-lang/`

**Qué hace:**
- ✅ Compila código `.jasb` (texto) a ejecutables
- ✅ Tiene Lexer, Parser, AST, Codegen
- ✅ Genera assembly (`.asm`) actualmente
- ✅ Usa nasm + gcc para crear ejecutables

**Flujo actual:**
```
archivo.jasb → Lexer → Parser → AST → Codegen → salida.asm → nasm → salida.o → gcc → ejecutable
```

**Ejemplo de uso:**
```bash
cd jasboot-lang
./jasboot programa.jasb    # Compila a salida.asm
nasm -f elf64 salida.asm   # Ensambla
gcc salida.o -o programa   # Enlaza
./programa                 # Ejecuta
```

---

### 2. **jasboot-ir** (Formato IR Binario) ⚠️

**Ubicación:** `jasboot-ir/`

**Qué hace:**
- ✅ Implementa el **formato IR binario** (`.jbo`)
- ✅ Tiene herramientas para leer/escribir IR
- ✅ Tiene una VM básica para ejecutar IR
- ❌ **NO compila código jasboot** (solo tiene un ejemplo básico)

**Qué es el IR binario:**
- Es un **formato intermedio** (como el assembly, pero binario)
- Reemplaza al assembly como formato intermedio
- Es un formato propio de jasboot

**Flujo futuro (cuando esté integrado):**
```
archivo.jasb → Lexer → Parser → AST → Codegen IR → archivo.jbo → VM/Backend → ejecución
```

**Ejemplo de uso actual:**
```bash
cd jasboot-ir
./bin/jasboot-ir-compiler ejemplo.jasb -o ejemplo.jbo  # Solo ejemplo básico
./bin/jasboot-ir-vm ejemplo.jbo                         # Ejecuta en VM
```

---

## 🎯 Diferencia Clave

| Aspecto | jasboot-lang | jasboot-ir |
|---------|--------------|------------|
| **¿Qué es?** | Compilador completo | Formato IR binario |
| **¿Compila jasboot?** | ✅ SÍ | ❌ NO (solo ejemplo) |
| **¿Genera ejecutables?** | ✅ SÍ (vía nasm/gcc) | ⚠️ Solo VM (no ejecutables reales) |
| **¿Tiene Lexer/Parser?** | ✅ SÍ | ❌ NO |
| **¿Tiene AST?** | ✅ SÍ | ❌ NO (solo ejemplo) |
| **¿Qué genera?** | Assembly (`.asm`) | IR binario (`.jbo`) |
| **Estado** | ✅ Funcional | ⚠️ Formato implementado, falta integración |

---

## 🔄 Relación Entre Proyectos

### Estado Actual

```
jasboot-lang (Compilador)
    ↓
    Genera: .asm (assembly)
    ↓
    nasm + gcc
    ↓
    Ejecutable
```

### Visión Futura (con IR binario)

```
jasboot-lang (Compilador)
    ↓
    [Integrar codegen_ir de jasboot-ir]
    ↓
    Genera: .jbo (IR binario)
    ↓
    jasboot-ir (VM o Backend)
    ↓
    Ejecución
```

---

## 💡 ¿Por Qué Existe jasboot-ir?

**Razón:** Independencia de herramientas externas

**Problema actual:**
- `jasboot-lang` depende de `nasm` y `gcc`
- No es completamente independiente
- El assembly es difícil de analizar para IA

**Solución (IR binario):**
- Formato propio, no depende de nasm/gcc
- Fácil de analizar para IA
- Puede ejecutarse directamente (VM)
- O compilarse a código nativo (backend futuro)

---

## 🎯 Resumen

### jasboot-lang
- ✅ **ES el compilador** que compila código jasboot
- ✅ Funciona actualmente
- ✅ Genera assembly

### jasboot-ir
- ⚠️ **NO es el compilador**
- ✅ Es la **implementación del formato IR binario**
- ⚠️ Falta integrarlo con jasboot-lang
- ✅ Tiene formato, validación, VM básica

---

## 🚀 Para Compilar Código jasboot

**Usa jasboot-lang:**
```bash
cd jasboot-lang
./jasboot tu_programa.jasb
```

**NO uses jasboot-ir** (aún no compila código real, solo tiene ejemplos)

---

## 📝 Analogía Simple

Piensa en esto como construir una casa:

- **jasboot-lang** = El arquitecto y constructor (compila el código)
- **jasboot-ir** = Un nuevo tipo de ladrillo (formato IR binario)
- **Assembly** = El tipo de ladrillo actual (que se usa ahora)

El IR binario es solo un "tipo de ladrillo diferente", pero el constructor (compilador) sigue siendo jasboot-lang.

---

**Última actualización:** 2026-01-19
