# 📦 Formato IR Binario (jasboot)

**Especificación completa del formato binario IR para jasboot**

---

## 🎯 Visión General

El IR binario es un formato intermedio optimizado para:
- ✅ Ejecución eficiente (VM o backend directo)
- ✅ Análisis IA (formato estructurado)
- ✅ Validación de seguridad (JASB-SEC)
- ✅ Portabilidad (IR genérico, no específico de arquitectura)

**Extensión**: `.jbo` (jasboot object)

---

## 1️⃣ Header Fijo (16 bytes)

**Ubicación**: Offset 0 del archivo

### Estructura

| Offset | Tamaño | Campo | Descripción |
|--------|--------|-------|-------------|
| 0x00 | 4 | MAGIC | `0x4A 0x41 0x53 0x42` ("JASB") |
| 0x04 | 1 | VERSION | `0x01` (versión del formato) |
| 0x05 | 1 | ENDIAN | `0x00` (little-endian) |
| 0x06 | 1 | TARGET | `0x00` (IR genérico) |
| 0x07 | 1 | FLAGS | Bits de flags |
| 0x08 | 4 | CODE_SIZE | Tamaño del código (uint32) |
| 0x0C | 4 | DATA_SIZE | Tamaño de datos (uint32) |

### FLAGS (byte 0x07)

| Bit | Significado |
|-----|-------------|
| 0 | DEBUG: Información de debug presente |
| 1 | KERNEL: Código de kernel |
| 2 | IA_METADATA: Metadata IA presente |
| 3 | SECURITY_STRICT: Seguridad estricta |
| 4-7 | Reservados |

---

## 2️⃣ Metadata IA (Opcional)

**Presente solo si FLAGS.bit2 = 1**

### Estructura

```
[IA_SIZE: uint32] (4 bytes)
[IA_PAYLOAD: IA_SIZE bytes]
```

### Formato IA (v1, opcional)

Si el payload comienza con el magic `IA01`, se interpreta como formato estructurado:

```
IA_MAGIC[4] = "IA01"
IA_VERSION[1] = 0x01
RESERVED[3] = 0x00
TLV...
```

### TLV (Type-Length-Value)

```
[TAG: uint8][LEN: uint16 LE][VALUE: LEN bytes]
```

Tags definidos:
- `0x01` Perfil IA (string UTF-8)
- `0x02` Build ID (string UTF-8)
- `0x10` JASB-SEC policy (8 bytes)

**JASB-SEC policy (tag 0x10)**
```
VERSION[1] | MODE[1] | MAX_STACK[2] | MAX_JUMP[4]
```
Donde:
- `MODE`: `0=off`, `1=warn`, `2=strict`
- `MAX_STACK`: límite de profundidad de llamadas (0 = 256)
- `MAX_JUMP`: límite de salto relativo en bytes (0 = sin límite)

**Propósito**: Información para análisis, optimización y seguridad IA.

**No afecta ejecución**: Solo metadata para herramientas.

---

## 3️⃣ Formato de Instrucción IR

### Tamaño Fijo

**Cada instrucción ocupa exactamente 5 bytes**.

```
BYTE 0: OPCODE (1 byte)
BYTE 1: FLAGS (1 byte)
BYTE 2: OPERANDO A (1 byte)
BYTE 3: OPERANDO B (1 byte)
BYTE 4: OPERANDO C (1 byte)
```

**PC avanza siempre +5 bytes** salvo saltos.

---

### Flags de Instrucción (BYTE 1)

| Bit | Significado |
|-----|-------------|
| 0 | A es inmediato |
| 1 | B es inmediato |
| 2 | C es inmediato |
| 3 | Dirección relativa |
| 4 | Operación segura |
| 5 | Kernel-only |
| 6-7 | Reservado |

---

### Registros Virtuales

- **256 registros virtuales**: `r0` - `r255`
- El backend los mapea a hardware real
- Registros especiales:
  - `r0`: Siempre cero
  - `r1-r15`: Registros de propósito general
  - `r16-r255`: Registros extendidos

