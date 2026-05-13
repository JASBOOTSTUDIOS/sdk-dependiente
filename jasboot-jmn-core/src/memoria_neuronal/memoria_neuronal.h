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

typedef void (*JMNActivacionRastroFn)(void* ud, uint32_t id, float activacion);

typedef struct JMNConflictoResultado {
    uint32_t id_ganador;
    float confianza;
} JMNConflictoResultado;

#define JMN_RELACION_PATRON     2
#define JMN_RELACION_SECUENCIA  3
#define JMN_RELACION_SIMILITUD  4
#define JMN_RELACION_OPOSICION  5
#define JMN_RELACION_MAX        10

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
/** Lista ids de nodos con texto exactamente igual a `literal` (hasta `max_out`); devuelve cantidad escrita. */
int jmn_listar_nodos_por_texto_exacto(JMNMemoria* mem, const char* literal, uint32_t* out, int max_out);
int jmn_contiene_texto(JMNMemoria* mem, uint32_t id_frase, uint32_t id_patron);
int jmn_termina_con(JMNMemoria* mem, uint32_t id_frase, uint32_t id_sufijo);
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
    JMNActivacionRastroFn rastro_fn, int reserved, void* rastro_ud);
void jmn_resolver_conflictos(JMNMemoria* mem, uint32_t origen, uint32_t tipo_rel, float umbral,
    uint16_t prof, JMNBusquedaResultado* resultados, uint16_t n, float w1, float w2, JMNConflictoResultado* out);

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
