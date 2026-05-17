# Requisitos futuros: semilla MAI / ontología y terminal asíncrona

Documento de referencia para implementación posterior. Resume la dirección acordada (semilla como grafo cognitivo, no solo texto; diálogo proactivo en terminal) y las dependencias técnicas con Jasboot/JMN/VM.

**Estado:** borrador de requisitos — no implementado.  
**Ámbito:** Neurixis, memoria neuronal (JMN), subsistema MAI si aplica, capa de E/S.

---

## 1. Objetivo general

1. Evolucionar la **semilla** desde “texto conectado” / dataset de respuestas hacia una **ontología neuronal viva**: más estructurada, diferenciada, abstracta, composicional y dinámica.
2. Opcionalmente (producto/runtime), soportar **comunicación en terminal** donde la IA puede **hacer preguntas en silencios**, **respetar cuando el usuario escribe** y **re-enganchar** sin frases fijas hardcodeadas.

---

## 2. Semilla y conocimiento

### 2.1 Principios de diseño

| Principio | Descripción |
|-----------|-------------|
| Estructura | Capas o particiones claras de conocimiento vs. superficie lingüística. |
| Diferenciación | No mezclar al mismo nivel “hecho”, “secuencia léxica”, “meta-diálogo” y “empatía” sin tipos o namespaces. |
| Abstracción | Preferir relaciones reutilizables (`campo_cientifico`, `estudia`, …) antes que solo cadenas `A → es → B`. |
| Composición | El razonamiento y la generación deben poder **combinar** aristas de varios tipos, no solo recorrer secuencias tipo 3. |
| Dinamismo | El grafo crece y se consolida con políticas explícitas (aprendizaje incremental, olvido selectivo, conflictos). |

### 2.2 Separación explícita: conocimiento vs. lenguaje

- **Conocimiento (núcleo):** jerarquías (clase / instancia), causalidad, temporalidad, intención, valor/prioridad — **no** frases completas como unidad atómica principal.
- **Lenguaje (superficie):** secuencias léxicas, plantillas, transiciones palabra a palabra — como **capa superficial** conectada al núcleo, no como único contenido del grafo.

**Criterio de aceptación (dirección):** se puede identificar en datos y en código qué aristas/nodos pertenecen a “concepto/mundo” y cuáles a “forma de decir”.

---

## 3. Capas cognitivas de la semilla (referencia)

Numeración de **tipos de ejemplo** en el análisis original es orientativa; en implementación hay que **mapear** a tipos JMN reales o extender el formato.

### 3.1 Capa léxica (átomos)

- Símbolos, palabras, categorías primitivas (p. ej. palabra → tipo léxico/semántico con peso).
- Convierte texto en **entidades** direccionables, no solo strings sueltos.

### 3.2 Capa conceptual

- Relaciones de mundo: pertenencia / clase / taxonomía (p. ej. `perro → animal`).
- **Jerarquía explícita:** cadenas tipo `perro → mamífero → animal → ser_vivo → organismo` para abstracción y generalización.

### 3.3 Capa causal

- Relaciones **causa → efecto** (inferencia, predicción, “qué pasaría si”).
- Prioridad alta frente a asociación genérica para razonamiento.

### 3.4 Capa temporal

- Anclajes y relaciones temporales (pasado/futuro, duración, orden de eventos).
- Sin temporalidad modelada, el comportamiento conversacional queda anclado al “presente textual”.

### 3.5 Capa intencional

- Objetivos, necesidades, preguntas como actos (p. ej. `preguntar → obtener_información`).
- Soporta modelar **por qué** ocurre un turno o una acción.

### 3.6 Capa emocional / valorativa (no “magia humana”)

- Prioridad, urgencia, amenaza, recompensa, corrección — para **atención** y **consolidación** de memoria.

### 3.7 Capa lingüística

- Secuencias y co-ocurrencias léxicas (p. ej. tipo secuencia / patrón actual en JMN).
- Debe ser **derivable** o enlazada al núcleo conceptual, no el único sustrato cognitivo.

---

## 4. Tipos de relación enriquecidos

Además de los tipos ya contemplados en el proyecto (asociación, secuencia, similitud, oposición, etc.), el análisis pide soportar **semántica distinta** para:

- Pertenencia / clase  
- Causalidad  
- Temporalidad  
- Intención  
- Ubicación  
- Propiedad  
- `parte_de`  
- Consecuencia  
- Condición  

**Requisito de implementación:** no basta con etiquetar la semilla con números nuevos; hace falta que **VM/JMN** (carga, búsqueda, `pensar_siguiente` o equivalentes, métricas, journal si existe) **interpreten** cada tipo con reglas coherentes. Documentar el catálogo oficial de tipos y su significado algorítmico.

---

## 5. Niveles de aprendizaje (referencia)

Orden conceptual para políticas de ingesta y consolidación:

1. Texto superficial  
2. Conceptos  
3. Relaciones  
4. Causalidad  
5. Patrones recurrentes  
6. Abstracciones  

**Criterio:** el pipeline de aprendizaje (Neurixis / MAI) debe poder **clasificar** o **enrutar** nuevas aristas hacia el nivel adecuado y no colapsar todo en secuencias léxicas.

---

