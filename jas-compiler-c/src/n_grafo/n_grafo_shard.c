/**
 * n_grafo_shard.c - Sharding por hash de sujeto
 */
#include "n_grafo_shard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

size_t n_grafo_shard_hash_sujeto(const char* sujeto) {
    return (size_t)fnv1a(sujeto ? sujeto : "");
}

size_t n_grafo_shard_indice(NGrafoShard* gs, const char* sujeto) {
    if (!gs || gs->num_shards == 0) return 0;
    return n_grafo_shard_hash_sujeto(sujeto) % gs->num_shards;
}

NGrafoShard* n_grafo_shard_crear(const char* base_ruta, size_t num_shards) {
    if (!base_ruta || num_shards == 0 || num_shards > NGF_MAX_SHARDS) return NULL;

    NGrafoShard* gs = (NGrafoShard*)calloc(1, sizeof(NGrafoShard));
    if (!gs) return NULL;

    strncpy(gs->base_ruta, base_ruta, sizeof(gs->base_ruta) - 1);
    gs->base_ruta[sizeof(gs->base_ruta) - 1] = '\0';
    gs->num_shards = num_shards;

    for (size_t i = 0; i < num_shards; i++) {
        char ruta[600];
        snprintf(ruta, sizeof(ruta), "%s_%zu.ngf", gs->base_ruta, i);
        gs->shards[i] = n_abrir_grafo(ruta);
        if (!gs->shards[i]) {
            for (size_t j = 0; j < i; j++) n_cerrar_grafo(gs->shards[j]);
            free(gs);
            return NULL;
        }
    }
    return gs;
}

void n_grafo_shard_cerrar(NGrafoShard* gs) {
    if (!gs) return;
    for (size_t i = 0; i < gs->num_shards; i++) {
        n_cerrar_grafo(gs->shards[i]);
    }
    free(gs);
}

int n_grafo_shard_recordar(NGrafoShard* gs, const char* s, const char* p, const char* o) {
    if (!gs || !s || !p || !o) return 0;
    size_t idx = n_grafo_shard_indice(gs, s);
    return n_recordar(gs->shards[idx], s, p, o);
}

uint32_t n_grafo_shard_buscar_objeto(NGrafoShard* gs, const char* sujeto, const char* predicado) {
    if (!gs) return NGF_ID_NULO;
    size_t idx = n_grafo_shard_indice(gs, sujeto);
    return n_buscar_objeto(gs->shards[idx], sujeto, predicado);
}

uint32_t n_grafo_shard_buscar_objeto_global(NGrafoShard* gs, const char* sujeto, const char* predicado) {
    if (!gs) return NGF_ID_NULO;
    for (size_t i = 0; i < gs->num_shards; i++) {
        uint32_t o = n_buscar_objeto(gs->shards[i], sujeto, predicado);
        if (o != NGF_ID_NULO) return o;
    }
    return NGF_ID_NULO;
}

const NGrafo* n_grafo_shard_obtener(NGrafoShard* gs, size_t idx) {
    if (!gs || idx >= gs->num_shards) return NULL;
    return gs->shards[idx];
}

int n_grafo_shard_obtener_texto(NGrafoShard* gs, const char* sujeto, uint32_t id, char* buf, size_t buf_size) {
    if (!gs || !buf || buf_size == 0) return 0;
    size_t idx = n_grafo_shard_indice(gs, sujeto);
    return n_obtener_texto(gs->shards[idx], id, buf, buf_size);
}

size_t n_grafo_shard_num_shards(const NGrafoShard* gs) {
    return gs ? gs->num_shards : 0;
}

size_t n_grafo_shard_tamano(NGrafoShard* gs, size_t* conceptos_out) {
    if (!gs) return 0;
    size_t triples = 0, conceptos = 0;
    for (size_t i = 0; i < gs->num_shards; i++) {
        size_t c = 0;
        triples += n_tamano_grafo(gs->shards[i], &c);
        conceptos += c;
    }
    if (conceptos_out) *conceptos_out = conceptos;
    return triples;
}

#define N_HEREDAR_SHARD_MAX 64
#define N_HEREDAR_BUF 128
static int n_heredar_shard_visitado(const char visitado[][N_HEREDAR_BUF], size_t n, const char* s) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(visitado[i], s) == 0) return 1;
    return 0;
}

static int n_heredar_shard_impl(NGrafoShard* gs, const char* sujeto, const char* predicado,
    char* buf, size_t buf_size, char visitado[][N_HEREDAR_BUF], size_t n_vis) {
    if (!gs || !sujeto || !predicado || n_vis >= N_HEREDAR_SHARD_MAX) return 0;

    /* Directo: (sujeto, predicado, O) en cualquier shard */
    for (size_t i = 0; i < gs->num_shards; i++) {
        uint32_t oid = n_buscar_objeto(gs->shards[i], sujeto, predicado);
        if (oid != NGF_ID_NULO) {
            return n_obtener_texto(gs->shards[i], oid, buf, buf_size);
        }
    }

    /* Transitiva: (sujeto, n_es_un, tipo) */
    for (size_t i = 0; i < gs->num_shards; i++) {
        uint32_t tipo_id = n_buscar_objeto(gs->shards[i], sujeto, "n_es_un");
        if (tipo_id != NGF_ID_NULO) {
            char tipo_buf[N_HEREDAR_BUF];
            tipo_buf[0] = '\0';
            if (!n_obtener_texto(gs->shards[i], tipo_id, tipo_buf, sizeof(tipo_buf)))
                continue;
            if (n_heredar_shard_visitado(visitado, n_vis, tipo_buf)) continue;
            strncpy(visitado[n_vis], tipo_buf, N_HEREDAR_BUF - 1);
            visitado[n_vis][N_HEREDAR_BUF - 1] = '\0';
            if (n_heredar_shard_impl(gs, tipo_buf, predicado, buf, buf_size, visitado, n_vis + 1))
                return 1;
        }
    }
    return 0;
}

int n_grafo_shard_heredar(NGrafoShard* gs, const char* sujeto, const char* predicado,
    char* buf, size_t buf_size) {
    if (!gs || !buf || buf_size == 0) return 0;
    buf[0] = '\0';
    char visitado[N_HEREDAR_SHARD_MAX][N_HEREDAR_BUF];
    memset(visitado, 0, sizeof(visitado));
    strncpy(visitado[0], sujeto, N_HEREDAR_BUF - 1);
    visitado[0][N_HEREDAR_BUF - 1] = '\0';
    return n_heredar_shard_impl(gs, sujeto, predicado, buf, buf_size, visitado, 1);
}
