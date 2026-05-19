# Capas de memoria y cognición: JMN, VM, MAI y uso en Jasboot (incl. Neurixis)

Este documento describe **de arriba abajo** las capas implicadas en memoria neuronal, activación, percepción y MAI (“Memoria Activa Independiente”), con **funcionalidades**, **cuándo usar cada pieza** y **ejemplos en Jasboot**. Está alineado con el SDK en `sdk-dependiente/` (compilador **jbc**, VM **jasboot-ir**, núcleo **jasboot-jmn-core**).

**Ámbito:** lenguaje y runtime del proyecto. Las aplicaciones (p. ej. **Neurixis** en `apps/neurixis/`) consumen estas mismas llamadas; la semilla y el generador de Neurixis son **clientes** de estas capas, no un motor paralelo.

---

## 1. Visión general del flujo

```
Fuente .jasb
    → Compilador (jbc): AST → bytecode IR (.jbo)
        → VM (jasboot-ir-vm): registros, opcodes, integración opcional JMN + MAI
            → JMN (grafo: nodos + conexiones tipadas y ponderadas, .jmn / .jwl)
            → MAI (colas, prioridades, contexto reciente, mensajes ligados a activación)
    → Aplicación (Neurixis, mai_laboratorio, etc.): menú, semilla, razonamiento conversacional
```

| Capa | Rol principal | Dónde vive |
|------|----------------|------------|
| **Compilador** | Traduce llamadas incorporadas a opcodes + flags | `sdk-dependiente/jas-compiler-c/` |
| **IR / bytecode** | Instrucciones fijas (256 opcodes); variantes MAI reutilizan opcode + flag | `sdk-dependiente/jasboot-ir/src/ir_format.h` |
| **VM** | Ejecuta IR, llama a JMN/MAI, percepción, rastro | `sdk-dependiente/jasboot-ir/src/vm.c` |
| **JMN** | Persistencia asociativa: nodos, aristas por **tipo** y **fuerza** | `sdk-dependiente/jasboot-jmn-core/` |
| **MAI** | Foco, refuerzo, contexto reciente, scheduling ligado a eventos de activación | `sdk-dependiente/jasboot-ir/src/mai.c`, hooks en `vm.c` |
| **App (Neurixis)** | Orquestación: semilla, terminal, generador | `apps/neurixis/` |

---

## 2. Capa compilador e IR

### Funcionalidad

- Resuelve **llamadas de sistema** (`recordar`, `asociar_relacion`, `propagar_activacion`, …) a secuencias de **opcodes** con registros o inmediatos.
- Las variantes **`*_mai`** (`asociar_relacion_mai`, `pensar_siguiente_mai`, `propagar_activacion_mai`, `buscar_asociados_lista_mai`) emiten el **mismo opcode** que la API clásica, pero con el flag **`IR_INST_FLAG_RELATIVE`** en la instrucción para que la VM active la rama MAI / máscaras / tipos extendidos sin consumir un opcode nuevo.

### Uso

- Código Jasboot portable: mismas palabras reservadas y mismas firmas “clásicas”; las `*_mai` son **funciones incorporadas adicionales** (no sustituyen a las antiguas).
- Para depuración del bytecode: variable de entorno **`JASBOOT_DEBUG=1`** al ejecutar la VM (trazas en stderr según opcode).

### Ejemplo (solo idea de flujo)

```jasboot
# Clásico: una arista tipo asociación (1) entre dos conceptos por texto
asociar_relacion("agua", "vida", 1, 0.7)

# MAI: misma operación base, rama VM extendida (tipos 6–15, refuerzo MAI, etc.)
asociar_relacion_mai("agua", "cultivo", 7, 0.4)
```

---

## 3. Capa VM (máquina virtual)

### Funcionalidad

- Ejecuta el **IR**: registros, pila, memoria lineal del programa.
- Cuando la memoria neuronal está abierta (`crear_memoria` / `abrir_memoria`), opcodes como **`OP_MEM_ASOCIAR_RELACION`**, **`OP_MEM_PROPAGAR_ACTIVACION`**, **`OP_MEM_PENSAR_SIGUIENTE`**, etc., delegan en **jmn_*` y opcionalmente notifican a **MAI** (`mai_send_message` / `mai_send_message_ex`).
- **Percepción** y **rastro de activación** son estructuras de la VM asociadas al ciclo de inferencia (ventanas, listas, índices).

### Uso típico

- No se programa la VM en C desde Jasboot; se programa con **llamadas incorporadas** documentadas en la referencia del lenguaje.
- Distinguir: operaciones que devuelven **texto** (`pensar_siguiente`, `buscar` según contexto) vs **identificadores** (`aprender`, `propagar_activacion`).

### Ejemplo mínimo VM + JMN

```jasboot
principal
 crear_memoria("mi_mem.jmn")
 recordar "hola" con valor "saludo"
 buscar "hola"
 imprimir resultado
 cerrar_memoria()
