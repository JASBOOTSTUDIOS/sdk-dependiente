# 🤖 NEURIXIS IA (ARA) - CHECKLIST ULTRA-DETALLADO

Este checklist divide la construcción de Neurixis en funciones individuales. Cada check representa una unidad de trabajo mínima y testeable bajo la Arquitectura de Resonancia Asociativa (ARA).

---

## 📚 FASE 0: VOCABULARIO Y GRAFO BASE (JMN)
*Objetivo: Preparar la infraestructura de memoria y el entrenamiento inicial.*

- [ ] **`inicializar_memoria_maestra()`**
    - **Archivo**: `neurixis.jasb`
    - **Detalle**: Crear/abrir `neurixis_core.jmn` y `neurixis_estado.jmn`.
- [ ] **`registrar_palabra_en_grafo(texto palabra)`**
    - **Archivo**: `modulos/entrenamiento.jasb`
    - **Detalle**: Crear un nodo en JMN para una palabra si no existe.
- [ ] **`entrenar_secuencia_basica(texto palabra_a, texto palabra_b, flotante peso)`**
    - **Archivo**: `modulos/entrenamiento.jasb`
    - **Detalle**: Crear relación de **Tipo 2 (Secuencia)** con peso inicial.
- [ ] **`entrenar_asociacion_semantica(texto concepto, texto etiqueta, flotante peso)`**
    - **Archivo**: `modulos/entrenamiento.jasb`
    - **Detalle**: Crear relación de **Tipo 1 (Asociación)** para categorización.
- [ ] **`entrenar_sinonimos(texto palabra_a, texto palabra_b)`**
    - **Archivo**: `modulos/entrenamiento.jasb`
    - **Detalle**: Crear relación de **Tipo 3 (Similitud)** con peso 0.9000.

---

## ⚖️ FASE 0.2: LÓGICA DE PESOS Y CÁLCULO
*Objetivo: Implementar las fórmulas matemáticas de activación ARA.*

- [ ] **`fn_calcular_potencial_activacion(texto nodo_objetivo, lista tokens) retorna flotante`**
    - **Detalle**: Implementar \( \sum (Peso(Token \to Objetivo)) \) con penalización del 20% para Tipo 3.
- [ ] **`fn_seleccionar_mejor_candidato(lista candidatos) retorna texto`**
    - **Detalle**: Devolver nodo con mayor peso + variabilidad de 0.0500.
- [ ] **`fn_validar_coherencia_concepto(texto concepto) retorna bool`**
    - **Detalle**: Filtrar caracteres extraños y ruidos.
- [ ] **`fn_determinar_tipo_relacion(texto a, texto b, mapa contexto) retorna entero`**
    - **Detalle**: Decidir si es Secuencia (2), Asociación (1) o Similitud (3).
- [ ] **`fn_gestionar_ciclo_vida_memoria()`**
    - **Detalle**: Ejecutar `decaer_conexiones` (< 0.3000) y aplicar entropía dinámica.
- [ ] **`fn_calcular_tasa_olvido_dinamica(texto origen_id, texto destino_id) retorna flotante`**
    - **Detalle**: Implementar zonas Roja (0.9990), Verde (0.9999) y Platino (1.0000).
- [ ] **`fn_aplicar_homeostasis_nodo(texto origen_id, entero tipo_rel)`**
    - **Detalle**: Normalización de Suma Cero para conexiones salientes.

---

## 🌱 FASE 0.3: SEMILLA DE CONOCIMIENTO (ATÓMICA)
*Objetivo: Poblar la JMN con el núcleo funcional.*

- [ ] **`fn_cargar_semilla_desde_archivo(texto ruta)`**
    - **Detalle**: Procesar frases base para crear asociaciones iniciales.
- [ ] **`entrenar_nodos_control()`**
    - **Detalle**: Crear `inicio_saludo`, `inicio_pregunta`, `fin_oracion`, `nodo_punto`, `estado_inverso`, etc.
- [ ] **`entrenar_gramatica_base()`**
    - **Detalle**: Secuencias Tipo 2 para conectores, pronombres y etiquetas (`gram_femenino`, etc.).
- [ ] **`entrenar_alfabeto_y_categorias()`**
    - **Detalle**: Crear nodos `char_a` a `char_z`, categorías vocal/consonante y su secuencia de orden.
- [ ] **`entrenar_digitos_atomicos()`**
    - **Detalle**: Crear nodos `digito_0` a `digito_9` y asociar palabras numéricas ("mil") a sus secuencias.

---

## 🛡️ FASE 0.4: FILTROS DE COHERENCIA Y SESGOS
*Objetivo: Capas de control para estabilidad y personalidad.*

- [ ] **`fn_verificar_concordancia(texto candidato, mapa contexto_gramatical) retorna flotante`**
    - **Detalle**: Penalización (0.1000) por discordancia de género/número.
- [ ] **`fn_aplicar_saciedad(texto palabra)`**
    - **Detalle**: Inhibición temporal (-1.0000) para evitar bucles en la respuesta.
- [ ] **`fn_resolver_ambiguedad_lateral(texto palabra_multiple)`**
    - **Detalle**: Inhibición de ramas semánticas irrelevantes según el tema dominante.
- [ ] **`fn_detectar_formato_pedido(lista tokens) retorna texto`**
    - **Detalle**: Activar nodos maestros de formato (`formato_markdown`, etc.).
