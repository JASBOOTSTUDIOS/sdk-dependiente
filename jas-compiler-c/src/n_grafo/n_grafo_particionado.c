/**
 * n_grafo_particionado.c - Implementación de grafo particionado por dominio
 */
#include "n_grafo_particionado.h"
#include "n_grafo.h"
#include <stdlib.h>
#include <string.h>

struct NGrafoParticionado {
    NGrafo* particiones[NGF_MAX_PARTICIONES];
    char* dominios[NGF_MAX_PARTICIONES];
    size_t n;
};

NGrafoParticionado* n_abrir_grafo_particionado(const char** rutas, const char** dominios, size_t n) {
    if (!rutas || n == 0 || n > NGF_MAX_PARTICIONES) return NULL;

    NGrafoParticionado* gp = (NGrafoParticionado*)calloc(1, sizeof(NGrafoParticionado));
    if (!gp) return NULL;

    for (size_t i = 0; i < n; i++) {
        gp->particiones[i] = n_abrir_grafo(rutas[i]);
        if (!gp->particiones[i]) {
            for (size_t j = 0; j < i; j++) {
                n_cerrar_grafo(gp->particiones[j]);
                free(gp->dominios[j]);
            }
            free(gp);
            return NULL;
        }
        gp->dominios[i] = dominios && dominios[i] ? strdup(dominios[i]) : strdup(NGF_DOMINIO_GEN);
        gp->n++;
    }
    return gp;
}

void n_cerrar_grafo_particionado(NGrafoParticionado* gp) {
    if (!gp) return;
    for (size_t i = 0; i < gp->n; i++) {
        n_cerrar_grafo(gp->particiones[i]);
        free(gp->dominios[i]);
    }
    free(gp);
}

int n_grafo_particionado_valido(const NGrafoParticionado* gp) {
    return gp != NULL && gp->n > 0;
}

static size_t n_dominio_a_indice(NGrafoParticionado* gp, const char* dominio) {
    if (!dominio || !dominio[0]) return 0;
    for (size_t i = 0; i < gp->n; i++) {
        if (gp->dominios[i] && strcmp(gp->dominios[i], dominio) == 0)
            return i;
    }
    return 0;
}

int n_recordar_en(NGrafoParticionado* gp, const char* dominio, const char* s, const char* p, const char* o) {
    if (!gp) return 0;
    size_t idx = n_dominio_a_indice(gp, dominio);
    return n_recordar(gp->particiones[idx], s, p, o);
}

uint32_t n_buscar_objeto_en(NGrafoParticionado* gp, const char* dominio, const char* sujeto, const char* predicado) {
    if (!gp) return NGF_ID_NULO;
    if (dominio && dominio[0]) {
        size_t idx = n_dominio_a_indice(gp, dominio);
        return n_buscar_objeto(gp->particiones[idx], sujeto, predicado);
    }
    for (size_t i = 0; i < gp->n; i++) {
        uint32_t o = n_buscar_objeto(gp->particiones[i], sujeto, predicado);
        if (o != NGF_ID_NULO) return o;
    }
    return NGF_ID_NULO;
}

size_t n_buscar_objetos_en(NGrafoParticionado* gp, const char* dominio, const char* s, const char* p, uint32_t* ids, size_t max_count) {
    if (!gp) return 0;
    size_t total = 0;
    uint32_t tmp[256];
    if (dominio && dominio[0]) {
        size_t idx = n_dominio_a_indice(gp, dominio);
        return n_buscar_objetos(gp->particiones[idx], s, p, ids, max_count);
    }
    for (size_t i = 0; i < gp->n && total < max_count; i++) {
        size_t n = n_buscar_objetos(gp->particiones[i], s, p, ids ? ids + total : tmp, max_count - total);
        total += n;
    }
    return total;
}

size_t n_tamano_grafo_particionado(const NGrafoParticionado* gp, size_t* conceptos_out) {
    if (!gp) return 0;
    size_t triples = 0;
    size_t conceptos = 0;
    for (size_t i = 0; i < gp->n; i++) {
        size_t c = 0;
        triples += n_tamano_grafo(gp->particiones[i], &c);
        conceptos += c;
    }
    if (conceptos_out) *conceptos_out = conceptos;
    return triples;
}

int n_heredar_particionado(const NGrafoParticionado* gp, const char* dominio,
    const char* sujeto, const char* predicado, char* buf, size_t buf_size) {
    if (!gp || !buf || buf_size == 0) return 0;
    buf[0] = '\0';

    if (dominio && dominio[0]) {
        size_t idx = n_dominio_a_indice((NGrafoParticionado*)gp, dominio);
        return n_heredar_texto(gp->particiones[idx], sujeto, predicado, buf, buf_size);
    }

    for (size_t i = 0; i < gp->n; i++) {
        if (n_heredar_texto(gp->particiones[i], sujeto, predicado, buf, buf_size))
            return 1;
    }
    return 0;
}