## 6. Ventajas a optimizar (vs. LLM genérico)

La semilla y el motor deben favorecer:

- Memoria **persistente** y **trazable**  
- Aprendizaje **incremental**  
- Razonamiento **explícito** sobre el grafo  
- **Eficiencia** y control del conocimiento  

No optimizar la semilla solo para “sonar más humano que GPT” en una respuesta aislada.

---

## 7. Terminal asíncrona y diálogo proactivo

### 7.0 Implementado en Neurixis (2026-05)

- **Percepción en vivo:** entre palabras de la respuesta, la VM llama a `percibir_teclado` (vía `drenar_teclado_no_bloqueante` en `modulos/terminal_cognitivo.jasb`), acumula **borrador**, hace eco y vuelve a ejecutar `activar_propagacion_contextual` sobre el borrador normalizado, de modo que `pensar_siguiente` sigue viendo el contexto mientras se emite la frase.
- **Emisión palabra a palabra:** `generar_respuesta_con_percepcion_en_vivo` en `modulos/generador.jasb` + flujo en `neurixis.jasb`.
- **Siguiente turno:** `leer_linea_usuario(prefijo)` reutiliza el borrador no enviado con Enter.
- **Límite:** no hay segundo hilo de CPU; el “pensar en paralelo” es **intercalado** (mismo bucle VM). En consola sin TTY o con stdin redirigido, `percibir_teclado` suele devolver vacío. Lo pendiente de §7.1–7.3 (silencios, re-preguntas proactivas, políticas anti-spam) sigue abierto.

### 7.1 Comportamiento deseado (UX)

1. **Detección de actividad del usuario:** mientras el usuario **escribe**, el sistema **no** debe “hablar encima” (pausa o baja prioridad de salida de la IA).  
2. **Ventana de inactividad:** si el usuario deja de escribir y hay **dudas o metas** pendientes, la IA puede **emitir preguntas** (contenido generado desde memoria/plantillas en grafo, **no** lista fija de strings en código).  
3. **Sin respuesta:** tras un tiempo o N intentos, la IA puede **re-enganchar** (p. ej. variaciones de “¿viste mi pregunta?”) siempre **desde generación** + estado de sesión, evitando repetición agresiva o robótica.  
4. **Turn-taking:** reglas claras de quién “tiene la palabra” (usuario vs. IA).

### 7.2 Requisitos técnicos (dependencias)

- **Entrada no solo por línea completa:** en consola clásica suele hacer falta modo **raw** / eventos de teclado, o un **host** (p. ej. Node/Electron/TUI) que envíe eventos `keydown` / buffer parcial a la VM o al scheduler.  
- **Concurrencia o multiplexación:** si la IA “piensa” mientras se escucha teclado, hace falta **otro hilo/proceso** o integración con **cola MAI** ya prevista en VM, más canales seguros de mensajes.  
- **Estado de diálogo:** temporizadores, cooldowns, contador de re-preguntas, cola de “intenciones pendientes” — persistible o solo en sesión según producto.  
- **Políticas anti-intrusión:** límites de frecuencia de mensajes proactivos y tono configurable.

### 7.3 Criterios de aceptación (dirección)

- Se puede demostrar un flujo: usuario escribe → IA en espera → silencio → IA pregunta → usuario responde / ignora → comportamiento distinto según política.  
- Las frases proactivas y de seguimiento provienen de **grafo + composición** o plantillas parametrizadas, no de un bloque monolítico de `imprimir "..."` fijo.

---

## 8. Riesgos y límites conocidos

- Semilla “abstracta” sin cambios en **generador e inferencia** → subutilización del grafo.  
- Muchos tipos de relación sin **semántica en VM** → ruido o equivalencia práctica a “asociación”.  
- Terminal async mal calibrado → sensación de **spam** o interrupciones; hace falta UX y límites explícitos.  
- Concurrencia mal sincronizada → condiciones de carrera con JMN (escritura/consolidación).

---

## 9. Trazabilidad / enlaces útiles en repo

- Semilla actual: `apps/neurixis/datos/semilla_conocimiento.txt`  
- Generación en vivo y terminal: `apps/neurixis/modulos/generador.jasb`, `apps/neurixis/modulos/terminal_cognitivo.jasb`, `apps/neurixis/neurixis.jasb`  
- Entrenamiento / nodos de control: `apps/neurixis/modulos/entrenamiento.jasb`  
- Documentación global del proyecto: `AGENTS.md`, docs de JMN en `sdk-dependiente` según versión en uso.

---

## 10. Próximos pasos sugeridos (cuando se implemente)

1. Decidir **catálogo de tipos** oficial y extensión de formato `.jmn` / IR si aplica.  
2. Implementar **lectura y uso** de nuevos tipos en VM (`buscar_asociados`, `pensar_siguiente`, etc.).  
3. Refactor semilla en **particiones** (conocimiento vs. lenguaje) y migración gradual.  
4. Prototipo de **host con eventos** para terminal o TUI; luego integración con política de diálogo.  
5. Pruebas de estrés: consolidación concurrente, flood de preguntas, sesiones largas.

---

*Última actualización del documento: alineado con conversación de diseño (semilla como ontología; terminal asíncrona y proactividad generada).*
