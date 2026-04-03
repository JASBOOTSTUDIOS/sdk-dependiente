# Backend Directo IR (x86-64 ELF)

## Objetivo
Compilar un `.jbo` a un binario ELF **sin VM**, generando ensamblador NASM y enlazando a código nativo.

## Target actual
- **Arquitectura**: x86-64
- **Formato de salida**: ELF (Linux)
- **Toolchain**: `nasm` + `ld`

## Layout de ejecución
- **Registros virtuales**: array `regs[256]` en `.bss` (64-bit)
- **Memoria de datos**: sección `.data` con el payload de datos del IR
- **Stack de llamadas**: `stack[256]` + `sp` en `.bss`

## Convenciones
- `OP_RETORNAR` usa `operand_a` (registro o inmediato) como **exit code**
- `OP_IMPRIMIR_TEXTO` imprime strings null-terminated en `.data`
- `OP_OBSERVAR` imprime `IA observa: <valor>`
- `OP_MEM_*` y `OP_MARCAR_ESTADO` son **no-op** en este backend básico

## Compatibilidad
El backend respeta las reglas del IR actual y su VM básica, incluyendo:
- Inmediatos de 8-bit (o 16-bit en LEER/ESCRIBIR con flag C)
- Saltos relativos/absolutos interpretados como offsets de byte

## Subset Tier-1 para AOT/JIT
Subconjunto recomendado para la primera ola de compilación nativa, alineado con la instrumentación de hotspots de la VM:

- `OP_MOVER`
- `OP_LEER`
- `OP_ESCRIBIR`
- `OP_SUMAR`
- `OP_RESTAR`
- `OP_MULTIPLICAR`
- `OP_DIVIDIR`
- `OP_Y`
- `OP_O`
- `OP_XOR`
- `OP_NO`
- `OP_COMPARAR`
- `OP_IR`
- `OP_SI`
- `OP_LLAMAR`
- `OP_RETORNAR`
- `OP_OBSERVAR`
- `OP_IMPRIMIR_TEXTO`
- `OP_NOP`

Regla práctica:

- Si un benchmark caliente cae mayormente en este subset, es candidato natural para AOT/JIT.
- Si el programa usa `fs_*`, FFI, memoria neuronal o efectos laterales complejos, debe mantenerse fallback al intérprete hasta modelar esa semántica con exactitud.

## Uso
```
bin/jasboot-ir-backend input.jbo -o output.elf
```

Para preservar el ASM:
```
bin/jasboot-ir-backend input.jbo -o output.elf --asm output.asm
```
