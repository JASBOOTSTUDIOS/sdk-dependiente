---
title: Ruta de implementación — “Algoritmos reestructurables” sobre JMN/MAI
source: sdk-dependiente/docs/planificacion/chat_con_gpt.md
scope: jasboot + neurixis_IA
---

# 1) Resumen ejecutivo

El enfoque del documento [chat_con_gpt.md](file:///C:/src/jasboot/sdk-dependiente/docs/planificacion/chat_con_gpt.md) propone evolucionar JMN desde “memoria semántica textual” hacia un **núcleo cognitivo unificado** que represente:

- semántica (conceptos y relaciones),
- episodios (eventos con tiempo y contexto),
- procedimientos (secuencias y grafos de decisión),
- y control (energía + atención + estabilidad).

Como ingeniería de software: el enfoque es **coherente** y aprovecha muy bien la base de JMN/MAI (grafo tipado + propagación + rastro + consolidación), pero el riesgo principal es intentar “AGI completa” antes de tener **esquemas**, **límites**, **métricas** y **estabilidad**.

La recomendación práctica es implementarlo como un “sistema operativo cognitivo” con dos capas:

- **Nativo (SDK/VM/JMN/MAI):** primitives rápidas y correctas (persistencia, activación, contexto, contadores, timestamps, máscara por tipos, rastro, journaling).
- **Aplicación (neurixis_IA):** política y orquestación (qué aprender, cuándo, cómo convertir texto→estructura, cómo componer respuesta, cómo validar).

# 2) Qué propone exactamente el enfoque (desglosado)

Del documento, las ideas técnicas accionables son:

## 2.1 Representación unificada (multimodal)
- No “guardar texto”, sino convertir entradas (texto/sensores) a **nodos y relaciones tipadas**.
- Para streams continuos: guardar **eventos** (cambios relevantes), no frames completos.

## 2.2 Cognición procedural (“neuronas como algoritmos”)
- Los “procedimientos” no son funciones hardcodeadas: son **grafos** que se reestructuran.
- Aprender = reforzar rutas/atajos, inhibir otras, y crear nuevas secuencias.

## 2.3 Control de estabilidad
- Sin control aparecen loops, explosión de memoria y contaminación contextual.
- Se proponen mecanismos tipo: inhibición, decaimiento/olvido, consolidación (“sueño”), jerarquías y atención selectiva.

## 2.4 Meta-aprendizaje
- Procedimientos que modifican procedimientos: detectar fallas repetidas, dividir, reorganizar, probar variantes.

# 3) Diferencia con el enfoque que hemos tenido hasta ahora (Neurixis_IA actual)

## 3.1 “Antes” (lo que hoy hace neurixis_IA)
- Pipeline principalmente: **tokenizar → asociar/propagar → seleccionar candidato → renderizar**.
- Aprendizaje centrado en **patrones lingüísticos** (ej. “X es Y”, “X causa Y”) y “triggers” de intención desde JMN.
- Respuestas: mayormente **plantillas** y **recuperación** de relaciones/valores.

Esto es consistente con las capas actuales de JMN/MAI descritas en [CAPAS_JMN_MAI_VM_Y_USO.md](file:///C:/src/jasboot/docs/LENGUAJE/CAPAS_JMN_MAI_VM_Y_USO.md), y con la lógica de generador en app (ejemplo: [generador.jasb](file:///C:/src/jasboot/apps/neurixis_IA/cerebro/generador.jasb)).

## 3.2 “Después” (lo que propone chat_con_gpt.md)
- Pipeline: **entrada → segmentación → extracción estructural → procedimiento/simulación → evaluación → reestructuración**.
- La unidad de aprendizaje no es “un dato”: es un **procedimiento** y su “política de uso”.
- Se agregan explícitamente: **tiempo, confianza, energía, atención** como variables de control.

En corto: el enfoque actual es “memoria asociativa + generación”, el nuevo es “memoria asociativa + ejecución procedural + control + reescritura”.

# 4) Opinión técnica (pros, riesgos, y cómo aterrizarlo)

## 4.1 Pros reales
- **Persistencia incremental:** JMN ya es persistente; el sistema puede mejorar por interacción sin reentrenar.
- **Costo computacional acotado:** propagación sparse + K/d_max controlables; viable en CPU.
- **Interpretabilidad:** rastro de activación y grafos procedurales hacen trazable el “por qué”.

## 4.2 Riesgos reales (ingeniería)
- **Explosión combinatoria:** si cada frase genera nodos/procedimientos sin compresión/poda.
- **Inestabilidad por plasticidad:** el sistema puede “olvidar” rutas útiles o reforzar ruido.
- **Falta de contrato de datos:** sin un “schema” de nodos/relaciones, el grafo se vuelve incontrolable.
- **Evaluación/métricas:** sin pruebas objetivas, el aprendizaje puede parecer “inteligente” pero degradar.

## 4.3 Recomendación de aterrizaje
- Definir primero un **modelo de datos mínimo** (ontología + convenciones de claves) y un **ciclo de aprendizaje** muy conservador.
- Implementar “procedimientos” en una versión mínima: secuencia + condición simple + evaluación básica.
- Recién después: abstracción automática, compresión, simulación y meta-aprendizaje.

# 5) Qué conviene implementar “nativo” (SDK/VM/JMN/MAI) vs “app”

Esta división sigue la guía del repo: cambios transversales/performance a SDK; orquestación/política en app.

## 5.1 Nativo (recomendado)

### A) Metadatos de aristas/nodos (sin depender de texto)
Objetivo: que “confianza, frecuencia, timestamp, proveniencia” no vivan como strings frágiles.

Opciones:
- Extender JMN core para almacenar por arista:
  - `last_seen_ts`
  - `count_seen`
  - `confidence` (o `evidence_score`)
- Extender APIs para leer/escribir estos campos sin roundtrips por texto.

### B) Primitives de tiempo/contexto (episódico)
Objetivo: eventos con tiempo y recuperación por recencia.

Encaje con MAI:
- MAI ya modela “contexto reciente” y prioridades; conviene que exponga helpers para “recencia” y ventanas temporales.

### C) Operaciones rápidas de “matching” sobre secuencias
Objetivo: detectar patrones como secuencias de tokens de forma confiable y rápida.

Recomendación:
- Un builtin/VM helper para “match de subsecuencia” sobre listas de ids (evita problemas de string).
- Soporte de límites de loop y profundidad directamente en VM para procedimientos.

### D) Energía/atención como mecanismo de scheduling
Objetivo: que el motor tenga un presupuesto por turno y un selector estable.

Recomendación:
- Energía como contador nativo por turno (o por sesión), consumido por:
  - propagación,
  - búsqueda multi-tipo,
  - expansión de candidatos,
  - simulación.
- Atención como “top-N” sobre activación + recencia + prioridad (parte puede vivir en MAI).

### E) Instrumentación y auditoría “first-class”
Objetivo: explicar decisiones y depurar sin ensuciar app.

Recomendación:
- Reforzar rastro (ya existe) con:
  - “por qué ganó” (tipo/peso/profundidad),
  - “qué se descartó” (inhibición/loops).

## 5.2 Aplicación (recomendado)

### A) Ontología y esquema de nodos/keys
Objetivo: que el grafo sea mantenible.

Ejemplos de convenciones (orientativas):
- `ent:persona:<id>`
- `evt:<timestamp>:<id>`
- `proc:<nombre>` (procedimiento)
- `step:<proc>:<n>`
- `slot:<evt>:actor|obj|lugar|tiempo`

### B) Extractor texto→estructura (mínimo viable)
Primero: robusto con heurísticas + JMN (sin LLM).
- tokenización y normalización
- patrones de enseñanza (ya hay base)
- entidades simples (personas/objetos por nombre)
- fechas/recencia como tags (“ayer”, “hoy”)

### C) Motor de ejecución procedural (mínimo viable)
Representar procedimientos con:
- secuencias (tipo 3),
- condiciones (tipo 15),
- consecuencias (tipo 14),
- y evaluación (una métrica simple por outcome).

### D) Política de aprendizaje (segura)
Reglas conservadoras:
- aprender sólo cuando la intención lo marque o cuando haya patrón claro,
- cap de nodos/aristas por turno,
- decaimiento y poda controlada,
- proveniencia obligatoria para lo aprendido.

# 6) Ruta de implementación propuesta (por fases)

## Fase 0 — Asegurar base (2–3 iteraciones)
- Estabilizar “respuesta válida” (no IDs/artefactos; no repetir el mismo output).
- Asegurar pruebas mínimas: carga de semilla, no loops, salida no vacía.
- Medir: latencia por turno y tamaño de grafo.

## Fase 1 — Esquema y tipos (core del enfoque)
- Definir “schema” de claves y nodos (en semilla, no hardcode).
- Establecer relaciones mínimas:
  - semántica (definición, causalidad),
  - episodio (evento/turno),
  - procedimiento (proc/step).
- Añadir “proveniencia” (turno/fuente) como parte del dato.

Entregables:
- Semilla con tipos base y parámetros.
- Tests: construir un evento y recuperarlo por recencia.

## Fase 2 — Procedimientos mínimos (sin meta-aprendizaje aún)
- Representar 3–5 habilidades simples como procedimientos:
  - “saludar”, “preguntar por estado”, “pedir aclaración”, “resumir”.
- Ejecutar procedimientos con:
  - selección por intención,
  - `pensar_siguiente_mai` / secuencia,
  - condición básica (si falta dato → preguntar).

Entregables:
- Procedimientos en JMN + motor en app.
- Pruebas: “saludo → follow-up distinto al saludo”.

## Fase 3 — Episódico con tiempo + recencia + confianza
- Guardar eventos por turno (input, output, intent, conceptos activados).
- Recuperación por:
  - recencia,
  - similitud semántica,
  - prioridad.
- Confianza inicial simple: contadores y contradicción básica.

Entregables:
- “Recuerdo que dijiste X” con evidencia (turno/fecha).

## Fase 4 — Atención + energía (control)
- Energía por turno: limitar exploración vs usar hábitos.
- Atención: top-N activaciones con inhibición de ruido.
- Decaimiento + consolidación programada (usa funciones existentes si están).

Entregables:
- Métrica de estabilidad: loops=0, crecimiento controlado, latencia estable.

## Fase 5 — Abstracción/compresión (emergencia)
- Detectar secuencias repetidas y “promoverlas” a un nodo/procedimiento.
- Poda de subestructuras no revalidadas.
- Reescritura: atajos y refuerzo.

Entregables:
- La IA “aprende un hábito” (misma tarea cada vez más directa).

## Fase 6 — Simulación (razonamiento procedural)
- Simular pasos sin ejecutar efectos reales.
- Evaluar outcomes y elegir estrategia.

Entregables:
- Resolver un problema pequeño con 2 estrategias y seleccionar la mejor.

# 7) Checklist de “Definition of Done” por feature

- Configuración y patrones viven en semilla/JMN (sin hardcode en app).
- Hay límites explícitos (`d_max`, `K`, caps de nodos/aristas por turno).
- Existe mecanismo de:
  - inhibición (anti-loop),
  - decaimiento/poda,
  - consolidación (si aplica).
- Pruebas: saludo, consulta básica, enseñanza, fallback seguro.

# 8) Notas finales (alineación con el repo)

Este plan encaja con la separación de capas del repo (JMN/MAI/VM vs app) y con la documentación de propagación y máscaras.

Lecturas complementarias:
- [CAPAS_JMN_MAI_VM_Y_USO.md](file:///C:/src/jasboot/docs/LENGUAJE/CAPAS_JMN_MAI_VM_Y_USO.md)
- [ARQUITECTURA_IGA_GENERATIVA.md](file:///C:/src/jasboot/docs/ARQUITECTURA_IGA_GENERATIVA.md)
- [PLAN_APRENDIZAJE_DINAMICO.md](file:///C:/src/jasboot/apps/neurixis_IA/PLAN_APRENDIZAJE_DINAMICO.md)

