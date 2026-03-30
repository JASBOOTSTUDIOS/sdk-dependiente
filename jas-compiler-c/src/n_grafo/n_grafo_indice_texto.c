/**
 * Índice invertido para búsqueda de texto completo (opcional)
 * Término -> [concept_ids] donde el concepto contiene el término
 */
#include "n_grafo_indice_texto.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Acceso al vocabulario interno - n_grafo no lo expone; usamos n_obtener_texto iterando.
 * Alternativa: añadir n_iterar_conceptos al n_grafo. Por ahora, el índice se construye
 * pasando por n_tamano_grafo para saber conceptos, pero no hay API para iterar.
 * Solución: el índice se construye desde el grafo exportando IDs. Necesitamos n_lista_conceptos.
 * Como no existe, hacemos un índice "lazy" que se alimenta con n_obtener_texto cuando
 * se hace n_recordar - no, el grafo no notifica. 
 * Mejor: añadir a n_grafo.h una función n_iterar_conceptos(g, cb, user) que llame cb(id, texto) para cada concepto.
 * O: el índice se crea en n_grafo_core cuando guardamos - pero eso requiere modificar el core.
 * 
 * Implementación mínima: asumimos que podemos acceder a los conceptos. Revisando n_grafo...
 * No hay n_iterar_conceptos. La opción más limpia: agregar n_para_cada_concepto(g, callback, user) al n_grafo.
 * Eso requiere modificar n_grafo_core.c y n_grafo.h.
 * 
 * Alternativa sin modificar core: el índice se alimenta manualmente. n_indice_texto_agregar(it, id, texto).
 * Y tenemos n_indice_texto_construir_desde_grafo que... no puede iterar sin API.
 * 
 * Voy a añadir n_para_cada_concepto al n_grafo para que el índice pueda construirse.
 */
#include "n_grafo.h"


#define NGF_IT_HASH_BUCKETS 4096
#define NGF_IT_MAX_TERM_LEN 64

typedef struct NGFITEntry {
    char* termino;
    uint32_t* ids;
    size_t n, cap;
    struct NGFITEntry* next;
} NGFITEntry;

struct NGrafoIndiceTexto {
    const NGrafo* grafo;
    NGFITEntry** buckets;
};

static uint32_t n_it_hash(const char* s) {
    uint32_t h = 5381;
    while (*s) { h = ((h << 5) + h) + (unsigned char)*s++; }
    return h;
}

static void n_it_agregar(NGrafoIndiceTexto* it, const char* term, uint32_t id) {
    if (!term || !term[0]) return;
    uint32_t h = n_it_hash(term) % NGF_IT_HASH_BUCKETS;
    NGFITEntry* e = it->buckets[h];
    while (e) {
        if (strcmp(e->termino, term) == 0) {
            for (size_t i = 0; i < e->n; i++) if (e->ids[i] == id) return;
            if (e->n >= e->cap) {
                size_t nc = e->cap ? e->cap * 2 : 4;
                uint32_t* t = (uint32_t*)realloc(e->ids, nc * sizeof(uint32_t));
                if (!t) return;
                e->ids = t;
                e->cap = nc;
            }
            e->ids[e->n++] = id;
            return;
        }
        e = e->next;
    }
    e = (NGFITEntry*)calloc(1, sizeof(NGFITEntry));
    if (!e) return;
    e->termino = strdup(term);
    e->ids = (uint32_t*)malloc(4 * sizeof(uint32_t));
    e->cap = 4;
    e->n = 1;
    e->ids[0] = id;
    e->next = it->buckets[h];
    it->buckets[h] = e;
}

static int n_construir_cb(uint32_t id, const char* texto, void* user) {
    NGrafoIndiceTexto* it = (NGrafoIndiceTexto*)user;
    if (!texto) return 0;
    char buf[NGF_IT_MAX_TERM_LEN + 1];
    const char* p = texto;
    while (*p) {
        size_t i = 0;
        while (*p && isalnum((unsigned char)*p) && i < NGF_IT_MAX_TERM_LEN)
            buf[i++] = (char)tolower((unsigned char)*p++);
        buf[i] = '\0';
        if (i > 0) n_it_agregar(it, buf, id);
        while (*p && !isalnum((unsigned char)*p)) p++;
    }
    return 0;
}

NGrafoIndiceTexto* n_indice_texto_crear(const NGrafo* g) {
    if (!g) return NULL;
    NGrafoIndiceTexto* it = (NGrafoIndiceTexto*)calloc(1, sizeof(NGrafoIndiceTexto));
    if (!it) return NULL;
    it->grafo = g;
    it->buckets = (NGFITEntry**)calloc(NGF_IT_HASH_BUCKETS, sizeof(NGFITEntry*));
    if (!it->buckets) { free(it); return NULL; }
    n_para_cada_concepto(g, n_construir_cb, it);
    return it;
}

void n_indice_texto_cerrar(NGrafoIndiceTexto* it) {
    if (!it) return;
    for (uint32_t i = 0; i < NGF_IT_HASH_BUCKETS; i++) {
        NGFITEntry* e = it->buckets[i];
        while (e) {
            NGFITEntry* next = e->next;
            free(e->termino);
            free(e->ids);
            free(e);
            e = next;
        }
    }
    free(it->buckets);
    free(it);
}

size_t n_indice_texto_buscar(const NGrafoIndiceTexto* it, const char* termino, uint32_t* ids_out, size_t max_count) {
    if (!it || !termino) return 0;
    char norm[NGF_IT_MAX_TERM_LEN + 1];
    size_t i = 0;
    while (termino[i] && i < NGF_IT_MAX_TERM_LEN) {
        norm[i] = (char)tolower((unsigned char)termino[i]);
        i++;
    }
    norm[i] = '\0';
    if (i == 0) return 0;

    uint32_t h = n_it_hash(norm) % NGF_IT_HASH_BUCKETS;
    NGFITEntry* e = it->buckets[h];
    while (e) {
        if (strcmp(e->termino, norm) == 0) {
            size_t copy = e->n < max_count ? e->n : max_count;
            if (ids_out) for (size_t j = 0; j < copy; j++) ids_out[j] = e->ids[j];
            return copy;
        }
        e = e->next;
    }
    return 0;
}
