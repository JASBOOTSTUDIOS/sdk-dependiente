# Pipeline L nativo: `tokenizar_L` y `claves_L` (SDK)

Documento de **comportamiento y límites** alineado con el código bajo `sdk-dependiente/` (`jas-compiler-c`, `jasboot-ir`). Complementa la visión de producto en `flujo_model_IA/01_tokenizacion_L.md` (objetivos del pipeline L) con lo que **hoy implementa la VM**.

## Qué son

| Función     | Descripción breve |
|-------------|-------------------|
| `tokenizar_L` | Normaliza un **texto** (según `modo`), lo **parte** por separador o por **blancos** si el separador está vacío, y devuelve una **`lista`** de ids de cadena (tokens en caché de texto / JMN si aplica). |
| `claves_L`    | **Alias** de `tokenizar_L`: misma firma, mismo bytecode y misma semántica. Útil para nombrar el paso “claves para JMN” en flujos de modelo. |

Requieren integración de lenguaje en la VM (`JASBOOT_LANG_INTEGRATION` en el build de `jasboot-ir`): sin ella no hay RAM de colecciones y la lista devuelta sería inválida.

## Firma en Jasboot

```jasboot
lista L = tokenizar_L(texto_entrada)
lista L = tokenizar_L(texto_entrada, separador)
lista L = tokenizar_L(texto_entrada, separador, modo)
lista L = claves_L(texto_entrada)
lista L = claves_L(texto_entrada, separador)
lista L = claves_L(texto_entrada, separador, modo)
```

- **`texto_entrada`**: obligatorio.
- **`separador`**: opcional. Si falta, se usa un **único espacio** `" "` (segmentación “tipo palabra” respecto a espacios en el texto ya normalizado).
- **`modo`**: opcional. Si falta, se usa **`3`** = minúsculas (1) + colapsar espacios (2). Ver tabla de bits más abajo.

Los elementos de la lista son **identificadores de texto** (hashes) coherente con el resto del runtime; para mostrar o persistir el literal suele usarse la caché de texto / JMN según el contexto.

## Modo (tercer argumento y registro interno 240)

La VM lee el modo desde el registro **240** inmediatamente antes de ejecutar el opcode del pipeline L (`vm_run_tokenizar_L` en `jasboot-ir/src/vm.c`). El compilador escribe ahí el tercer argumento o el inmediato por defecto.

Solo se interpreta el **byte bajo** (`modo & 0xFF`). Bits definidos:

| Bit | Constante (C)   | Efecto |
|-----|-----------------|--------|
| 1   | `TL_MOD_LOWER`  | Pasa tokens (y bigramas) a **minúsculas** ASCII (`tolower`). |
| 2   | `TL_MOD_COLLAPSE` | **Colapsa** espacios en blanco y recorta comillas/espacios extremos antes de segmentar. |
| 4   | `TL_MOD_BIGRAM` | Tras los unigramas, añade **bigramas** `"t_i t_{i+1}"` a la lista (misma política de minúsculas si aplica). |
| 8   | `TL_MOD_MIN2`   | No emite tokens cuya longitud sea **&lt; 2** (tras trim). |

**Reglas de normalización del modo:**

- Si `modo == 0`, se sustituye por **minúsculas + colapsar** (valor efectivo 3).
- Si no lleva ni minúsculas ni colapsar activos, se les **fuerza** también (no se puede desactivar hoy por flags solo; combina con la documentación de evolución si en el futuro se relaja).

Ejemplos de valores útiles:

- **`3`**: minúsculas + colapsar (por defecto).
- **`7`**: lo anterior + bigramas (`3 | 4`). Ejemplo verificado: tres palabras → **3** unigramas + **2** bigramas = **5** entradas en la lista (`test_tokenizar_L.jasb`).

## Separador: tres comportamientos

Tras normalizar en el buffer de trabajo, `sep_len = strlen(separador)`:

| `sep_len` | Comportamiento |
|-----------|----------------|
| **0**     | Segmentación por **cualquier espacio en blanco** (`isspace`): trozos de no-blancos son candidatos a token. |
| **1**     | El carácter `separador[0]` es el **delimitador** (p. ej. `","`). |
| **&gt; 1** | Se usa **`strstr`** con la subcadena completa como delimitador (multi-carácter). |

En segmentos delimitados se recortan **espacios y comillas** simples/dobles al inicio y fin de cada trozo antes de contar longitud y emitir el token.

## Diferencia con `dividir_texto` / `dividir`

- **`dividir_texto(texto, sep)`** usa el mismo opcode **`OP_STR_DIVIDIR_TEXTO`** pero **sin** `IR_INST_FLAG_SAFE` y con convención de operandos **A == B** (lista resultado en el registro de destino de la división clásica).
- **`tokenizar_L` / `claves_L`** emiten **`IR_INST_FLAG_SAFE`** y operandos **A ≠ B** (destino de lista vs registros de ids de texto y separador). La VM entonces ejecuta **`vm_run_tokenizar_L`** (pipeline L + registro 240).

No añas un opcode nuevo distinto de `0x5C` en el IR actual: el espacio de opcodes de byte está saturado.

