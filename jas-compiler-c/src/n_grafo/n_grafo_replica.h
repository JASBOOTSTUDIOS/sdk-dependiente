#ifndef N_GRAFO_REPLICA_H
#define N_GRAFO_REPLICA_H

#include "n_grafo.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_REPLICA_MAX 8

typedef struct NGrafoReplica NGrafoReplica;

/**
 * Crea grafo con replicas (consistencia eventual).
 * ruta_primaria: grafo principal (lectura/escritura)
 * rutas_replica: array de rutas para replicas (solo lectura tras sync)
 * n_replicas: numero de replicas
 * Al cerrar, se sincroniza primaria -> replicas (copia del archivo).
 */
NGrafoReplica* n_grafo_replica_crear(const char* ruta_primaria, const char** rutas_replica, size_t n_replicas);

void n_grafo_replica_cerrar(NGrafoReplica* gr);

/**
 * Sincroniza ahora: primaria -> replicas. Bloqueante.
 */
int n_grafo_replica_sincronizar(NGrafoReplica* gr);

/**
 * Acceso al grafo primario para operaciones.
 */
NGrafo* n_grafo_replica_primario(NGrafoReplica* gr);

#ifdef __cplusplus
}
#endif

#endif