fin_principal
```

---

## 4. Capa JMN (Memoria neuronal Jasboot)

### Funcionalidad

- **Nodos:** conceptos identificados por **id** (hash de texto u otros esquemas internos).
- **Conexiones:** aristas **origen → destino** con **`key_id` = tipo de relación** y **`fuerza`** (float, típicamente 0–1).
- **Búsqueda:** `jmn_buscar_asociaciones` filtra por `tipo_rel` (0 = cualquier tipo que pase umbral en la ruta rápida) y umbral de fuerza.
- **Propagación:** BFS aproximado sobre aristas del tipo pedido, combinando activación con fuerza y factor de decaimiento por profundidad.
- **Persistencia:** archivo **`.jmn`**; journal opcional **`.jwl`** (replay, checkpoint — ver [jmn/JMN_JOURNAL_Y_CONSOLIDACION.md](jmn/JMN_JOURNAL_Y_CONSOLIDACION.md)).

### Tipos de relación (códigos 1–15)

Resumen: cada conexión lleva `key_id` = tipo y una fuerza. **Detalle completo**, alias en texto, máscaras y casos especiales de la VM: **[TIPOS_RELACION_JMN.md](TIPOS_RELACION_JMN.md)**.

| Código | Nombre (constante C) | Idea corta |
|--------|----------------------|------------|
| 1 | ASOCIACION | Asociación libre |
| 2 | PATRON | Patrón |
| 3 | SECUENCIA | Orden / siguiente |
| 4–5 | SIMILITUD / OPOSICION | Inversa automática al asociar |
| 6–15 | PERTENENCIA … CONDICION | Ontología extendida (ver doc dedicada) |

**Importante:** los tipos **14–15** (y el resto de 6–15) son **primero etiquetas en el grafo**; reglas lógicas “si/entonces” van en la aplicación si las necesitas.

### Uso en Jasboot

| Necesidad | Llamadas típicas |
|-----------|------------------|
| Crear / abrir grafo | `crear_memoria(ruta)` o `crear_memoria(ruta, cap_nodos, cap_conex)` |
| Guardar concepto + valor | `recordar "clave" con valor "texto"` |
| Arista tipada | `asociar_relacion(a, b, tipo, peso)` o `asociar_relacion_mai(...)` |
| Peso entre dos nodos | `buscar_peso(a, b)` |
| Lista de vecinos | `buscar_asociados_lista(origen, K, tipo)` o `..._mai(..., mascara)` |
| Mapa por varias consultas y rango de peso | `buscar_asociados_rango(lista_claves, min_f, max_f)` |
| Cadena lineal como lista | `obtener_secuencia(concepto_raiz)` |
| Mantenimiento | `decae_conexiones`, `consolidar_sueno`, `consolidar_memoria`, `cerrar_memoria` |

### Ejemplo: grafo pequeño con secuencia y asociación

```jasboot
principal
 crear_memoria("demo.jmn")
 recordar "paso1" con valor "inicio"
 recordar "paso2" con valor "medio"
 asociar_relacion("paso1", "paso2", 3, 0.9)
 lista s = obtener_secuencia("paso1")
 imprimir str_desde_numero(lista_tamano(s))
 cerrar_memoria()
