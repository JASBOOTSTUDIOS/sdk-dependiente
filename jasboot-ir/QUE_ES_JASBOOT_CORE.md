# 🔍 ¿Qué es el "Código jasboot Core"?

**Aclaración sobre qué es el código jasboot y cómo se relaciona con el IR binario**

---

## 📝 El Código jasboot Core

**El "código jasboot core" es el LENGUAJE FUENTE que escribes.**

### Ejemplo de Código jasboot Core

```jasb
principal
    recordar "contador" con valor 1
    recordar "suma" con valor 0
    
    mientras "contador" es menor que 6
        buscar "suma"
        buscar "contador"
        pensar resultado + resultado
        aprender "suma" con valor resultado
        
        buscar "contador"
        pensar resultado + 1
        aprender "contador" con valor resultado
    
    buscar "suma"
    responder resultado
    retornar 0
```

**Esto es código jasboot core** - el lenguaje que escribes en archivos `.jasb`.

---

## 🔄 Flujo Completo de Compilación

### Paso a Paso

```
1. CÓDIGO JASBOOT CORE (lo que escribes)
   ↓
   archivo.jasb (texto)
   "recordar 'contador' con valor 1"
   
2. COMPILADOR (jasboot-lang)
   ↓
   Lexer → Parser → AST
   
3. CODEGEN (generación de código)
   ↓
   Opción A: Genera ASSEMBLY (.asm) ← Estado actual
   Opción B: Genera IR BINARIO (.jbo) ← Futuro
   
4. EJECUCIÓN
   ↓
   Assembly: nasm + gcc → ejecutable
   IR Binario: VM o Backend → ejecución
```

---

## 🎯 ¿Qué Hace Cada Proyecto?

### jasboot-lang (Compilador)
- ✅ **Lee código jasboot core** (archivos `.jasb`)
- ✅ **Compila** código jasboot a assembly o IR binario
- ✅ **Tiene** Lexer, Parser, AST

### jasboot-ir (Formato IR Binario)
- ❌ **NO lee** código jasboot core directamente
- ✅ **Implementa** el formato IR binario (`.jbo`)
- ✅ **Puede ejecutar** IR binario en VM
- ⚠️ **Falta integrar** con jasboot-lang para compilar código real

---

## 💡 Analogía Completa

Piensa en escribir un libro:

1. **Código jasboot core** = El libro que escribes (en español)
   - `recordar "contador" con valor 1`
   - `cuando "edad" es mayor que "dieciocho"`

2. **Compilador (jasboot-lang)** = El traductor
   - Lee tu libro (código jasboot)
   - Lo traduce a otro formato

3. **Assembly** = Traducción a inglés (formato actual)
   - `mov rax, 1`
   - `cmp rax, 18`

4. **IR Binario** = Traducción a código binario estructurado (formato futuro)
   - `[0x01][0x02][r1][1][0x00]` (5 bytes)
   - Formato propio, fácil de analizar

---

## ❓ ¿El IR Binario es Código jasboot Core?

**NO.** Son cosas diferentes:

| Aspecto | Código jasboot Core | IR Binario |
|---------|---------------------|------------|
| **¿Qué es?** | Lenguaje fuente | Formato intermedio |
| **¿Dónde está?** | Archivos `.jasb` (texto) | Archivos `.jbo` (binario) |
| **¿Quién lo escribe?** | Tú (programador) | El compilador (automático) |
| **¿Es legible?** | ✅ Sí (texto) | ❌ No (binario) |
| **Ejemplo** | `recordar "x" con valor 10` | `[0x01][0x02][r1][10][0x00]` |

---

## 🔄 Relación Real

```
TÚ escribes:
┌─────────────────────────┐
│ Código jasboot Core     │ ← Esto es lo que escribes
│ (archivo.jasb)          │
│                         │
│ recordar "x" con valor 10│
│ pensar "x" + 5          │
│ responder resultado      │
└─────────────────────────┘
         ↓
    COMPILADOR
    (jasboot-lang)
         ↓
┌─────────────────────────┐
│ IR Binario              │ ← Esto es lo que genera el compilador
│ (archivo.jbo)           │
│                         │
│ [0x01][0x02][r1][10]... │ ← Binario, no legible
└─────────────────────────┘
         ↓
    VM o Backend
         ↓
    EJECUCIÓN
```

---

## ✅ Resumen

1. **Código jasboot core** = Lo que escribes (archivos `.jasb` con sintaxis jasboot)
2. **IR binario** = Formato intermedio que el compilador genera (archivos `.jbo`)
3. **jasboot-lang** = El compilador que convierte código jasboot → assembly o IR
4. **jasboot-ir** = La implementación del formato IR binario

**El IR binario NO es código jasboot core. Es un formato intermedio que el compilador genera desde el código jasboot core.**

---

**Última actualización:** 2026-01-19
