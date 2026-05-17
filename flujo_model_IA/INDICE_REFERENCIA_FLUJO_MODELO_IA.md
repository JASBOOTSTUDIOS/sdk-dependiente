# Índice de referencia — `flujo_model_IA/`

Mapa de los documentos del **modelo de flujo IA** sobre JMN (pesos globales por tipo, máscaras, profundidad, fusión teórica). Úsalo como tabla de contenidos; el modelo mental global está en [`docs/LENGUAJE/MODELO_MENTAL_Y_FLUJO_PREDICCION_JMN.md`](../docs/LENGUAJE/MODELO_MENTAL_Y_FLUJO_PREDICCION_JMN.md).

| Archivo | Contenido |
|--------|-----------|
| [`README.md`](README.md) | Tabla por archivo y orden de lectura sugerido |
| [`ORDEN_IMPLEMENTACION_ALGORITMOS.md`](ORDEN_IMPLEMENTACION_ALGORITMOS.md) | Fases, DoD, pruebas y riesgos de implementación |
| [`01_tokenizacion_L.md`](01_tokenizacion_L.md) | Pipeline **L**: tokenización / claves |
| [`02_d_max.md`](02_d_max.md) | **`d_max`**: tope de profundidad en propagación |
| [`03_g_tau.md`](03_g_tau.md) | **`g(τ)`**: peso global por tipo de relación (y estado frente al código) |
| [`04_h_d.md`](04_h_d.md) | **`h(d)`**: atenuación por distancia (`JASBOOT_PROPAGAR_H_*` en VM/JMN) |
| [`05_mask_C_tau.md`](05_mask_C_tau.md) | **`mask(C,τ)`**: tipos activos por contexto (conceptual + API Jasboot) |
| [`06_alpha_phi.md`](06_alpha_phi.md) | **`α_τ`** y **`φ`**: fusión por canal (mayormente **no** implementada como matriz en la propagación escalar actual) |
| [`07_politica_tau_10.md`](07_politica_tau_10.md) | Política **τ=10** (valorativa) |
| [`08_ranking_y_diversidad_MMR.md`](08_ranking_y_diversidad_MMR.md) | **MMR**: Diversificación de respuestas |
| [`09_d_max_dinamico.md`](09_d_max_dinamico.md) | **d_max(C)**: Profundidad adaptativa |
| [`10_inferencia_simbolica_MIL.md`](10_inferencia_simbolica_MIL.md) | **MIL Multi-paso**: Razonamiento lógico encadenado |
| [`11_metacognicion_y_simulacion.md`](11_metacognicion_y_simulacion.md) | **Metacognición**: Simulación interna "pensar antes de hablar" |
| [`12_memoria_episodica_temporal.md`](12_memoria_episodica_temporal.md) | **Memoria Episódica**: Línea de tiempo y eventos |

## Implementación en el monorepo (enlaces rápidos)

| Tema | Ubicación principal |
|------|---------------------|
| VM: `OP_MEM_PROPAGAR_ACTIVACION`, empaquetado `C`, auditoría | `sdk-dependiente/jasboot-ir/src/vm.c` |
| VM: tabla **`g_extra`** (`g_tau`, `mask_tau`) | `vm.c` (`vm_propagar_pack_extra`, opcodes `OP_MEM_CONFIGURAR_*_G`) |
| JMN: factor **`g·mask`**, perfiles, normalización | `sdk-dependiente/jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c` (`JMNPropagarExtra`, `jmn_propagar_factor_arista`) |
| Compilador: builtins `configurar_peso_g`, `cargar_perfil_g`, etc. | `sdk-dependiente/jas-compiler-c/src/codegen.c` |
| **Flags IR**: deben coincidir compilador ↔ VM | `jas-compiler-c/include/opcodes.h` y `jasboot-ir/src/ir_format.h` |
| Prueba de estrés **≥400** casos (g, máscaras, perfiles) | `tests/test_ia_g_tau_stress_400.jasb`, `tests/run_ia_g_tau_stress_400.cjs` |
| **Neurixis** (pipeline **g(τ)** + **mask** mínimo en app) | `apps/neurixis/neurixis.jasb`, `apps/neurixis/modulos/generador.jasb` (`neurixis_pipeline_g_tau_ara`, `neurixis_aplicar_mask_contexto`) |
| **Plantilla semilla τ=1..30** | `apps/neurixis/datos/semilla_plantilla_tipos_1_30.txt` |
| **Guía semilla por tipo JMN** | `apps/neurixis/docs/GUIA_SEMILLA_POR_TIPO_JMN.md` |
| Propagación **`d_max` / `K` / auditoría** | `docs/LENGUAJE/jmn/PROPAGACION_D_MAX_K_AUDITORIA_Y_ESTRES.md` |
