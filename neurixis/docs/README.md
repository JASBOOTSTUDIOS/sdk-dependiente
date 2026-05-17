# 🧠 Neurixis IA

Neurixis es una IA conversacional avanzada desarrollada en **Jasboot** que utiliza el sistema de **Memoria Neuronal Asociativa (JMN)** para procesar y generar interacciones humanas reales sin depender de textos pre-escritos (hardcoded).

## 🎯 Visión
A diferencia de los chatbots tradicionales, Neurixis no busca respuestas en una base de datos de texto. En su lugar, utiliza un **grafo de conceptos** donde las palabras y las ideas están conectadas por **pesos y secuencias**. La respuesta se "compone" navegando por el grafo basándose en la activación neuronal y la relevancia contextual.

## 🏗️ Arquitectura
- **Vocabulario Asociativo**: Grafo de palabras conectadas por co-ocurrencia.
- **Detección de Intenciones**: Activación de nodos de intención mediante la suma de pesos de las palabras de entrada.
- **Generación por Composición**: Uso de **relaciones de secuencia (tipo 3)** para construir oraciones palabra por palabra.
- **Propagación de Activación**: Sesgo del pensamiento hacia temas relevantes del contexto actual. **Actualización 2026:** `d_max` y `K` explícitos en Jasboot (sin usar un flotante como segundo argumento), **multi-semilla** con `propagar_activacion_semillas` cuando varios tokens ya tienen aristas en JMN, **ventana de rastro** 2048 y **flujo temporal** 256 al iniciar memoria. El núcleo JMN puede **re-encolar** mejoras de activación a **igual profundidad** (presupuesto acotado) para refinar señales convergentes. **g(τ) + mask(C,τ) — 2026:** tras abrir JMN y antes de cada oleada contextual se ejecuta **`neurixis_pipeline_g_tau_ara()`** (`modulos/generador.jasb`): **paso 1** `cargar_perfil_g` + `configurar_peso_g` finos, **paso 2** `normalizar_pesos_g`, **paso 3** `neurixis_aplicar_mask_contexto("chat_sin_geo")` (por defecto desactiva propagación por **τ=11 ubicación**; modos `con_geo` / `completo` la reactivan), **paso 4** uso implícito en `propagar_*` (ver `flujo_model_IA/03_g_tau.md`, `flujo_model_IA/05_mask_C_tau.md` y § g(τ) en `docs/LENGUAJE/REFERENCIA_LENGUAJE_JASBOOT.md`). **Rendimiento:** ya no se llama `consolidar_memoria()` en cada mensaje (solo al salir), se evita el barrido léxico O(N) por token y se reutilizan los tokens de entrada en el aprendizaje. Referencias: `docs/LENGUAJE/jmn/PROPAGACION_D_MAX_K_AUDITORIA_Y_ESTRES.md`, `flujo_model_IA/02_d_max.md`. **Semilla por tipos:** [`GUIA_SEMILLA_POR_TIPO_JMN.md`](GUIA_SEMILLA_POR_TIPO_JMN.md), [`../datos/semilla_plantilla_tipos_1_30.txt`](../datos/semilla_plantilla_tipos_1_30.txt).
- **Control ejecutivo (inhibición competitiva):** cada turno se llama **`rastro_activacion_limpiar()`** antes de percepción/propagación; **`activar_propagacion_contextual(tokens, tipo_intencion)`** acota **`d_max`** y omite **`propagar_activacion_mai`** en intenciones cerradas (saludo, despedida, identidad, alfabeto, capacidades); la generación usa **`tema_para_expandir`** (p. ej. forzar **`tema_saludo`** en saludos) y **`expandir_respuesta_secuencia_completa(..., usar_fallback_rastro)`** con **`usar_fallback_rastro = 0`** para no mezclar **`candidato_desde_rastro`** con secuencias ajenas; el nodo por defecto pasa a **`inicio_generica`** salvo intención/tema explícitos; **`detectar_intencion_neuronal`** prioriza pregunta larga/`?` frente a saludo vago y refuerza **`intencion_capacidades`** (`que haces`, `memoria`+`que`). Semilla: aristas **`intencion_saludo`** desde hola/buenos/días.
- **Aprendizaje Continuo**: Reforzamiento de pesos y creación de nuevas asociaciones tras cada conversación.
- **Entrada (pipeline L nativo)**: `normalizar_entrada` usa `tokenizar_L` con modo **394243** (NFKC + casefold + colapsar + colapso Unicode **131072** + puntuación como separador **262144**; sin bit So para menor coste por mensaje). Detalle: `sdk-dependiente/docs/TOKENIZAR_L_PIPELINE_NATIVE.md`.

## 📁 Estructura del Proyecto
- `neurixis.jasb`: Flujo principal del sistema.
- `modulos/`: Módulos especializados (Normalizador, Procesamiento, Generador, Contexto).
- `docs/`: Plan de implementación y checklist detallado.
- `memoria/jmn/`: Archivos de persistencia de la memoria neuronal.

## 🚀 Cómo empezar
1. Asegúrate de tener instalado el entorno de Jasboot.
2. Ejecuta el sistema principal:
   ```bash
   node .vscode/run-jasb.cjs apps/neurixis/neurixis.jasb
   ```
