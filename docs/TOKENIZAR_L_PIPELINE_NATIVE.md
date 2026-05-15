# Pipeline L nativo: `tokenizar_L` y `claves_L` (SDK)

Comportamiento y límites alineados con **`jasboot-ir/src/vm_tokenizar_l_pipeline.inc`** y **`vm_unicode_norm.c`** (utf8proc, `third_party/utf8proc/`). Complementa `flujo_model_IA/01_tokenizacion_L.md` con lo que **implementa la VM hoy** (stem sigue siendo heurístico, no Snowball).

## Qué son

| Función       | Descripción breve |
|---------------|-------------------|
| `tokenizar_L` | Normaliza un **texto** (según `modo` y opciones), lo **parte** por separador o por **blancos** si el separador está vacío, y devuelve una **`lista`** de ids de cadena (tokens en caché de texto / JMN si aplica). Opcionalmente añade **bigramas** y **trigramas**. |
| `claves_L`    | **Alias** de `tokenizar_L`: misma firma, mismo bytecode y misma semántica. |

Requiere integración de lenguaje en la VM (`JASBOOT_LANG_INTEGRATION`): sin ella no hay RAM de colecciones y la lista devuelta sería inválida.

## Firma en Jasboot

```jasboot
lista L = tokenizar_L(texto_entrada)
lista L = tokenizar_L(texto_entrada, separador)
lista L = tokenizar_L(texto_entrada, separador, modo)
lista L = tokenizar_L(texto_entrada, separador, modo, stopwords_csv)
lista L = tokenizar_L(texto_entrada, separador, modo, stopwords_csv, min_len)
lista L = claves_L(...)   /* misma aridad 1..5 */
```

- **`texto_entrada`**: obligatorio.
- **`separador`**: opcional. Si falta, **`" "`** (un espacio); la VM trata `sep` vacío como **segmentación por cualquier blanco** tras normalizar (ver más abajo).
- **`modo`**: opcional. Por defecto **`3`** = minúsculas (1) + colapsar (2). Se usan los bits relevantes del entero (p. ej. **1024** / **2048** para Unicode; ver tabla).
- **`stopwords_csv`**: opcional, id de texto. Solo se usa si el modo incluye el bit **`TL_MOD_STOPWORDS` (256)**: lista **adicional** separada por comas (tras trim y minúsculas en cada término del CSV). La lista **builtin** en C (español) se aplica siempre que ese bit esté activo.
- **`min_len`**: opcional. Si es **> 0**, longitud mínima en caracteres para emitir un token o n-grama. Si es **0**, se usa solo el modo: con bit **`TL_MOD_MIN2` (8)** la longitud mínima efectiva es **2**; si no, **1**. El valor explícito se **capa a 64** en la VM.

El compilador escribe los registros **240** (modo), **241** (`min_len`), **242** (id texto stopwords CSV) antes de **`OP_STR_DIVIDIR_TEXTO`** con **`IR_INST_FLAG_SAFE`**.

## Paso 1 — `NORMALIZAR(T)` (qué hace la VM)

Objetivo alineado con `flujo_model_IA/01_tokenizacion_L.md`: dejar el texto en un buffer estable antes de **segmentar**. Orden **fijo** en código:

1. **Copia** del literal/JMN a `work[]` (tamaño `VM_TL_WORK_CAP`, hoy **8192** bytes útiles + NUL).
2. **`vm_tl_unicode_apply`** si algún bit de `VM_TL_UNICODE_APPLY_MASK` está activo:
   - **BOM** (**32768**): quita `EF BB BF` inicial.
   - **Forma Unicode** (solo **una**; prioridad estricta): **NFKC (1024) > NFKD (8192) > NFC (2048) > NFD (4096)**. Con bit **1** (`LOWER`), la forma incluye **casefold** estándar Unicode (`utf8proc`).
   - **Postproceso** (**16384** marcas, **65536** controles): segunda pasada `utf8proc_map` con `COMPOSE` + `STRIPMARK` y/o `STRIPCC` (puede ejecutarse aunque no hayas pedido NFC/NFKC/NFD/NFKD, si el texto es UTF-8 válido).
