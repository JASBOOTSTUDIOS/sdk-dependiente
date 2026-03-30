/**
 * n_grafo_shard.h - Sharding por hash de sujeto (Fase D)
 * particion = hash(sujeto) % num_shards
 */
#ifndef N_GRAFO_SHARD_H
#define N_GRAFO_SHARD_H

#include "n_grafo.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_MAX_SHARDS 256

typedef struct NGrafoShard NGrafoShard;

struct NGrafoShard {
    NGrafo* shards[NGF_MAX_SHARDS];
    char base_ruta[512];
    size_t num_shards;
};

/**
 * Crea grafo shardeado. Cada shard = directorio/shard_N.ngf
 * @param base_ruta: prefijo (ej. "data/grafo" -> data/grafo_0.ngf, data/grafo_1.ngf...)
 * @param num_shards: número de particiones
 */
NGrafoShard* n_grafo_shard_crear(const char* base_ruta, size_t num_shards);

void n_grafo_shard_cerrar(NGrafoShard* gs);

int n_grafo_shard_recordar(NGrafoShard* gs, const char* s, const char* p, const char* o);

uint32_t n_grafo_shard_buscar_objeto(NGrafoShard* gs, const char* sujeto, const char* predicado);

/**
 * Consulta en todos los shards (map) y devuelve primer resultado (reduce any).
 * Para consultas que no conocen el shard.
 */
uint32_t n_grafo_shard_buscar_objeto_global(NGrafoShard* gs, const char* sujeto, const char* predicado);

int n_grafo_shard_obtener_texto(NGrafoShard* gs, const char* sujeto, uint32_t id, char* buf, size_t buf_size);

const NGrafo* n_grafo_shard_obtener(NGrafoShard* gs, size_t idx);

size_t n_grafo_shard_tamano(NGrafoShard* gs, size_t* conceptos_out);

size_t n_grafo_shard_hash_sujeto(const char* sujeto);
size_t n_grafo_shard_indice(NGrafoShard* gs, const char* sujeto);

/**
 * n_grafo_shard_heredar - n_heredar sobre shards; consulta todos y hace merge.
 * Búsqueda transitiva vía n_es_un sobre todas las particiones.
 * @return 1 si encontrado (buf con objeto), 0 si no
 */
int n_grafo_shard_heredar(NGrafoShard* gs, const char* sujeto, const char* predicado,
    char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
