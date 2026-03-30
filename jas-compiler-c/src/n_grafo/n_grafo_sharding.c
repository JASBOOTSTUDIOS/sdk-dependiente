/**
 * Sharding por hash de sujeto o dominio (Fase D)
 */
#include "n_grafo_sharding.h"
#include <stdlib.h>
#include <string.h>

struct NGrafoSharding {
    NGrafo* shards[NGF_SHARD_MAX];
    char* rutas[NGF_SHARD_MAX];
    size_t n_shards;
    int modo;
};

static uint32_t hash_str(const char* s) {
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

NGrafoSharding* n_grafo_sharding_crear(const char** rutas, size_t n, int modo) {
    if (!rutas || n == 0 || n > NGF_SHARD_MAX) return NULL;
    NGrafoSharding* gs = (NGrafoSharding*)calloc(1, sizeof(NGrafoSharding));
    if (!gs) return NULL;
    gs->modo = modo;
    gs->n_shards = n;
    for (size_t i = 0; i < n; i++) {
        gs->shards[i] = n_abrir_grafo(rutas[i]);
        gs->rutas[i] = rutas[i] ? strdup(rutas[i]) : NULL;
        if (!gs->shards[i]) {
            for (size_t j = 0; j < i; j++) {
                n_cerrar_grafo(gs->shards[j]);
                free(gs->rutas[j]);
            }
            free(gs);
            return NULL;
        }
    }
    return gs;
}

void n_grafo_sharding_cerrar(NGrafoSharding* gs) {
    if (!gs) return;
    for (size_t i = 0; i < gs->n_shards; i++) {
        n_cerrar_grafo(gs->shards[i]);
        free(gs->rutas[i]);
    }
    free(gs);
}

int n_grafo_sharding_valido(const NGrafoSharding* gs) {
    return gs != NULL && gs->n_shards > 0;
}

static size_t shard_idx(NGrafoSharding* gs, const char* sujeto, const char* predicado) {
    uint32_t h;
    if (gs->modo == NGF_SHARD_BY_SUJETO)
        h = hash_str(sujeto ? sujeto : "");
    else
        h = hash_str(predicado ? predicado : "");
    return (size_t)(h % (uint32_t)gs->n_shards);
}

int n_grafo_sharding_recordar(NGrafoSharding* gs, const char* s, const char* p, const char* o) {
    if (!gs || !s || !p || !o) return 0;
    size_t idx = shard_idx(gs, s, p);
    return n_recordar(gs->shards[idx], s, p, o);
}

uint32_t n_grafo_sharding_buscar_objeto(NGrafoSharding* gs, const char* sujeto, const char* predicado) {
    if (!gs) return NGF_ID_NULO;
    if (sujeto && sujeto[0]) {
        size_t idx = shard_idx(gs, sujeto, predicado);
        return n_buscar_objeto(gs->shards[idx], sujeto, predicado);
    }
    for (size_t i = 0; i < gs->n_shards; i++) {
        uint32_t o = n_buscar_objeto(gs->shards[i], sujeto, predicado);
        if (o != NGF_ID_NULO) return o;
    }
    return NGF_ID_NULO;
}

size_t n_grafo_sharding_buscar_objetos(NGrafoSharding* gs, const char* sujeto, const char* predicado,
    uint32_t* ids_out, size_t max_count) {
    if (!gs) return 0;
    size_t total = 0;
    uint32_t tmp[256];
    for (size_t i = 0; i < gs->n_shards && total < max_count; i++) {
        size_t n = n_buscar_objetos(gs->shards[i], sujeto, predicado,
            ids_out ? ids_out + total : tmp, max_count - total);
        total += n;
    }
    return total;
}

size_t n_grafo_sharding_tamano(NGrafoSharding* gs, size_t* conceptos_out) {
    if (!gs) return 0;
    size_t triples = 0;
    size_t conceptos = 0;
    for (size_t i = 0; i < gs->n_shards; i++) {
        size_t c = 0;
        triples += n_tamano_grafo(gs->shards[i], &c);
        conceptos += c;
    }
    if (conceptos_out) *conceptos_out = conceptos;
    return triples;
}
