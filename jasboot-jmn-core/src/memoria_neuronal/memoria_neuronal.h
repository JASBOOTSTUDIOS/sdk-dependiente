/**
 * JMN - Memoria Neuronal Jasboot
 * Persistencia cerebro.jmn (v1: núcleo; v2: +listas/mapas en archivo).
 * Formato: docs/TECNICO/FORMATO_JMN.md
 */
#ifndef MEMORIA_NEURONAL_H
#define MEMORIA_NEURONAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JMNMemoria JMNMemoria;
typedef struct JMNNodo JMNNodo;
typedef struct JMNConexion JMNConexion;

/* Valores en listas/mapas: sin etiqueta de tipo en tiempo de ejecución; lista<T> y comprobaciones
 * de agregado se aplican en el compilador. Coherencia u vs f es responsabilidad del programa. */
typedef union JMNValor {
    uint32_t u;
    float f;
} JMNValor;

struct JMNNodo {
    uint32_t id;
    JMNValor peso;
};

struct JMNConexion {
    uint32_t destino_id;
    uint32_t key_id;  /* tipo de relación */
    JMNValor fuerza;
};

typedef struct JMNBusquedaResultado {
    uint32_t id;
    uint32_t tipo_relacion;
    float fuerza;
} JMNBusquedaResultado;

typedef struct JMNActivacionResultado {
    uint32_t id;
    float activacion;
} JMNActivacionResultado;

/** Diversifica una lista de candidatos usando el algoritmo Maximal Marginal Relevance (MMR).
 *  @param lambda Factor de balance [0, 1]. 1.0 = pura relevancia, 0.0 = pura diversidad.
 *  @param K Numero de candidatos finales a devolver.
 *  @return El numero de candidatos finales en out. */
int jmn_diversificar_candidatos_mmr(JMNMemoria* mem, JMNActivacionResultado* in, int n_in,
    float lambda, int K, JMNActivacionResultado* out);

/** Calcula la similitud del coseno de adyacencia entre dos nodos basándose en sus vecinos comunes. */
float jmn_similitud_coseno(JMNMemoria* mem, uint32_t id1, uint32_t id2);

typedef void (*JMNActivacionRastroFn)(void* ud, uint32_t id, float activacion, uint16_t depth);

typedef struct JMNConflictoResultado {
    uint32_t id_ganador;
    float confianza;
} JMNConflictoResultado;

typedef struct JMNInferenciaResultado {
    uint32_t id_conclusion;
    float confianza;
    uint32_t path[8]; /* Camino de la deducción (máx 8 saltos) */
    int path_len;
} JMNInferenciaResultado;

#define JMN_RELACION_ASOCIACION 1
#define JMN_RELACION_PATRON     2
#define JMN_RELACION_SECUENCIA  3
#define JMN_RELACION_SIMILITUD  4
#define JMN_RELACION_OPOSICION  5
#define JMN_RELACION_PERTENENCIA   6
#define JMN_RELACION_CAUSALIDAD    7
#define JMN_RELACION_TEMPORALIDAD  8
#define JMN_RELACION_INTENCION     9
#define JMN_RELACION_VALORATIVA    10
#define JMN_RELACION_UBICACION     11
#define JMN_RELACION_PROPIEDAD     12
#define JMN_RELACION_PARTE_DE      13
#define JMN_RELACION_CONSECUENCIA  14
#define JMN_RELACION_CONDICION     15
#define JMN_RELACION_INSTANCIA      16
#define JMN_RELACION_POSESION       17
#define JMN_RELACION_FUNCIONALIDAD  18
#define JMN_RELACION_EVIDENCIA      19
#define JMN_RELACION_CUANTIFICACION  20
#define JMN_RELACION_MEDIDA          21
#define JMN_RELACION_OPERADOR        22
#define JMN_RELACION_MAGNITUD        23
#define JMN_RELACION_FRECUENCIA     24
#define JMN_RELACION_PARENTESCO     25
#define JMN_RELACION_CALIFICACION    26
#define JMN_RELACION_ACCION          27
#define JMN_RELACION_COMPLEMENTO     28
#define JMN_RELACION_SITUACION       29
#define JMN_RELACION_REFERENCIA      30
#define JMN_RELACION_MAX           30

