# JASB-SEC (validación mínima)

## Objetivo
Aplicar reglas de seguridad **mínimas** al IR antes de ejecutar o compilar a backend directo.

## Activación
JASB-SEC se aplica cuando:
- `IR_FLAG_SECURITY_STRICT` está activo en el header, **o**
- La metadata IA incluye policy `JASB-SEC` con `MODE = strict`.

## Reglas implementadas
1. **Kernel-only protegido**
   - Si una instrucción tiene `KERNEL_ONLY`, el header debe tener `IR_FLAG_KERNEL`.
2. **Stack depth**
   - Se valida profundidad de llamadas (`OP_LLAMAR`) y retornos (`OP_RETORNAR`).
   - Límite configurable: `MAX_STACK` (default 256).
3. **División inmediata por cero**
   - `OP_DIVIDIR` con `C_IMMEDIATE = 0` se rechaza.
4. **Memoria inmediata fuera de límites**
   - `OP_LEER` con dirección inmediata fuera de `data_size` se rechaza.
   - `OP_ESCRIBIR` con dirección inmediata fuera de `data_size` se rechaza.
5. **Saltos inmediatos válidos**
   - `OP_IR`, `OP_SI`, `OP_LLAMAR` con destinos inmediatos deben:
     - estar alineados a `IR_INSTRUCTION_SIZE` (5 bytes)
     - estar dentro de `code_size`
   - Límite opcional: `MAX_JUMP` (solo saltos relativos).

## Formato JASB-SEC en metadata IA

Tag `0x10` (8 bytes):
```
VERSION[1] | MODE[1] | MAX_STACK[2] | MAX_JUMP[4]
```

Ejemplo (strict, max_stack=256, max_jump=64):
```
01 02 00 01 40 00 00 00
```

## Notas
- Estas reglas son **mínimas** y no cubren análisis avanzado (CFG).
- El validador conserva compatibilidad con metadata legacy no estructurada.
