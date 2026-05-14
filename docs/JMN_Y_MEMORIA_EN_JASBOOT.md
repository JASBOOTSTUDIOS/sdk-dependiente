# JMN y memoria en Jasboot (SDK)

Documento de **operación verificada** respecto al código bajo `sdk-dependiente/`. Para la arquitectura amplia del monorepo, ver `AGENTS.md` en la raíz del repositorio Jasboot.

## Bytecode y toolchain

- El compilador **`jbc`** (`jas-compiler-c`) emite **`.jbo`** por defecto (`main.c`, opción `-o`).
- La VM **`jasboot-ir-vm`** carga y ejecuta **`.jbo`** (`jasboot-ir`).

## Tipos de relación JMN (`asociar_relacion`)

- En `jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal.h`, **`JMN_RELACION_MAX` es 30** (tipos enteros válidos en el cuarto argumento de `asociar_relacion` acotados por esa constante en el núcleo).
- Regresión manual: `node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_escalado_tipos_jmn.jasb` (exit 0; usa tipos 1, 2, 3, 8 y 9).

## `recordar`, `buscar` y `resultado`

- Tras **`recordar "clave" con valor …`**, el valor queda en JMN; la variable global **`resultado`** (tipo `elemento`) se rellena típicamente con **`buscar "clave"`** u otras primitivas que devuelven por `resultado`. No asumas que `recordar` deja el valor listo en `resultado` sin una lectura explícita (comportamiento descrito en `AGENTS.md`).

## Varias aristas del mismo tipo desde una clave (p. ej. varias tipo 1)

- Cuando hay **varios destinos** con texto asociado y la VM debe elegir uno para lecturas tipo “memoria / valor asociado”, en **`OP_MEM_OBTENER_VALOR`** (`jasboot-ir/src/vm.c`) se prefiere la **mayor fuerza**; si hay **empate**, se toma el **último** candidato en el orden devuelto por `jmn_buscar_asociaciones` (comentario en fuente: suele corresponder al más reciente).

## Variables de entorno útiles (JMN)

- **`JASBOOT_JMN_ROOT`**: raíz del paquete `jasboot-jmn-core` (directorio que contiene `src/`) si la VM no está junto al layout estándar `sdk-dependiente/jasboot-jmn-core`.

**Última revisión:** alineado con fuentes del SDK en 2026-05-14 (comprobación de `JMN_RELACION_MAX`, ejecución de `test_escalado_tipos_jmn.jasb`, revisión de `vm.c` y `main.c` del compilador).
