# 📋 Tabla de Opcodes - jasboot IR

**Referencia completa de opcodes del IR binario**

---

## 🎯 Formato de Instrucción

Cada instrucción ocupa **5 bytes fijos**:

```
[OPCODE] [FLAGS] [A] [B] [C]
```

---

## 📊 Tabla Completa

### Transferencia (0x01 - 0x0F)

| Opcode | Nombre | Semántica | A | B | C |
|--------|--------|-----------|---|---|---|
| 0x01 | mover | A ← B | Destino | Origen | - |
| 0x02 | leer | A ← [B] | Destino | Dirección | - |
| 0x03 | escribir | [A] ← B | Dirección | Valor | - |

### Aritmética (0x10 - 0x1F)

| Opcode | Nombre | Semántica | A | B | C |
|--------|--------|-----------|---|---|---|
| 0x10 | sumar | A ← B + C | Destino | Op1 | Op2 |
| 0x11 | restar | A ← B - C | Destino | Op1 | Op2 |
| 0x12 | multiplicar | A ← B * C | Destino | Op1 | Op2 |
| 0x13 | dividir | A ← B / C | Destino | Dividendo | Divisor |

### Lógica (0x20 - 0x2F)

| Opcode | Nombre | Semántica | A | B | C |
|--------|--------|-----------|---|---|---|
| 0x20 | y | A ← B & C | Destino | Op1 | Op2 |
| 0x21 | o | A ← B \| C | Destino | Op1 | Op2 |
| 0x22 | xor | A ← B ^ C | Destino | Op1 | Op2 |
| 0x23 | no | A ← ¬B | Destino | Operando | - |

### Comparación (0x30 - 0x3F)

| Opcode | Nombre | Semántica | A | B | C |
|--------|--------|-----------|---|---|---|
| 0x30 | comparar | A ← compare(B, C) | Resultado | Op1 | Op2 |
| 0x3C | cmp_le_flt | A ← ((float)B <= (float)C) ? 1 : 0 | Destino | Op1 | Op2 |
| 0x3D | cmp_ge_flt | A ← ((float)B >= (float)C) ? 1 : 0 | Destino | Op1 | Op2 |
| 0x3E | cmp_eq_flt | A ← ((float)B == (float)C) ? 1 : 0 (semántica C) | Destino | Op1 | Op2 |

**Valores de resultado**:
- `0` = falso (B == C)
- `1` = verdadero (B != C)
- `2` = menor (B < C)
- `3` = mayor (B > C)

### Control de Flujo (0x40 - 0x4F)

| Opcode | Nombre | Semántica | A | B | C |
|--------|--------|-----------|---|---|---|
| 0x40 | ir | PC ← A | Dirección | - | - |
| 0x41 | si | si A ≠ 0 → PC ← B | Condición | Dirección | - |
| 0x42 | llamar | push PC; PC ← A | Dirección | - | - |
| 0x43 | retornar | pop PC | - | - | - |

### Sistema / IA (0xF0 - 0xFF)

| Opcode | Nombre | Semántica | A | B | C |
|--------|--------|-----------|---|---|---|
| 0xF0 | marcar_estado | Marca estado para IA | Estado | - | - |
| 0xF1 | observar | Observa estado para IA | Registro | - | - |
| 0xFF | nop | No operation | - | - | - |

---

## 🔧 Flags de Instrucción

| Bit | Flag | Descripción |
|-----|------|-------------|
| 0 | A_IMMEDIATE | A es inmediato (no registro) |
| 1 | B_IMMEDIATE | B es inmediato (no registro) |
| 2 | C_IMMEDIATE | C es inmediato (no registro) |
| 3 | RELATIVE | Dirección relativa |
| 4 | SAFE | Operación segura |
| 5 | KERNEL_ONLY | Solo kernel |
| 6-7 | RESERVED | Reservado |

**Direccionamiento inmediato extendido (datos):**
- `OP_LEER`: si `B_IMMEDIATE` y `C_IMMEDIATE` están activos, la dirección es `B | (C << 8)` (16-bit LE).
- `OP_ESCRIBIR`: si `A_IMMEDIATE` y `C_IMMEDIATE` están activos, la dirección es `A | (C << 8)` (16-bit LE).

---

## 📝 Ejemplos

### Ejemplo 1: Mover valor

```
[0x01] [0x02] [r1] [10] [0x00]
       ^      ^    ^
       |      |    |
       |      |    +-- B es inmediato (10)
       |      +------- A es registro r1
       +------------- Flags: B_IMMEDIATE
```

**Semántica**: `r1 ← 10`

---

### Ejemplo 2: Sumar

```
[0x10] [0x04] [r2] [r1] [5]
       ^      ^    ^    ^
       |      |    |    |
       |      |    |    +-- C es inmediato (5)
       |      |    +------- B es registro r1
       |      +------------ A es registro r2
       +------------------- Flags: C_IMMEDIATE
```

**Semántica**: `r2 ← r1 + 5`

---

### Ejemplo 3: Comparar y saltar

```
[0x30] [0x00] [r3] [r1] [r2]  # comparar r3, r1, r2
[0x41] [0x00] [r3] [dest] [0] # si r3 != 0 → PC ← dest
```

**Semántica**: Si `r1 != r2`, saltar a `dest`

---

**Última actualización**: 2026-01-19
