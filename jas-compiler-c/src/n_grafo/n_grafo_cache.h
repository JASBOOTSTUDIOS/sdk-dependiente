#ifndef N_GRAFO_CACHE_H
#define N_GRAFO_CACHE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_CACHE_LOCAL   0
#define NGF_CACHE_SHM     1
#define NGF_CACHE_REDIS   2

typedef struct NGrafoCache NGrafoCache;

/* Crea caché. tipo: LOCAL (LRU en proceso), SHM (memoria compartida POSIX), REDIS (stub) */
NGrafoCache* n_grafo_cache_crear(int tipo, const char* config);

void n_grafo_cache_cerrar(NGrafoCache* c);

/* Guarda/obtiene valor por clave (hash de triple o concepto) */
int n_grafo_cache_put(NGrafoCache* c, uint64_t key, const void* val, size_t len);
int n_grafo_cache_get(NGrafoCache* c, uint64_t key, void* val_out, size_t* len_out);

#ifdef __cplusplus
}
#endif

#endif