3. **Plegado Latin lite** (**128**): solo si **no** hay forma **1024…8192** (evita duplicar trabajo).
4. **Contracciones ES** (**512**): `del`, `al`, etc.
5. **Colapsar** (**2**): con **131072** + **2**, colapso Unicode-aware (`vm_tl_collapse_ws_unicode`); si no, colapso por bytes `isspace` (`vm_tl_collapse_ws`).
6. **Trim** de comillas/espacios extremos en `work`.

**Qué no cubre el paso 1 (normalización):** no hay **tailoring por locale** (p. ej. turco *i/İ*) más allá del **casefold** de Unicode cuando usas formas **1024…8192** con minúsculas. No hay normalización **por streaming** fuera del buffer `work[]` (**8191** bytes útiles + NUL): entradas más largas se **truncan** al copiar, sin código de error.

**Segmentación con `sep` vacío (`sep_len == 0`):** la VM parte por **utf8proc** (codepoint a codepoint) con el **mismo criterio de separador** que `vm_tl_collapse_ws_unicode`: categorías **Zs, Zl, Zp**, ASCII HT/LF/CR/FF/VT/SP y **U+FEFF** (`vm_tl_utf8_segment_by_whitespace` en `vm_unicode_norm.c`). Los separadores de ancho cero (p. ej. **ZWSP U+200B**) **no** cortan. UTF-8 inválido: se avanza **1 byte** sin partir (el byte permanece en el token actual). El bit **131072** + colapsar **2** sigue siendo útil para **normalizar** runs de espacio a un solo ASCII **antes** de segmentar; con solo modo **3**, NBSP entre palabras **sí** separa tokens gracias a esta segmentación.

## Modo (bits; máscara en VM `modo & 0xFFFFFFFF`)

