# Flujo modelo IA — diagramas por componente

**Índice maestro (mapa + enlaces al código):** [`INDICE_REFERENCIA_FLUJO_MODELO_IA.md`](INDICE_REFERENCIA_FLUJO_MODELO_IA.md).

Documentación **desglosada** del pipeline de predicción con memoria tipo grafo (JMN / modelo mental descrito en [`docs/LENGUAJE/MODELO_MENTAL_Y_FLUJO_PREDICCION_JMN.md`](../docs/LENGUAJE/MODELO_MENTAL_Y_FLUJO_PREDICCION_JMN.md)).

**Orden de implementación (fases, DoD, pruebas, riesgos):** [`ORDEN_IMPLEMENTACION_ALGORITMOS.md`](ORDEN_IMPLEMENTACION_ALGORITMOS.md).

Cada archivo numérico incluye **algoritmo en pasos**, **pseudocódigo** y **diagramas Mermaid** (flujo interno).

| # | Archivo | Parámetro / bloque |
|---|---------|-------------------|
| 1 | [`01_tokenizacion_L.md`](01_tokenizacion_L.md) | **L** / tokenización |
| 2 | [`02_d_max.md`](02_d_max.md) | **d_max** — límite de profundidad en el grafo |
| 3 | [`03_g_tau.md`](03_g_tau.md) | **g(τ)** — peso por tipo de relación |
| 4 | [`04_h_d.md`](04_h_d.md) | **h(d)** — penalización por distancia |
| 5 | [`05_mask_C_tau.md`](05_mask_C_tau.md) | **mask(C, τ)** — tipos activos según contexto |
| 6 | [`06_alpha_phi.md`](06_alpha_phi.md) | **α_τ** y **φ** — fusión de canales en un score |
| 7 | [`07_politica_tau_10.md`](07_politica_tau_10.md) | **Política τ=10** (valorativa: tono, límites, seguridad) |

Orden sugerido de lectura: 1 → 2 → 4 → 3 → 5 → 6 → 7 (d_max y h(d) se entienden mejor juntos antes de g y mask).

**Neurixis (aplicación de referencia):** pipeline `g` + normalización + máscara mínima por contexto en `apps/neurixis/modulos/generador.jasb`; plantilla de aristas por τ en `apps/neurixis/datos/semilla_plantilla_tipos_1_30.txt` y guía en `apps/neurixis/docs/GUIA_SEMILLA_POR_TIPO_JMN.md`. Tabla de estado por fase: [`ORDEN_IMPLEMENTACION_ALGORITMOS.md`](ORDEN_IMPLEMENTACION_ALGORITMOS.md) (sección final).