- [ ] **`fn_procesar_cuantificadores(lista tokens) retorna mapa`**
    - **Detalle**: Vincular números con conceptos y activar `estado_duracion` si aplica.
- [ ] **`fn_mantener_activacion_personalidad()`**
    - **Detalle**: Inyectar energía constante (0.5000) a nodos de rasgo.

---

## 🧹 FASE 1: MÓDULO NORMALIZADOR
*Archivo: `modulos/normalizador.jasb`*

- [ ] **`fn_limpiar_puntuacion(texto entrada) retorna texto`**
- [ ] **`fn_convertir_minusculas(texto entrada) retorna texto`**
- [ ] **`fn_tokenizar(texto entrada) retorna lista`**
- [ ] **`fn_es_palabra_vacia(texto palabra) retorna bool`**
- [ ] **`fn_calcular_distancia_lexica(texto a, texto b) retorna entero`**
    - **Detalle**: Levenshtein para detectar "ols" -> "hola".
- [ ] **`fn_extraer_raiz_concepto(texto palabra) retorna texto`**
    - **Detalle**: Unir variantes ("corriendo") al maestro ("correr").
- [ ] **`fn_convertir_palabras_a_cifras(lista tokens) retorna lista`**
    - **Detalle**: Ensamblar secuencias de dígitos atómicos (ej: "mil" -> "1000").
- [ ] **`normalizar_entrada(texto entrada) retorna lista`**

---

## 🧠 FASE 2: MÓDULO DE PROCESAMIENTO
*Archivo: `modulos/procesamiento.jasb`*

- [ ] **`fn_activar_nodos_entrada(lista tokens)`**
- [ ] **`fn_obtener_puntuacion_intencion(texto intencion, lista tokens) retorna flotante`**
- [ ] **`detectar_intencion_neuronal(lista tokens) retorna mapa`**
- [ ] **`fn_extraer_entidades(lista tokens) retorna lista`**
- [ ] **`fn_mapear_emoji_a_emocion(texto emoji) retorna mapa`**
- [ ] **`fn_detectar_conceptos_desconocidos(lista tokens) retorna lista`**
- [ ] **`fn_inferir_contexto_desconocido(texto palabra, lista tokens)`**
- [ ] **`fn_detectar_operacion_matematica(lista tokens) retorna bool`**
    - **Detalle**: Identificar si se requiere el MIL para el cálculo.

---

## 💬 FASE 3: MÓDULO DE CONTEXTO
*Archivo: `modulos/contexto.jasb`*

- [ ] **`iniciar_buffer_contexto()`**
- [ ] **`agregar_a_memoria_corto_plazo(texto mensaje, texto rol)`**
- [ ] **`fn_extraer_temas_dominantes() retorna lista`**
- [ ] **`fn_actualizar_nodos_estado(mapa analisis) retorna void`**
- [ ] **`fn_volcar_activacion_a_disco()`**
    - **Detalle**: Checkpoint en `neurixis_estado.jmn`.
- [ ] **`fn_restaurar_estado_conciencia()`**
- [ ] **`activar_propagacion_contextual(lista temas)`**

---

## 🎭 FASE 4: MÓDULO GENERADOR (COMPOSICIÓN)
*Archivo: `modulos/generador.jasb`*

- [ ] **`fn_determinar_nodo_inicio(mapa intencion) retorna texto`**
- [ ] **`fn_inyectar_ancla_contextual(texto palabra)`**
- [ ] **`fn_predecir_siguiente_palabra(texto palabra_actual) retorna texto`**
- [ ] **`fn_navegar_secuencia_inversa(texto nodo_inicio) retorna lista`**
    - **Detalle**: Uso de `pensar_anterior` para órdenes inversos.
- [ ] **`fn_ejecutar_puente_logico(texto operacion, lista operandos) retorna texto`**
    - **Detalle**: Ejecución MIL en la VM de Jasboot.
- [ ] **`fn_validar_fin_oracion(texto palabra, entero contador) retorna bool`**
- [ ] **`generar_respuesta_por_secuencia(mapa intencion) retorna texto`**
- [ ] **`fn_post_procesar_puntuacion(texto crudo) retorna texto`**
- [ ] **`fn_generar_variante_numero(texto concepto, entero objetivo_numero) retorna texto`**
- [ ] **`fn_sintetizar_palabra_nueva(texto raiz, texto sufijo) retorna texto`**

---

## ❤️ FASE 5: APRENDIZAJE Y REFORZAMIENTO EMOCIONAL
*Archivo: `modulos/aprendizaje.jasb`*

- [ ] **`fn_detectar_intensidad_interaccion(mapa contexto) retorna flotante`**
- [ ] **`fn_calcular_delta_emocional(flotante valencia, flotante intensidad, flotante peso_actual) retorna flotante`**
- [ ] **`reforzar_camino_emocional(lista palabras_respuesta, flotante intensidad)`**
- [ ] **`penalizar_camino_emocional(lista palabras_respuesta, flotante intensidad)`**
- [ ] **`aprender_nueva_secuencia(lista tokens_usuario)`**
- [ ] **`consolidar_conocimiento_dia()`**

---

## 🚀 FASE 6: FLUJO PRINCIPAL (INTEGRACIÓN)
*Archivo: `neurixis.jasb`*

- [ ] **`bucle_conversacion_infinito()`**
- [ ] **`fn_manejar_errores_neuronales()`**
