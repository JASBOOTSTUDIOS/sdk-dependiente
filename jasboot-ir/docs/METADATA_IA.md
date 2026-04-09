# Metadata IA (formato extendido)

## Objetivo
Estandarizar el payload de metadata IA para análisis, optimización y seguridad.

## Encabezado
```
IA_MAGIC = "IA01"
IA_VERSION = 0x01
RESERVED = 3 bytes (0x00)
```

## TLV
```
[TAG: uint8][LEN: uint16 LE][VALUE: LEN bytes]
```

Tags actuales:
- `0x01` Perfil IA (string UTF-8)
- `0x02` Build ID (string UTF-8)
- `0x10` Policy JASB-SEC (8 bytes)

## Ejemplo mínimo
Perfil + policy strict:
```
49 41 30 31 01 00 00 00    # "IA01" + version
01 06 00 6A 61 73 62 6F 6F  # tag=perfil, len=6, "jasboo"
10 08 00 01 02 00 01 40 00 00 00
```

## Compatibilidad
Si el payload **no** inicia con `IA01`, se considera legacy (no estructurado).
