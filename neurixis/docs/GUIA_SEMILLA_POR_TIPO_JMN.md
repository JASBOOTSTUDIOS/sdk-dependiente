# Guía: semilla Neurixis por tipo JMN (1–30)

Neurixis carga aristas desde `datos/semilla_conocimiento.txt` con el formato:

`TIPO | CONCEPTO_A | CONCEPTO_B | PESO`

Los tipos coinciden con el catálogo oficial en [`docs/LENGUAJE/TIPOS_RELACION_JMN.md`](../../../docs/LENGUAJE/TIPOS_RELACION_JMN.md). La plantilla [`datos/semilla_plantilla_tipos_1_30.txt`](../datos/semilla_plantilla_tipos_1_30.txt) incluye **varias filas por τ**, tabla **`TABLA_TAU_PMIN_PMAX`** y anclas **A/B/C/D** de variación dentro de cada banda (copia y adapta; no es un programa Jasboot ejecutable).

## Principios

1. **No saturar pesos**: reserva `0.95–1.0` para relaciones muy fuertes (taxonomía clara, operadores MIL, unidades). Usa `0.55–0.85` para asociaciones débiles o exploratorias.
2. **Secuencia (3)** para cadena de habla; **asociación (1)** para contexto y sinónimos puntuales; **similitud (4)** y **oposición (5)** cuando el motor deba alternar formulaciones.
3. **Valorativa (10)**: solo donde quieras prioridad ética o de producto; combina con la política Π descrita en [`flujo_model_IA/07_politica_tau_10.md`](../../../flujo_model_IA/07_politica_tau_10.md) (hoy Neurixis no implementa Π post-score; la señal entra vía grafo y propagación).
4. **Ubicación (11)**: en chat sin mapa, el pipeline Neurixis pone por defecto `mask(τ=11)=0` tras normalizar `g` (ver `neurixis_aplicar_mask_contexto` en `modulos/generador.jasb`). Las aristas τ=11 siguen en disco pero no aportan a la propagación hasta que uses modo `con_geo` o `completo`.

## Tabla resumen (τ, rol, rango de peso sugerido)

| τ | Rol breve | Peso sugerido (orientativo) |
|---|-----------|----------------------------|
| 1 | Asociación general | 0.55–0.90 |
| 2 | Patrón reconocido | 0.70–0.95 |
| 3 | Secuencia (habla) | 0.80–0.95 |
| 4 | Similitud | 0.85–0.98 |
| 5 | Oposición | 0.80–1.0 |
| 6 | Taxonomía (es un) | 0.85–1.0 |
| 7 | Causalidad | 0.70–0.95 |
| 8 | Temporalidad | 0.65–0.90 |
| 9 | Intención / objetivo | 0.75–0.95 |
| 10 | Valorativa | 0.60–0.90 |
| 11 | Ubicación | 0.80–1.0 (puede quedar anulada por máscara) |
| 12 | Atributo | 0.60–0.90 |
| 13 | Parte–todo | 0.85–1.0 |
| 14 | Consecuencia lógica | 0.70–0.90 |
| 15 | Condición / prerequisito | 0.80–1.0 |
| 16 | Instancia | 0.80–1.0 |
| 17 | Posesión | 0.65–0.90 |
| 18 | Funcionalidad / uso | 0.75–0.95 |
| 19 | Evidencia / fuente | 0.40–0.90 |
| 20 | Magnitud / comparación | 0.70–0.90 |
| 21 | Frecuencia / probabilidad | 0.65–0.88 |
| 22 | Parentesco / social | 0.70–0.95 |
| 23 | Calificación (compuesto) | 0.85–1.0 |
| 24 | Acción (sujeto–verbo) | 0.75–0.95 |
| 25 | Complemento (objeto) | 0.75–0.95 |
| 26 | Cuantificación | 0.85–1.0 |
| 27 | Unidad de medida | 0.85–1.0 |
| 28 | Operador MIL | 0.90–1.0 |
| 29 | Situación / marco | 0.70–0.90 |
| 30 | Referencia lógica | 0.65–0.90 |

## Pipeline ARA en Neurixis

Orden por turno (resumen): `neurixis_pipeline_g_tau_ara()` → `cargar_perfil_g`, ajustes `configurar_peso_g`, `normalizar_pesos_g`, `neurixis_aplicar_mask_contexto("chat_sin_geo")`, luego `propagar_*`. Detalle teórico: [`flujo_model_IA/ORDEN_IMPLEMENTACION_ALGORITMOS.md`](../../../flujo_model_IA/ORDEN_IMPLEMENTACION_ALGORITMOS.md).
