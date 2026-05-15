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

## Vecinos de un salto (Fase 0 nativa)

- **`vecinos_jmn(origen, K, tipo)`** — alias del compilador para **`buscar_asociados_lista`** (mismo opcode `OP_MEM_BUSCAR_ASOCIADOS_LISTA`): devuelve una **lista** de hasta `K` ids de concepto destino en el **primer salto** desde `origen` con aristas que cumplen `tipo` (1..30; ver semántica en `memoria_neuronal.h`). Variantes: **`vecinos_jmn_mai`** (misma semántica que `buscar_asociados_lista_mai`), **`conexiones_salientes_de`** (alias de **`asociados_lista_de`**). Regresión: `jas-compiler-c/tests/test_vecinos_jmn.jasb`.

## Variables de entorno útiles (JMN)

- **`JASBOOT_JMN_ROOT`**: raíz del paquete `jasboot-jmn-core` (directorio que contiene `src/`) si la VM no está junto al layout estándar `sdk-dependiente/jasboot-jmn-core`.

## Pipeline L nativo (`tokenizar_L` / `claves_L`)

- Normalización y segmentación de texto a **lista** de ids (opcode **`OP_STR_DIVIDIR_TEXTO`** con **`IR_INST_FLAG_SAFE`**; registros **240** = `modo`, **241** = `min_len`, **242** = id texto CSV de stopwords extra). Incluye **Unicode** (NFC/NFD/NFKC/NFKD, BOM, strip marcas/CC, colapso WS Unicode; **utf8proc**), segmentación por espacio Unicode si `separador` vacío, n-gramas, stem lite, stopwords: **`TOKENIZAR_L_PIPELINE_NATIVE.md`**. Pruebas: **`test_tokenizar_L_estres_200.jasb`**, **`test_tokenizar_L_seg_ws_unicode_300.jasb`**, **`test_tokenizar_L_unicode_nfkc.jasb`**.

**Última revisión:** 2026-05-14 — pipeline L (regs 240–242, `test_tokenizar_L_estres_200.jasb`); además `JMN_RELACION_MAX`, `test_escalado_tipos_jmn.jasb`, `test_vecinos_jmn.jasb`.
