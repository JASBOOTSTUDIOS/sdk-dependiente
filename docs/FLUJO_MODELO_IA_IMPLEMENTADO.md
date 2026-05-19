# Flujo Model IA — Estado de Implementación Nativa (SDK)

Este documento resume, para el **SDK `sdk-dependiente/`**, qué partes del flujo descrito en `c:\src\jasboot\flujo_model_IA\` están **implementadas nativamente** (JMN/VM/runtime) y cuáles quedan en **capa aplicación**.

> “Nativo” aquí significa: existe soporte en C dentro de `jasboot-jmn-core/`, `jasboot-ir/` o el runtime de builtins, y puede ser invocado por bytecode/VM (directo o vía builtins).

## Implementado nativo

### 01 — Tokenización L
- Pipeline de tokenización (VM): [`jasboot-ir/src/vm_tokenizar_l_pipeline.inc`](../jasboot-ir/src/vm_tokenizar_l_pipeline.inc)
- Expuesto en Jasboot como builtins: `tokenizar_L(...)` / `claves_L(...)` (enrutado en VM/runtime).

### 02 — d_max (corte duro de profundidad)
- Implementado en la propagación multi-semilla: [`jmn_propagar_activacion_semillas`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
- Límite de seguridad `d_max ≤ 32` aplicado en el núcleo.

### 03 — g(τ) (peso por tipo)
- Vector `g_tau[1..30]` en `JMNPropagarExtra`, aplicado por arista:
  - Init/normalización/perfiles: [`jmn_propagar_extra_init`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
  - Factor efectivo por arista `g(τ) * mask(C,τ)`: [`jmn_propagar_factor_arista`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
- Ajustable por entorno (`JASBOOT_PROPAGAR_G*`) y por API/builtins (lado VM).

### 04 — h(d) (decaimiento por distancia)
- Tabla `H[0..32]` precomputada y aplicada por profundidad:
  - [`jmn_propagar_precompute_h`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
  - Parámetros `h_mode`, `h_lambda`, `h_kappa` dentro de `JMNPropagarExtra`.

### 05 — mask(C,τ) (máscara por tipo)
- Vector `mask_tau[1..30]` en `JMNPropagarExtra`, multiplicado con `g(τ)`:
  - [`jmn_propagar_factor_arista`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
- Ajustable por entorno (`JASBOOT_PROPAGAR_MASK*`) y por API/builtins (lado VM).

### 06 — α_τ y φ (fusión multi-canal a score escalar)
- La propagación mantiene una matriz acotada `E[v,τ]` y ejecuta fusión final:
  - Evidencia por canal: `E[v,τ]` durante el BFS.
  - Fusión `φ(α·E)` (modo suma vs modo max) al finalizar:
    - [`jmn_propagar_activacion_semillas`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
  - `alpha_tau[1..30]` existe en `JMNPropagarExtra`.

### 07 — Política τ=10 (valorativa) con bloqueo en propagación
- Umbral nativo de bloqueo por evidencia τ=10:
  - Campos: `tau10_reject_threshold` / `tau10_rewrite_threshold` en `JMNPropagarExtra`
  - Bloqueo de candidatos por señal τ=10 dentro de:
    - [`jmn_propagar_activacion_semillas`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)

### 08 — Ranking con diversidad (MMR)
- Post-proceso MMR nativo (si se activa vía `JMNPropagarExtra`):
  - [`jmn_diversificar_candidatos_mmr`](../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal_cognitivo.c)
  - Similaridad coseno (adyacencia): `jmn_similitud_coseno(...)` (misma unidad C).

## Parcial / “hay campos, falta orquestación completa”

### 09 — d_max dinámico
- Existen campos de tuning en `JMNPropagarExtra` (`dmax_*` y flag `dmax_dinamico_activado`), pero no hay una ruta completa nativa que calcule `d_max(X,C,E)` automáticamente dentro de `jmn_propagar_activacion_semillas` (aún).

## Capa aplicación (no hay motor genérico nativo)

### 10 — Inferencia simbólica MIL multi-paso
- No hay un motor MIL genérico en el SDK: se implementa como lógica en Jasboot (aplicación) usando consultas `buscar_asociados_lista`, `buscar_peso` y creación de relaciones efímeras cuando aplique.

### 11 — Metacognición y simulación
- No existe un “crítico” nativo completo como fase única. Se construye en app combinando:
  - propagación + política + evaluación (según diseño de producto),
  - memoria de trabajo (para sandbox) y reintentos.

### 12 — Memoria episódica temporal
- No hay sistema nativo automático de “episodios con recencia/olvido/homeostasis” como capa lista para usar; se implementa en app como estructura de nodos/aristas y, si se requiere, mantenimiento/poda explícita.