/** Opciones extendidas de propagación (BFS/DFS, acumulación, g(τ)). Si se pasa NULL a
 *  `jmn_propagar_activacion_semillas`, se usan solo variables de entorno (ver AGENTS.md). */
typedef struct JMNPropagarExtra {
    int queue_mode; /* 0 = BFS (cola), 1 = DFS (pila LIFO) */
    int score_mode; /* 0 = mejor aportación por profundidad (legacy), 1 = suma de na en aristas exploradas */
    float g_tau[JMN_RELACION_MAX + 1]; /* multiplicador por tipo de relación τ (índice 0 = defecto si τ fuera de rango) */
    float mask_tau[JMN_RELACION_MAX + 1]; /* Máscara contextual mask(C, τ) en [0,1]. Por defecto 1.0 */
    float alpha_tau[JMN_RELACION_MAX + 1]; /* Fase 6: Pesos de fusión α_τ. Por defecto 1.0 */
    uint32_t inhibition_map_id; /* Fase 7/8: ID de un mapa JMN que contiene penalizaciones (nodo_id -> float) */
    float tau10_reject_threshold;  /* Fase 7: Umbral rojo Π para τ=10. Bloquea si E[v,10] < este valor. Por defecto -1e9 (off). */
    float tau10_rewrite_threshold; /* Fase 7: Umbral verde Π para τ=10. Marca para reescritura si E[v,10] < este valor. */
    float mmr_lambda; /* Fase 8: Factor lambda para diversidad MMR [0,1]. 1.0 = deshabilitado. */
    int mmr_k;        /* Fase 8: Numero maximo de candidatos diversificados a devolver. */
    
    /* Fase 9: d_max Dinámico */
    int dmax_dinamico_activado; /* 1 si la VM debe calcular d_max dinámicamente */
    int dmax_base;              /* Profundidad mínima (d_base) */
    float dmax_alpha;           /* Peso para complejidad de entrada (X) */
    float dmax_beta;            /* Peso para modo de contexto (g(C)) */
    float dmax_gamma;           /* Peso para ahorro de energía (E) */

    int h_mode;    /* 0=lineal, 1=exponencial, 2=sigmoide, 3=paso_unico */
    float h_lambda; /* Factor de decaimiento (0.1 - 1.0) */
    float h_kappa;  /* Factor de saturación/forma */
    int audit_mode; /* 0=off, 1=resumen, 2=detallado (por arista) */
} JMNPropagarExtra;

/** Inicializa extra con valores por defecto (1.0). */
void jmn_propagar_extra_init(JMNPropagarExtra* extra);
/** Normaliza g_tau: por defecto escala para que max_{τ=1..30} g(τ)=1 (si hay valores > 0).
 *  Si `JASBOOT_PROPAGAR_G_NORM` empieza por `s` o `S`, divide cada g(τ)>0 por la suma de los g positivos (Σ=1). */
void jmn_propagar_extra_normalizar(JMNPropagarExtra* extra);
/** Carga un perfil predefinido de pesos g(τ) por nombre (ej: "explorador", "analitico", "secuencial"). */
int jmn_propagar_extra_cargar_perfil(JMNPropagarExtra* extra, const char* perfil_nombre);
/** Combina dest con src (los valores de src sobreescriben dest si son distintos de 0 o 1 según lógica). */
void jmn_propagar_extra_merge(JMNPropagarExtra* dest, const JMNPropagarExtra* src);
/** construir_g(defecto, override): copia `g_default` (o init si NULL) y fusiona `p_override` si no es NULL. */
void jmn_propagar_extra_construir(const JMNPropagarExtra* g_default, const JMNPropagarExtra* p_override, JMNPropagarExtra* out);
/** factor_arista(g, mask, τ) = g(τ)·mask(C,τ); τ en 1..30 (fuera de rango usa índice 0 como respaldo). */
float jmn_propagar_factor_arista(const JMNPropagarExtra* ex, uint32_t tau);

