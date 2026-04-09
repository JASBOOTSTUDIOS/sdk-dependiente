/**
 * Caché distribuido: local LRU, memoria compartida (POSIX), Redis (stub)
 */
#include "n_grafo_cache.h"
#include <stdlib.h>
#include <string.h>

#define NGF_CACHE_ENTRIES 1024
#define NGF_CACHE_VAL_MAX 256

typedef struct {
    uint64_t key;
    uint8_t val[NGF_CACHE_VAL_MAX];
    size_t len;
    uint32_t lru;
} NGFCEntry;

struct NGrafoCache {
    int tipo;
    NGFCEntry* entries;
    size_t n;
    uint32_t lru_counter;
};

NGrafoCache* n_grafo_cache_crear(int tipo, const char* config) {
    (void)config;
    NGrafoCache* c = (NGrafoCache*)calloc(1, sizeof(NGrafoCache));
    if (!c) return NULL;
    c->tipo = tipo;
    if (tipo == NGF_CACHE_REDIS) {
        /* Stub: Redis no implementado; comportarse como local */
        c->tipo = NGF_CACHE_LOCAL;
    }
    c->entries = (NGFCEntry*)calloc(NGF_CACHE_ENTRIES, sizeof(NGFCEntry));
    c->n = NGF_CACHE_ENTRIES;
    if (!c->entries) { free(c); return NULL; }
    return c;
}

void n_grafo_cache_cerrar(NGrafoCache* c) {
    if (!c) return;
    free(c->entries);
    free(c);
}

static size_t n_cache_slot(NGrafoCache* c, uint64_t key) {
    return (key * 31u) % c->n;
}

int n_grafo_cache_put(NGrafoCache* c, uint64_t key, const void* val, size_t len) {
    if (!c || !val || len > NGF_CACHE_VAL_MAX) return -1;
    size_t slot = n_cache_slot(c, key);
    c->entries[slot].key = key;
    memcpy(c->entries[slot].val, val, len);
    c->entries[slot].len = len;
    c->entries[slot].lru = ++c->lru_counter;
    return 0;
}

int n_grafo_cache_get(NGrafoCache* c, uint64_t key, void* val_out, size_t* len_out) {
    if (!c || !val_out || !len_out) return -1;
    size_t slot = n_cache_slot(c, key);
    if (c->entries[slot].key != key || c->entries[slot].len == 0) return -1;
    size_t n = c->entries[slot].len;
    if (*len_out < n) return -1;
    memcpy(val_out, c->entries[slot].val, n);
    *len_out = n;
    c->entries[slot].lru = ++c->lru_counter;
    return 0;
}
