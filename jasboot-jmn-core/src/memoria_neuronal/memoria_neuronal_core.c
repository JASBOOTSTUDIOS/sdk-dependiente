/**
 * JMN Core: apertura, cierre, persistencia
 */
#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern int jmn_io_guardar(JMNMemoria* mem, const char* ruta);
extern int jmn_io_cargar(JMNMemoria* mem, const char* ruta);

uint32_t jmn_hash_u32(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x);
    return x;
}

uint32_t jmn_hash_str(const char* s) {
    uint32_t h = 5381;
    if (!s) return 0;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

static JMNMemoria* jmn_alloc(uint32_t cap_nodos, uint32_t cap_conex) {
    JMNMemoria* m = (JMNMemoria*)calloc(1, sizeof(JMNMemoria));
    if (!m) return NULL;
    m->cap_nodos = cap_nodos ? cap_nodos : JMN_DEFAULT_NODOS;
    m->cap_conexiones = cap_conex ? cap_conex : JMN_DEFAULT_CONEX;
    m->cap_textos = 50000;
    m->num_listas = 0;
    m->num_mapas = 0;

    m->nodos = (JMNEntradaNodo*)calloc(m->cap_nodos, sizeof(JMNEntradaNodo));
    m->hash_nodos = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_nodos[i] = 0xFFFFFFFF;

    m->conexiones = (JMNEntradaConexion*)calloc(m->cap_conexiones, sizeof(JMNEntradaConexion));
    m->hash_conexiones = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    m->cabeza_origen = (uint32_t*)calloc(m->cap_nodos + 1, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_conexiones[i] = 0xFFFFFFFF;
    for (uint32_t i = 0; i <= m->cap_nodos; i++) m->cabeza_origen[i] = 0xFFFFFFFF;

    m->textos = (JMNEntradaTexto*)calloc(m->cap_textos, sizeof(JMNEntradaTexto));
    m->hash_textos = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_textos[i] = 0xFFFFFFFF;

    m->listas = (JMNLista*)calloc(10000, sizeof(JMNLista));
    for (uint32_t i = 0; i < 10000; i++) m->listas[i].next_hash = 0xFFFFFFFF;
    m->hash_listas = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_listas[i] = 0xFFFFFFFF;

    m->mapas = (JMNMapa*)calloc(10000, sizeof(JMNMapa));
    m->hash_mapas = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_mapas[i] = 0xFFFFFFFF;

    if (!m->nodos || !m->hash_nodos || !m->conexiones || !m->hash_conexiones ||
        !m->cabeza_origen || !m->textos || !m->hash_textos ||
        !m->listas || !m->hash_listas || !m->mapas || !m->hash_mapas) {
        jmn_cerrar(m);
        return NULL;
    }
    return m;
}

static void jmn_free_data(JMNMemoria* m) {
    if (!m) return;
    free(m->nodos);
    free(m->hash_nodos);
    free(m->conexiones);
    free(m->hash_conexiones);
    free(m->cabeza_origen);
    free(m->textos);
    free(m->hash_textos);
    if (m->listas) {
        for (uint32_t i = 0; i < 10000; i++) {
            if (m->listas[i].items) free(m->listas[i].items);
        }
        free(m->listas);
    }
    free(m->hash_listas);
    if (m->mapas) {
        for (uint32_t i = 0; i < 10000; i++) {
            if (m->mapas[i].keys) free(m->mapas[i].keys);
            if (m->mapas[i].vals) free(m->mapas[i].vals);
        }
        free(m->mapas);
    }
    free(m->hash_mapas);
}

JMNMemoria* jmn_crear(const char* ruta) {
    JMNMemoria* m = jmn_alloc(JMN_DEFAULT_NODOS, JMN_DEFAULT_CONEX);
    if (!m) return NULL;
    m->es_ram = 0;
    if (ruta) {
        strncpy(m->ruta_archivo, ruta, sizeof(m->ruta_archivo) - 1);
        m->ruta_archivo[sizeof(m->ruta_archivo)-1] = '\0';
    }
    m->dirty = 0;
    return m;
}

JMNMemoria* jmn_abrir_escritura(const char* ruta) {
    JMNMemoria* m = jmn_crear(ruta);
    if (!m || !ruta || !ruta[0]) return m;
    if (jmn_io_cargar(m, ruta) == 0) {
        m->dirty = 0;
    }
    return m;
}

JMNMemoria* jmn_abrir_lectura(const char* ruta) {
    JMNMemoria* m = jmn_abrir_escritura(ruta);
    return m;
}

void jmn_finalizar_escritura(JMNMemoria* mem) {
    if (!mem) return;
    if (!mem->es_ram && mem->dirty && mem->ruta_archivo[0]) {
        jmn_io_guardar(mem, mem->ruta_archivo);
        mem->dirty = 0;
    }
}

void jmn_cerrar(JMNMemoria* mem) {
    if (!mem) return;
    jmn_finalizar_escritura(mem);
    jmn_free_data(mem);
    free(mem);
}

JMNMemoria* jmn_crear_memoria_ram(uint32_t cap_nodos, uint32_t cap_conex) {
    JMNMemoria* m = jmn_alloc(cap_nodos, cap_conex);
    if (!m) return NULL;
    m->es_ram = 1;
    m->ruta_archivo[0] = '\0';
    return m;
}