/* Apertura/cierre y persistencia */
JMNMemoria* jmn_abrir_escritura(const char* ruta);
/** Igual que jmn_abrir_escritura pero reserva cap_nodos/cap_conexiones (p. ej. crear_memoria con capacidades). */
JMNMemoria* jmn_abrir_escritura_cap(const char* ruta, uint32_t cap_nodos, uint32_t cap_conexiones);
JMNMemoria* jmn_abrir_lectura(const char* ruta);
JMNMemoria* jmn_crear(const char* ruta);
void jmn_finalizar_escritura(JMNMemoria* mem);
/** Si `JASBOOT_JMN_NO_FINAL_SAVE` no empieza por `0`, `jmn_cerrar` no llama a `jmn_finalizar_escritura` (tests / evitar pisar .jmn corrupto). */
void jmn_cerrar(JMNMemoria* mem);

/* Memoria RAM (sin persistencia, para colecciones) */
JMNMemoria* jmn_crear_memoria_ram(uint32_t cap_nodos, uint32_t cap_conexiones);

/* Utilidades */
uint32_t jmn_estructura_id_texto(const char* texto);
uint32_t jmn_relacion_con_contexto(uint32_t tipo, uint32_t contexto);

/* Nodos */
void jmn_agregar_nodo(JMNMemoria* mem, uint32_t id, JMNValor peso);
JMNNodo* jmn_obtener_nodo(JMNMemoria* mem, uint32_t id);
void jmn_aprender_nodo(JMNMemoria* mem, uint32_t id, JMNValor peso);
int jmn_iterar_nodos(JMNMemoria* mem, int (*cb)(JMNNodo*, void*), void* user);

/* Conexiones */
void jmn_agregar_conexion(JMNMemoria* mem, uint32_t origen, uint32_t dest, JMNValor fuerza, uint32_t tipo);
JMNConexion* jmn_obtener_conexiones(JMNMemoria* mem, JMNNodo* nodo, uint32_t* count);
void jmn_penalizar_asociacion(JMNMemoria* mem, uint32_t id_a, uint32_t id_b, float delta);
float jmn_obtener_fuerza_asociacion(JMNMemoria* mem, uint32_t id1, uint32_t id2);
void jmn_reforzar_concepto(JMNMemoria* mem, uint32_t id, float delta);
void jmn_penalizar_concepto(JMNMemoria* mem, uint32_t id, float delta);
/* Decaimiento: factor en [0,1] por pasada (multiplica por 1-factor); pasadas >= 1; umbral: fuerzas < umbral → 0 */
void jmn_decaer_conexiones_global(JMNMemoria* mem, float factor, int pasadas, float umbral);
uint32_t jmn_olvidar_conexiones_debiles(JMNMemoria* mem, float umbral);
void jmn_consolidar_conexiones_supervivientes(JMNMemoria* mem, float boost_relativo);
void jmn_consolidar_memoria_sueno(JMNMemoria* mem, float factor_decay, int pasadas,
                                  float umbral_olvido, float boost_relativo);

/* Texto */
int jmn_guardar_texto(JMNMemoria* mem, uint32_t id, const char* texto);
int jmn_obtener_texto(JMNMemoria* mem, uint32_t id, char* buffer, size_t max_len);
int jmn_existe_texto(JMNMemoria* mem, uint32_t id);
/** Lista ids de nodos con texto exactamente igual a `literal` (hasta `max_out`); devuelve cantidad escrita. */
int jmn_listar_nodos_por_texto_exacto(JMNMemoria* mem, const char* literal, uint32_t* out, int max_out);
int jmn_contiene_texto(JMNMemoria* mem, uint32_t id_frase, uint32_t id_patron);
int jmn_termina_con(JMNMemoria* mem, uint32_t id_frase, uint32_t id_sufijo);
int jmn_establecer_contexto(JMNMemoria* mem, uint32_t id);
int jmn_activar_modulo(JMNMemoria* mem, uint32_t id);
void jmn_copiar_texto(JMNMemoria* mem, uint32_t id_origen, uint32_t id_destino);
uint32_t jmn_ultima_palabra(JMNMemoria* mem, uint32_t id_frase, uint32_t id_destino);
uint32_t jmn_ultima_silaba(JMNMemoria* mem, uint32_t id_frase, uint32_t id_destino);
void jmn_extraer_antes_de(JMNMemoria* mem, uint32_t id_f, uint32_t id_p, uint32_t id_d);
void jmn_extraer_despues_de(JMNMemoria* mem, uint32_t id_f, uint32_t id_p, uint32_t id_d);
void jmn_concatenar_texto(JMNMemoria* mem, uint32_t id_izq, uint32_t id_der, uint32_t id_dest);
uint32_t jmn_concatenar_dinamico(JMNMemoria* mem, uint32_t id_izq, uint32_t id_der);
uint32_t jmn_registrar_texto_dinamico(JMNMemoria* mem, const char* texto);
int jmn_imprimir_texto(JMNMemoria* mem, uint32_t id);
int jmn_leer_archivo(JMNMemoria* mem, const char* ruta, uint32_t id_destino);
int jmn_escribir_archivo(JMNMemoria* mem, const char* ruta, uint32_t id_origen);

