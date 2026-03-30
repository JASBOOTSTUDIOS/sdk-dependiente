#ifndef N_GRAFO_SHARDING_H
#define N_GRAFO_SHARDING_H

#include "n_grafo.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_SHARD_MAX 256
#define NGF_SHARD_BY_SUJETO 0
#define NGF_SHARD_BY_DOMINIO 1

typedef struct NGrafoSharding NGrafoSharding;

/**
 * Crea cluster de shards por hash.
 * rutas: array de rutas .ngf (shard 0..n-1)
 * n: numero de shards
 * modo: NGF_SHARD_BY_SUJETO (hash(s) % n) o NGF_SHARD_BY_DOMINIO (hash(dominio) % n)
 * dominios: solo si modo=DOMINIO; mapeo predicado->dominio opcional (NULL=usa predicado como dominio)
 */
NGrafoSharding* n_grafo_sharding_crear(const char** rutas, size_t n, int modo);

void n_grafo_sharding_cerrar(NGrafoSharding* gs);

int n_grafo_sharding_valido(const NGrafoSharding* gs);

/**
 * Enruta triple al shard por hash(sujeto) o hash(dominio del predicado).
 */
int n_grafo_sharding_recordar(NGrafoSharding* gs, const char* s, const char* p, const char* o);

/**
 * Busqueda: si sujeto conocido, va al shard; sino broadcast a todos (map-reduce).
 */
uint32_t n_grafo_sharding_buscar_objeto(NGrafoSharding* gs, const char* sujeto, const char* predicado);

/**
 * Buscar en todos los shards, unir resultados (map-reduce).
 */
size_t n_grafo_sharding_buscar_objetos(NGrafoSharding* gs, const char* sujeto, const char* predicado,
    uint32_t* ids_out, size_t max_count);

size_t n_grafo_sharding_tamano(NGrafoSharding* gs, size_t* conceptos_out);

#ifdef __cplusplus
}
#endif

#endif