| Valor | Constante (C)        | Efecto |
|-------|----------------------|--------|
| 1     | `TL_MOD_LOWER`       | Con **cualquier forma Unicode** (bits **1024…8192**), el casefold se aplica en el **buffer** (`utf8proc`); **no** se usa `tolower` por byte en tokens/bi/tri (evita romper UTF-8). Sin esas formas, `tolower` por byte en tokens (adecuado sobre todo para ASCII). |
| 2     | `TL_MOD_COLLAPSE`    | Colapsa separadores y recorta extremos antes de segmentar. Con bit **131072** y este bit activo, el colapso usa **categorías Unicode** Zs/Zl/Zp + ASCII (`vm_tl_collapse_ws_unicode`). Sin **131072**, solo `isspace` por byte (`vm_tl_collapse_ws`). |
| 4     | `TL_MOD_BIGRAM`      | Tras unigramas filtrados, añade **`"t_i t_{i+1}"`** donde ambos `keep`. |
| 8     | `TL_MOD_MIN2`        | Si no hay `min_len` explícito en arg 5, longitud mínima **2** (sinónimo práctico de filtro corto). |
| 16    | `TL_MOD_TRIGRAM`     | Añade **`"t_i t_{i+1} t_{i+2}"`** con los tres `keep`. |
| 32    | `TL_MOD_STEM`        | Stem **ligero** ES: sufijos largos (`mente`, `acion`, …) y `-s` tras vocal si longitud > 3. |
| 64    | `TL_MOD_STRIP_PUNCT` | Quita al inicio/final caracteres en `.,;:!?\"'``. |
| 128   | `TL_MOD_NFKC_LATIN`  | Plegado “lite” UTF-8 **`C3 xx`**. **Ignorado** si hay alguna forma Unicode **1024…8192** activa. |
| 256   | `TL_MOD_STOPWORDS`   | No emite tokens que estén en la lista builtin ES **o** en el CSV del arg 4. |
| 512   | `TL_MOD_CONTRACT`    | Sustituciones fijas: ` de el `→` del `, ` a el `→` al `, ` por que `→` porque `, ` para que `→` paraque `. |
| 1024  | `TL_MOD_UNICODE_NFKC` | **NFKC** (`utf8proc_NFKC` / `utf8proc_NFKC_Casefold` si bit **1**). |
| 2048  | `TL_MOD_UNICODE_NFC`  | **NFC**; con bit **1**: compose + casefold **sin** compat. |
| 4096  | `TL_MOD_UNICODE_NFD`  | **NFD**; con bit **1**: descomposición + casefold. |
| 8192  | `TL_MOD_UNICODE_NFKD` | **NFKD**; con bit **1**: descomposición + compat + casefold. |
| 16384 | `TL_MOD_UNICODE_STRIPMARK` | Tras la forma (o sobre texto UTF-8 válido): segunda pasada **quita marcas** (acentos combinantes), vía `utf8proc` (`COMPOSE` + `STRIPMARK`). |
| 32768 | `TL_MOD_STRIP_UTF8_BOM` | Quita **BOM UTF-8** (`EF BB BF`) al inicio del buffer **antes** de normalizar. |
| 65536 | `TL_MOD_UNICODE_STRIPCC` | Con (o sin) forma: quita / normaliza **caracteres de control** (`UTF8PROC_STRIPCC`) en la pasada de postproceso. |
| 131072 | `TL_MOD_UNICODE_WS_FULL` | Solo tiene efecto útil junto a **colapsar (2)**: colapso de espacio “Unicode-aware” (NBSP, Zl, Zp, etc.). |

**Reglas globales del modo:**

- Si `modo == 0`, se sustituye por **minúsculas + colapsar** (efectivo 3).
- Si, tras leer el modo, **no** lleva ni minúsculas ni colapsar, se **fuerzan** ambos (igual que antes).

Ejemplos:

- **`3`**: minúsculas + colapsar (por defecto).
- **`7`** = `3 | 4`: tres palabras → 3 unigramas + 2 bigramas = **5** (`test_tokenizar_L.jasb`).
- **`1027`** = NFKC + casefold + colapsar (`test_tokenizar_L_unicode_nfkc.jasb`, `Straße` → un token).
- **`8195`** = NFKD + colapsar + casefold (ligaduras compat, p. ej. `ﬁlm` → `film`).
- **`17411`** = NFKC + strip marcas + colapsar + casefold (`café` → `cafe`).
- **`131075`** = colapso Unicode + colapsar + lower (NBSP y demás Zs/Zl/Zp se colapsan a espacio ASCII antes de segmentar; `hola` + NBSP + `mundo` → dos tokens). Con solo modo **`3`**, NBSP entre palabras **también** produce dos tokens (segmentación Unicode en `sep` vacío).

Orden interno tras la normalización en `work` (incl. trim comillas en `work`): **segmentar** (`vm_tl_segment_fill`; con `sep_len==0`, lógica Unicode anterior) → por token: trim, strip punct (**64**), `tolower` por byte **solo** si no hubo forma Unicode **1024…8192**, stem (**32**), `keep` → lista → bigramas (**4**) → trigramas (**16**).

## Separador

Tras normalizar en el buffer de trabajo, `sep_len = strlen(separador)`:

> **Omitir el segundo argumento** no es lo mismo que pasar `""`: si falta el argumento, el compilador usa el literal **`" "`** (un espacio, `sep_len == 1`). La cadena vacía **`""`** implica **`sep_len == 0`** (segmentación por cualquier blanco).

| `sep_len` | Comportamiento |
|-----------|----------------|
| **0**     | **Separadores Unicode de espacio** (Zs/Zl/Zp, ASCII HT/LF/FF/CR/VT/SP, U+FEFF) vía `utf8proc_iterate`; implementación `vm_tl_utf8_segment_by_whitespace`. Trozos = runs de no-separador. |
| **1**     | Delimitador **un carácter** (p. ej. `","`). |
| **> 1**   | Delimitador **`strstr`** con la subcadena completa. |

En segmentos delimitados se recortan espacios y comillas al inicio/fin antes de contar longitud.

## Diferencia con `dividir_texto` / `dividir`

- **`dividir_texto`** usa **`OP_STR_DIVIDIR_TEXTO`** **sin** `IR_INST_FLAG_SAFE` y convención **A == B**.
- **`tokenizar_L` / `claves_L`** usan **`IR_INST_FLAG_SAFE`** y **A ≠ B**; la VM ejecuta **`vm_run_tokenizar_L`** (registros 240–242).

## Límites (comportamiento actual)

| Ámbito | Límite | Consecuencia |
|--------|--------|--------------|
| Texto de trabajo | **8191 bytes útiles + NUL** (`VM_TL_WORK_CAP`) | Entradas más largas se truncan al copiar; sin error explícito. |
| Tokens segmentados | **`VM_TL_MAX_TOK` = 384** × **`VM_TL_MAX_CH` = 256** chars | Segmentos extra se ignoran; cada token copiado recorta a **255** chars útiles + NUL. |
| Bigramas | Buffer interno **1024** bytes | Unión `a` + espacio + `b` por copia byte a byte; tokens acotados a **255** chars caben siempre. |
| Trigramas | Buffer interno **1536** bytes | Igual para tres tokens. |
| Stopwords CSV extra | Hasta **128** entradas × **47** chars útiles + NUL | Tras parseo por comas. |
| Unicode | **utf8proc** 2.11.3 (`third_party/utf8proc/`): NFC, NFD, NFKC, NFKD, casefold, strip marcas/CC, colapso Zs/Zl/Zp; datos embebidos ~2,3 MiB. | Ver `LICENSE.md`. Sin tailoring regional extra. |
| Id de lista | `hash(texto_raw) ^ hash(sep) ^ 0x544B4E4C` | Colisiones teóricas posibles entre llamadas distintas. |

## Casos de uso

1. Claves para JMN tras normalizar mayúsculas/espacios.  
2. CSV: `claves_L(linea, ",")`.  
3. Features con **bigramas/trigramas** combinando bits 4 y 16.  
4. Ruido: **`min_len`** o bit **MIN2**; stopwords con bit **256** + CSV opcional.  
5. Texto con tildes en UTF-8 Latin-1 supplement: bit **128**.

## Buenas prácticas en el compilador Jasboot

En bucles, **`mientras i < N - 1`** puede parsearse de forma no intuitiva; usar **`entero lim = N - 1`** o paréntesis **`(N - 1)`**, como en `test_tokenizar_L_estres_200.jasb`.

## Pruebas de regresión

| Archivo | Qué comprueba |
|---------|----------------|
| `jas-compiler-c/tests/test_tokenizar_L.jasb` | Espacios, comas, modo 7 (bigramas), modo 23 (bi+tri). |
| `jas-compiler-c/tests/test_tokenizar_L_unicode_nfkc.jasb` | NFKC/NFC/NFKD/strip/NBSP (**1027**, **2051**, **8195**, **17411**, **131075**, ASCII **3**); salida **`UNICODE_NFKC_FALLIOS=0`**. |
| `jas-compiler-c/tests/test_tokenizar_L_estres_200.jasb` | **200** escenarios (longitud, modos 7/11/23/67/259); salida **`CASOS=200 FALLIOS=0`**. |
| `jas-compiler-c/tests/test_tokenizar_L_seg_ws_unicode_300.jasb` | **300** casos: NBSP, EM SPACE, Zl/Zp, FEFF, modos **3** / **1027**; **`CASOS=300 FALLIOS=0`**. Generador: `tests/_gen_tokenizar_L_seg_ws_unicode_300.py`. |

```bash
node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L.jasb
node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_unicode_nfkc.jasb
node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_estres_200.jasb
node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_seg_ws_unicode_300.jasb
```

## Referencias de código

- VM: **`vm_tokenizar_l_pipeline.inc`**, **`vm_unicode_norm.c`** / **`vm_unicode_norm.h`** (normalización, colapso, **`vm_tl_utf8_segment_by_whitespace`**), **`vm_tokenizar_unicode_bits.h`**, **utf8proc** (`third_party/utf8proc/`).

**Última revisión:** 2026-05-14 — Segmentación `sep_len==0` Unicode-aware; prueba **`test_tokenizar_L_seg_ws_unicode_300.jasb`**; paso 1 (NFD/NFKD, BOM, strip, colapso WS, buffer **8192**).