/** Sincroniza los cambios en memoria mapeada al disco */
void jmn_sincronizar_disco(JMNMemoria* mem);

/** Cierra el stream append del .jwl si estaba abierto (llamar al liberar memoria). */
void jmn_journal_release(JMNMemoria* mem);

/** Journal append-only (.jwl) — paso hacia 2.2 del plan (integridad incremental). */
void jmn_journal_op_nodo(JMNMemoria* mem, uint32_t id, uint32_t peso_u);
void jmn_journal_op_conex(JMNMemoria* mem, uint32_t ori, uint32_t dest, uint32_t tipo, uint32_t fuerza_u);
/** Op 3 en .jwl: texto asociado a id (payload acotado al mismo límite que slots JMN). */
void jmn_journal_op_texto(JMNMemoria* mem, uint32_t id, const char* texto);
void jmn_journal_commit(JMNMemoria* mem);
/** Trunca `ruta.jwl` a vacío (tras guardar .jmn con `JASBOOT_JWL_CHECKPOINT` activo). */
void jmn_journal_truncate_desde_checkpoint(JMNMemoria* mem);
/** Si existe `JASBOOT_JWL_STAT`, imprime tamaño del .jwl (apertura / diagnóstico). */
void jmn_journal_log_size_if_any(const JMNMemoria* mem);
/**
 * Reproduce `ruta.jwl` sobre mem (sin re-append al journal).
 * Si `JASBOOT_JWL_REPLAY` está definido y el primer carácter no es '0', `jmn_abrir_escritura_cap`
 * invoca esto cuando **no existe** el archivo .jmn principal (p. ej. borrado tras crash antes del guardado),
 * o cuando el .jmn **existe pero no se puede cargar** (truncado, magic/version inválidos, checksum erróneo):
 * en ese caso se vacía la memoria en RAM y se intenta reconstruir desde `.jwl`.
 * Opcional: `JASBOOT_JWL_REPLAY_DBG` distinto de `0` imprime diagnóstico en stderr (fopen / fin / error).
 * Tras un **guardado exitoso** del .jmn, si `JASBOOT_JWL_CHECKPOINT` está definido y no empieza por `0`,
 * se trunca el `.jwl` (checkpoint: el snapshot .jmn es la fuente de verdad hasta la próxima sesión).
 * Formato .jwl: op 1=nodo, 2=conexión, 3=texto (id + len + payload + t), 0xFF=commit.
 * @return 0 éxito o sin .jwl; -1 formato truncado u operación desconocida.
 */
int jmn_journal_replay(JMNMemoria* mem);