## Casos de uso

1. **Preparar claves para memoria neuronal**  
   Normalizar mayúsculas/espacios y obtener una lista estable de strings para `recordar` / `asociar_relacion` / búsquedas por hash de texto.

2. **CSV o listas separadas por comas**  
   `claves_L(linea, ",")` con modo por defecto: tokens recortados y en minúsculas.

3. **Tokenización “tipo shell” por blancos**  
   Separador `""` o flujo interno con `sep_len == 0`: palabras separadas por espacios/tabs tras colapsar.

4. **Features léxicos para modelos (pipeline L)**  
   Activar **bigramas** (`modo` con bit 4) para enriquecer candidatos antes de mapear a nodos (ver diagrama en `flujo_model_IA/01_tokenizacion_L.md`).

5. **Filtrado mínimo de longitud**  
   `modo` con bit 8 para ignorar tokens de un solo carácter (ruido, puntuación suelta tras recortes).

6. **Contraste con división simple**  
   Cuando solo necesites **partir** por un separador sin normalización previa ni modo, sigue siendo más adecuado **`dividir_texto`**.

## Límites claros (comportamiento actual)

| Ámbito | Límite | Consecuencia |
|--------|--------|--------------|
| Texto de trabajo | **4095 bytes útiles + NUL** (`work[4096]` + `strncpy`) | Entradas más largas se **truncan** antes de normalizar y segmentar. No hay error explícito: el resultado corresponde al **prefijo** truncado. |
| Colapso de espacios | Buffer auxiliar **4096** | Misma ventana que el trabajo; textos enormes ya truncados. |
| Cada token | **`token_norm[512]`** | Si un segmento supera **511** caracteres útiles + NUL, se **recorta** a 512 al copiar; el hash y la lista reflejan el token **truncado**. |
| Bigramas | Buffer **`bi[1024]`** | Si `"t_i" + " " + "t_{i+1}"` supera el buffer interno, `snprintf` trunca; el bigrama almacenado puede estar **incompleto**. |
| Bigramas y unigramas internos | Array **`unigs[256]`** | Solo los **primeros 256** unigramas **emitidos** rellenan `unigs`; los bigramas en bucle solo recorren **hasta 255 pares** entre esos ids. Los tokens que sigan **sí** se añaden a la lista como unigramas, pero **no** generan bigrama vía ese array si ya pasaron de 256 unigramas contados en `unigs`. |
| Unicode | `tolower` / `isspace` **byte a byte** | No hay NFC/NFKC ni reglas Unicode completas; pensado para **ASCII / Latin-1** típico en literales `.jasb`. |
| Colecciones | Primera lista en el proceso | La VM llama **`ensure_jmn_col`** si hay texto válido, para que la **primera** `tokenizar_L` no falle con `mem_colecciones` aún NULL. |
| Id de lista | `hash(texto_raw) ^ hash(sep) ^ 0x544B4E4C` | Colisiones teóricas entre dos entradas distintas son posibles; en la práctica raras. Si colisionara, dos llamadas distintas podrían **reutilizar** la misma lista lógica hasta que se vuelva a crear. |
| Rendimiento | Muchas concatenaciones en Jasboot | Construir cadenas de miles de caracteres con **`concatenar` en bucle** es lento (prueba de estrés `test_tokenizar_L_estres_100_gigantes.jasb`). Para datos masivos, mejor **I/O nativo** o generar `.jbo` desde herramientas externas. |

## Buenas prácticas en el compilador Jasboot

- En condiciones de bucle, **`mientras i < N - 1`** puede **parsearse de forma no intuitiva**. Usar variable auxiliar: **`entero lim = N - 1`** y **`mientras i < lim`**, o paréntesis **`(N - 1)`**, como en otros módulos del repo (`detector_intencion_config.jasb`, etc.). Ver comentario en `test_tokenizar_L_estres_100_gigantes.jasb`.

## Pruebas de regresión (SDK)

| Archivo | Qué comprueba |
|---------|----------------|
| `jas-compiler-c/tests/test_tokenizar_L.jasb` | Casos básicos: espacios, comas, modo 7 (bigramas). |
| `jas-compiler-c/tests/test_tokenizar_L_estres_100_gigantes.jasb` | **120** escenarios con cadenas largas y bordes del buffer; salida **`CASOS=120 FALLIOS=0`**. |

Ejecución típica desde la raíz del monorepo:

```bash
node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L.jasb
node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_estres_100_gigantes.jasb
```

## Referencias de código

- VM: `jasboot-ir/src/vm.c` — `vm_run_tokenizar_L`, despacho **`OP_STR_DIVIDIR_TEXTO`** con `IR_INST_FLAG_SAFE`.
- IR: `jasboot-ir/src/ir_format.h` — comentario del opcode **`0x5C`**.
- Compilador: `jas-compiler-c/src/codegen.c` — emisión de `tokenizar_L` / `claves_L`; `keywords.c` / `sistema_llamadas.c` — incorporación al lenguaje.

**Última revisión:** 2026-05-14 — alineado con `vm_run_tokenizar_L`, `ir_format.h`, tests del SDK anteriores.