---

## 4️⃣ Tabla de Opcodes (v1)

### Transferencia

| Opcode | Acción | Semántica |
|--------|--------|-----------|
| 0x01 | mover | A ← B |
| 0x02 | leer | A ← [B] |
| 0x03 | escribir | [A] ← B |

### Aritmética

| Opcode | Acción | Semántica |
|--------|--------|-----------|
| 0x10 | sumar | A ← B + C |
| 0x11 | restar | A ← B - C |
| 0x12 | multiplicar | A ← B * C |
| 0x13 | dividir | A ← B / C |

### Lógica

| Opcode | Acción | Semántica |
|--------|--------|-----------|
| 0x20 | y | A ← B & C |
| 0x21 | o | A ← B \| C |
| 0x22 | xor | A ← B ^ C |
| 0x23 | no | A ← ¬B |

### Comparación

| Opcode | Acción | Semántica |
|--------|--------|-----------|
| 0x30 | comparar | A ← compare(B, C) |

**Resultado en A**:
- `0` = falso
- `1` = verdadero
- `2` = menor
- `3` = mayor

### Control de Flujo

| Opcode | Acción | Semántica |
|--------|--------|-----------|
| 0x40 | ir | PC ← A |
| 0x41 | si | si A ≠ 0 → PC ← B |
| 0x42 | llamar | push PC; PC ← A |
| 0x43 | retornar | pop PC |

### Sistema / IA

| Opcode | Acción | Semántica |
|--------|--------|-----------|
| 0xF0 | marcar_estado | Marca estado para IA |
| 0xF1 | observar | Observa estado para IA |
| 0xFF | nop | No operation |

---

## 5️⃣ Layout del Archivo

```
┌─────────────────────────────────────┐
│ Header (16 bytes)                  │
│ - Magic, Version, Flags            │
│ - CODE_SIZE, DATA_SIZE             │
├─────────────────────────────────────┤
│ Metadata IA (opcional)              │
│ - IA_SIZE (4 bytes)                 │
│ - IA_PAYLOAD (IA_SIZE bytes)        │
├─────────────────────────────────────┤
│ Código (CODE_SIZE bytes)            │
│ - Instrucciones IR (5 bytes c/u)    │
├─────────────────────────────────────┤
│ Datos (DATA_SIZE bytes)             │
│ - Strings, constantes, etc.          │
└─────────────────────────────────────┘
```

**Semántica de datos (VM):**
- La sección de datos se copia en memoria de la VM a partir de la dirección `0`.
- `OP_LEER` / `OP_ESCRIBIR` usan direcciones byte-offset dentro de esa región cuando el operando es inmediato.
- Direccionamiento inmediato extendido: si `B_IMMEDIATE` (o `A_IMMEDIATE` en `OP_ESCRIBIR`) y `C_IMMEDIATE` están activos, la dirección se interpreta como 16-bit little-endian (`low=B/A`, `high=C`).

---

## 6️⃣ Reglas de Ejecución

1. **No existen flags implícitos**: Todo estado es visible
2. **Saltos fuera de código**: Error
3. **Opcode inválido**: Trap IA
4. **Retornar sin llamar**: Inválido
5. **División por cero**: Detectable estáticamente

---

## 7️⃣ Ejemplo Mínimo

### Código jasboot

```jasb
principal
    recordar "valor" con valor 10
    pensar "valor" + 5
    responder resultado
    retornar 0
```

### IR Binario (simplificado)

```
[JASB] [0x01] [0x00] [0x00] [0x00] [0x00]  # Header
[0x01] [0x00] [r1] [0x0A] [0x00]           # mover r1, 10
[0x10] [0x00] [r2] [r1] [0x05]             # sumar r2, r1, 5
[0xF1] [0x00] [r2] [0x00] [0x00]           # observar r2
[0x43] [0x00] [0x00] [0x00] [0x00]         # retornar
```

---

**Última actualización**: 2026-01-19