/* Listas y mapas (colecciones) */
void jmn_crear_lista(JMNMemoria* mem, uint32_t id);
void jmn_lista_agregar(JMNMemoria* mem, uint32_t id, JMNValor val);
JMNValor jmn_lista_obtener(JMNMemoria* mem, uint32_t id, uint32_t idx);
/** 1 si la lista existe y idx no está en [0, count); 0 si id inválido o lista inexistente (sin cambiar semántica de “handle suelto”). */
int jmn_lista_indice_fuera_de_rango(JMNMemoria* mem, uint32_t id, uint32_t idx);
uint32_t jmn_lista_tamano(JMNMemoria* mem, uint32_t id);
int jmn_lista_existe(JMNMemoria* mem, uint32_t id);
void jmn_lista_poner(JMNMemoria* mem, uint32_t id, uint32_t idx, JMNValor val);
void jmn_lista_unir(JMNMemoria* mem, uint32_t id_izq, uint32_t id_der, uint32_t id_dest);
void jmn_vector_limpiar(JMNMemoria* mem, uint32_t id);
/* Libera el slot JMN de la lista (buffer + entrada en tabla); el id deja de existir. Ver `mem_lista_liberar` en el lenguaje. */
void jmn_lista_liberar(JMNMemoria* mem, uint32_t id);

void jmn_crear_mapa(JMNMemoria* mem, uint32_t map_id);
/** Numero de pares clave-valor almacenados en el slot del mapa (0 si el mapa no tiene buffer). */
uint32_t jmn_mapa_tamano(JMNMemoria* mem, uint32_t map_id);
void jmn_mapa_insertar(JMNMemoria* mem, uint32_t map_id, uint32_t key, JMNValor val);
/** Elimina la entrada con la clave dada si existe (reduce tamano). Sin efecto si la clave no esta. */
void jmn_mapa_eliminar(JMNMemoria* mem, uint32_t map_id, uint32_t key);
/** 1 si la clave existe en el slot del mapa; en *out el valor (si out no es NULL). */
int jmn_mapa_obtener_si_existe(JMNMemoria* mem, uint32_t map_id, uint32_t key, JMNValor* out);
JMNValor jmn_mapa_obtener(JMNMemoria* mem, uint32_t map_id, uint32_t key);
uint32_t jmn_mapa_obtener_llave(JMNMemoria* mem, uint32_t map_id, uint32_t idx);
JMNValor jmn_mapa_obtener_valor_por_indice(JMNMemoria* mem, uint32_t map_id, uint32_t idx);
int jmn_mapa_existe(JMNMemoria* mem, uint32_t map_id);

/* Búsqueda e inferencia */
int jmn_buscar_asociaciones(JMNMemoria* mem, uint32_t origen, uint32_t tipo_rel, float umbral,
    uint16_t profundidad, JMNBusquedaResultado* out, uint16_t max_out);
int jmn_propagar_activacion(JMNMemoria* mem, uint32_t origen, float activacion, float factor,
    float umbral, uint16_t prof, uint32_t tipo_rel, JMNActivacionResultado* out, uint16_t max_out,
    JMNActivacionRastroFn rastro_fn, void* rastro_ud);
/** Varias semillas (misma activacion inicial cada una). Excluye semillas del ranking final.
 *  Ver `JASBOOT_PROPAGAR_H_*` en documentacion: atenuacion h(d) por distancia del destino.
 *  `extra` puede ser NULL (entonces queue/score/g solo por entorno JASBOOT_PROPAGAR_*). */
int jmn_propagar_activacion_semillas(JMNMemoria* mem, const uint32_t* semillas, int n_sem,
    float activacion, float factor, float umbral, uint16_t prof, uint32_t tipo_rel,
    JMNActivacionResultado* out, uint16_t max_out, JMNActivacionRastroFn rastro_fn, void* rastro_ud,
    const JMNPropagarExtra* extra);
void jmn_resolver_conflictos(JMNMemoria* mem, uint32_t origen, uint32_t tipo_rel, float umbral,
    uint16_t prof, JMNBusquedaResultado* resultados, uint16_t n, float w1, float w2, JMNConflictoResultado* out);

/** Fase 10: Inferencia Simbólica MIL.
 *  Busca deducciones lógicas (transitividad) con filtrado de contradicción. */
int jmn_inferir_relaciones_mil(JMNMemoria* mem, uint32_t origen, uint16_t d_max, 
    const float* factor_delta, JMNInferenciaResultado* out, int max_out);