fin_principal
```

---

## 5. Capa “pensar siguiente” clásica vs `pensar_siguiente_mai`

### Clásico (`pensar_siguiente`)

- Prioriza **secuencia** (tipo 3), con variante contextual si se pasa un segundo argumento (id de contexto).
- Comportamiento estable y compatible con apps antiguas.

### MAI (`pensar_siguiente_mai`)

- Recorre un **conjunto fijo de tipos** (secuencia, asociación, pertenencia, causalidad, temporalidad, intención, valorativa) y elige la arista más fuerte entre ellas; notifica a MAI al acertar.
- **No incluye hoy** en ese barrido explícito: ubicación (11), propiedad (12), parte_de (13), consecuencia (14), condición (15). Esas aristas **sí existen** en JMN y se pueden usar con **búsqueda/lista/propagación por tipo o máscara**.

### Ejemplo

```jasboot
texto sg = pensar_siguiente("paso1")
texto sg_mai = pensar_siguiente_mai("paso1")
```

---

## 6. Capa propagación de activación

### Clásico (`propagar_activacion` / `propagar_activacion_de`)

- Documentación detallada ( **`d_max`**, **`K`**, límites, auditoría, estrés ): [**`PROPAGACION_D_MAX_K_AUDITORIA_Y_ESTRES.md`**](jmn/PROPAGACION_D_MAX_K_AUDITORIA_Y_ESTRES.md).
- **Empaquetado de parámetros en un registro C:** `tipo` (byte bajo), `K` y **`d_max`** (antes “profundidad” en comentarios antiguos) en bytes altos; ver implementación en `codegen.c` / `vm.c`.
- **`propagar_activacion(concepto [, d_max [, K]])`:** argumentos opcionales; por defecto **`d_max = 3`**, **`K = 8`** si se omiten.
- **`propagar_activacion_de(origen, tipo_literal [, d_max [, K]])`:** el segundo argumento debe ser **literal entero** en tiempo de compilación (p. ej. 0–10) para fijar el tipo de propagación; luego opcionales `d_max` y `K` como en la forma clásica.

### MAI (`propagar_activacion_mai`)

- **Un argumento:** máscara de tipos = 0 → la VM aplica una **máscara por defecto** (varios bits de tipos “cognitivos”).
- **Dos argumentos:** `(origen, mascara)` donde `mascara` es un entero cuyos **bits 1..15** seleccionan qué tipos compiten; **`K` y `prof`** van en **bytes altos** del mismo valor C (layout RELATIVE: bits bajos 16 = máscara, luego `K<<16`, `prof<<24` — ver implementación en VM y codegen).
- El ganador puede disparar **MAI** con prioridad más alta que la rama no-RELATIVE.

### Ejemplo

```jasboot
entero gan = propagar_activacion("concepto_inicial")
entero gan_mai = propagar_activacion_mai("concepto_inicial")
entero m = 10
entero gan_mask = propagar_activacion_mai("concepto_inicial", m)
```

---

## 7. Capa listas de asociados: clásica vs `buscar_asociados_lista_mai`

- **Clásica:** `(origen, K, tipo)` — un solo tipo (o 0 según semántica VM).
- **MAI:** tercer argumento como **máscara de bits** por tipo; 0 = máscara por defecto en VM; fusiona resultados de varios tipos y toma el mejor peso por destino.

```jasboot
lista L1 = buscar_asociados_lista("agua", 10, 1)
lista L2 = buscar_asociados_lista_mai("agua", 12, 0)
```

---

## 8. Capa rastro de activación

### Funcionalidad

- Registra **qué nodos** recibieron activación durante operaciones como propagación (callback desde JMN).
- Permite inspeccionar **tamaño**, **elemento por índice**, **peso por índice** y **lista** reciente.

### Uso

- Depuración, explicabilidad (“por qué saltó este concepto”), y futuras UIs que muestren “cadena de activación”.

### Ejemplo

```jasboot
rastro_activacion_limpiar()
ventana_rastro_activacion(128)
entero g = propagar_activacion("mi_concepto")
entero g2 = propagar_activacion("mi_concepto", 2, 16)
entero n = rastro_activacion_tamano()
entero id0 = rastro_activacion_obtener(0)
lista lr = rastro_activacion_lista()
```

---

## 9. Capa percepción

### Funcionalidad

- Ventana deslizante de **focos** recientes (`ventana_percepcion`, `flujo_temporal`).
- **`percepcion(concepto)`** registra un id en la ventana.
- **`percepcion_recientes`**, **`percepcion_tamano`**, **`percepcion_anterior`**: lectura.

### Uso

- Modelar “atención” explícita del sistema a ciertos nodos durante un diálogo (Neurixis puede alimentar esto al procesar entrada en vivo).

### Ejemplo

```jasboot
ventana_percepcion(32)
percepcion("usuario_tema")
entero t = percepcion_tamano()
lista pr = percepcion_recientes()
```

---

## 10. Capa MAI (Memoria Activa Independiente)

### Funcionalidad (resumen operativo)

- **Mensajes** entre conceptos (activación, refuerzo, etc.) con **prioridad** (`mai_send_message_ex`).
- **Anillo de contexto** (`mai_contexto_recientes`, `mai_contexto`): nodos recientes relevantes para el turno actual.
- Integración con **`buscar`**, **propagación** y aristas **`_mai`**: refuerzo y prioridades distintas para no competir mal con la cola general.
- **Sueño / consolidación** del lado MAI-JMN: `consolidar_sueno` y variables documentadas en [jmn/JMN_JOURNAL_Y_CONSOLIDACION.md](jmn/JMN_JOURNAL_Y_CONSOLIDACION.md) y `AGENTS.md`.

### Uso recomendado en apps

1. Tras **activar** un concepto (`buscar`, propagación, reforzar), dar tiempo con **`esperar_milisegundos`** si hace falta procesar ciclos.
2. Leer **`mai_contexto_recientes(k)`** para condicionar la generación de texto o la siguiente búsqueda.
3. Persistir con **`consolidar_memoria`** antes de cerrar.

### Ejemplo

```jasboot
buscar "tema_actual"
esperar_milisegundos(100)
lista ctx = mai_contexto_recientes(8)
```

---

## 11. Neurixis: cómo encaja

Neurixis (`apps/neurixis/`) **no redefine** JMN/MAI: usa las mismas llamadas desde `neurixis.jasb`, módulos `generador.jasb`, `terminal_cognitivo.jasb`, semilla en `datos/semilla_conocimiento.txt`, y memoria bajo `apps/neurixis/memoria/jmn/`.

- **Semilla:** carga masiva de nodos y aristas (incl. tipos extendidos si las anotas en la semilla o con `asociar_relacion_mai` en código).
- **Generador:** compone respuesta leyendo listas, propagación, percepción, contexto MAI.
- **Terminal:** entrada en vivo; la percepción puede actualizarse antes de cerrar la línea.

Para requisitos evolutivos del terminal y MAI, ver también `apps/neurixis/docs/REQUISITOS_FUTUROS_SEMILLA_MAI_TERMINAL.md`.

### Laboratorio de prueba en el repo

- `apps/mai_laboratorio/mai_laboratorio.jasb` — menú interactivo que ejercita la mayoría de llamadas.
- `tests/test_capas_jmn_mai_completo.jasb` — recorrido automático con comprobaciones.
- `node tools/run_mai_laboratorio_smoke.cjs` — envía `99` por stdin y sale (CI rápido).

---

## 12. Tabla rápida: qué usar según objetivo

| Objetivo | Preferir |
|----------|-----------|
| Compatibilidad con código antiguo | APIs **sin** `_mai` |
| Ontología rica (causalidad, pertenencia, …) y refuerzo MAI en arista | `asociar_relacion_mai` |
| Siguiente paso “multi-relación” con MAI | `pensar_siguiente_mai` |
| Competir varias familias de aristas en propagación | `propagar_activacion_mai` + máscara |
| Vecinos fusionados por varios tipos | `buscar_asociados_lista_mai` |
| Explicar por qué se activó X | `rastro_activacion_*` |
| Atención explícita | `percepcion_*`, `ventana_percepcion` |
| Foco conversacional reciente | `mai_contexto_recientes`, `mai_contexto` |
| Reglas lógicas “si A entonces B” | **Aún:** política en Jasboot; **no** solo tipos 14/15 en el grafo |

---

## 13. Referencias cruzadas

- [TIPOS_RELACION_JMN.md](TIPOS_RELACION_JMN.md) — tipos de arista 1–15, alias, simetrías VM, máscaras y `pensar_siguiente_mai`.
- [REFERENCIA_LENGUAJE_JASBOOT.md](REFERENCIA_LENGUAJE_JASBOOT.md) — sintaxis y llamadas del lenguaje.
- [jmn/JMN_JOURNAL_Y_CONSOLIDACION.md](jmn/JMN_JOURNAL_Y_CONSOLIDACION.md) — `.jwl`, consolidación, variables de entorno.
- [../TECNICO/VM/VM_ESTABLE.md](../TECNICO/VM/VM_ESTABLE.md) — estabilidad y depuración VM.
- [../../AGENTS.md](../../AGENTS.md) — checklist agentes, rutas SDK, patrones seguros.
- [../PROYECTO/CHECKLIST_INSTRUCCIONES_JASBOOT_CATALOGO_TESTS.md](../PROYECTO/CHECKLIST_INSTRUCCIONES_JASBOOT_CATALOGO_TESTS.md) — catálogo de instrucciones y baterías.

---

*Documento generado para la documentación principal del proyecto Jasboot. Última revisión de contenido: 2026-05-13.*