/* Cognitivas (stubs en cognitive_stubs.c) */
int jmn_procesar_texto(JMNMemoria* mem, uint32_t id_origen);
uint32_t jmn_pensar_respuesta(JMNMemoria* mem, uint32_t id);
uint32_t jmn_razonamiento_multipath(JMNMemoria* mem, uint32_t id, int profundidad);
int jmn_asociar_secuencia(JMNMemoria* mem, uint32_t id_sec, uint32_t id_lista);
uint32_t jmn_pensar_siguiente(JMNMemoria* mem, uint32_t id);
uint32_t jmn_pensar_anterior(JMNMemoria* mem, uint32_t id);
int jmn_marcar_estado(JMNMemoria* mem, uint32_t id_nombre, float valor);
int jmn_observar(JMNMemoria* mem, uint32_t id);
uint32_t jmn_registrar_patron(JMNMemoria* mem, uint32_t id_lista);
int jmn_corregir_secuencia(JMNMemoria* mem, uint32_t id_lista);
int jmn_asociar_relacion(JMNMemoria* mem, uint32_t id_a, uint32_t id_b, uint32_t tipo);
void jmn_asociar_relacion_efimera(JMNMemoria* mem, uint32_t id_a, uint32_t id_b, uint32_t tipo, float fuerza);
void jmn_limpiar_conexiones_efimeras(JMNMemoria* mem);
float jmn_evaluar_metacognicion(JMNMemoria* mem, const uint32_t* nodos, int num_nodos, const float* pesos_objetivo);
float jmn_comparar_patrones(JMNMemoria* mem, uint32_t id_a, uint32_t id_b);
uint32_t jmn_obtener_relacionados(JMNMemoria* mem, uint32_t id);
int jmn_extraer_caracter(JMNMemoria* mem, uint32_t id_frase, int32_t indice, uint32_t id_destino);

/* Búsqueda introspectiva en todos los conceptos de la JMN */
typedef struct JMNBusquedaIntrospectivaResultado {
    uint32_t id;           /* ID del concepto que contiene el texto */
    char texto[256];       /* Texto completo del concepto */
    int posicion;          /* Posición donde se encontró la coincidencia (-1 si no se encontró) */
} JMNBusquedaIntrospectivaResultado;

/* Estructura detallada con metadata adicional */
typedef struct JMNBusquedaDetalladaResultado {
    uint32_t id;           /* ID del concepto que contiene el texto */
    char texto[256];       /* Texto completo del concepto */
    int posicion;          /* Posición donde se encontró la coincidencia */
    int longitud_match;    /* Longitud de la coincidencia encontrada */
    int es_clave;          /* 1 si se encontró en la clave, 0 si en el valor */
    float relevancia;      /* Score de relevancia (0.0-1.0) */
} JMNBusquedaDetalladaResultado;

/* Búsqueda básica (case insensitive, primer resultado) */
int jmn_buscar_introspectiva(JMNMemoria* mem, const char* termino, 
                           JMNBusquedaIntrospectivaResultado* resultados, 
                           uint32_t max_resultados, int case_sensitive);

/* Búsqueda con lista de IDs (solo IDs, sin metadata) */
int jmn_buscar_introspectiva_lista(JMNMemoria* mem, const char* termino,
                                   uint32_t* ids, uint32_t max_resultados,
                                   int case_sensitive);

/* Búsqueda con control de case sensitive (primer resultado) */
int jmn_buscar_introspectiva_cs(JMNMemoria* mem, const char* termino,
                                JMNBusquedaIntrospectivaResultado* resultado,
                                int case_sensitive);

/* Búsqueda detallada con metadata completa */
int jmn_buscar_introspectiva_detallada(JMNMemoria* mem, const char* termino,
                                       JMNBusquedaDetalladaResultado* resultados,
                                       uint32_t max_resultados,
                                       int case_sensitive);

/* Funciones auxiliares */
int jmn_buscar_conceptos_con_texto(JMNMemoria* mem, const char* termino, 
                                  uint32_t* ids, uint32_t max_ids, int case_sensitive);
int jmn_concepto_contiene_texto(JMNMemoria* mem, uint32_t id_concepto, 
                               const char* termino, int case_sensitive);

#ifdef __cplusplus
}
#endif

#endif
