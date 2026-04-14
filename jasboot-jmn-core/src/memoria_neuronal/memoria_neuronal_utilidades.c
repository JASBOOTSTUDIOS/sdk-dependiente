#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

uint32_t jmn_estructura_id_texto(const char* texto) {
    if (!texto) return 0;
    uint32_t h = 5381;
    const char* p = texto;
    while (*p) {
        unsigned char c = (unsigned char)tolower(*p);
        h = ((h << 5) + h) + c;
        p++;
    }
    return h;
}

uint32_t jmn_relacion_con_contexto(uint32_t tipo, uint32_t contexto) {
    return tipo | (contexto << 8);
}

/* Listas: id lógico completo + cadena en hash_listas (evita colisiones id % 10000). */
static uint32_t find_lista_slot(JMNMemoria* mem, uint32_t id) {
    if (!mem || id == 0) return 0xFFFFFFFF;
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    uint32_t slot = mem->hash_listas[h];
    while (slot != 0xFFFFFFFF) {
        if (mem->listas[slot].id == id) return slot;
        slot = mem->listas[slot].next_hash;
    }
    return 0xFFFFFFFF;
}

static uint32_t alloc_lista_slot(JMNMemoria* mem, uint32_t id) {
    if (!mem || id == 0) return 0xFFFFFFFF;
    uint32_t slot = id % 10000u;
    uint32_t start = slot;
    for (;;) {
        if (mem->listas[slot].id == 0) break;
        slot = (slot + 1u) % 10000u;
        if (slot == start) return 0xFFFFFFFF;
    }
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    mem->listas[slot].id = id;
    mem->listas[slot].next_hash = mem->hash_listas[h];
    mem->hash_listas[h] = slot;
    if (mem->listas[slot].items == NULL) {
        mem->listas[slot].items = (JMNValor*)calloc(64, sizeof(JMNValor));
        mem->listas[slot].cap = 64;
        mem->num_listas++;
    }
    mem->listas[slot].count = 0;
    if (!mem->es_ram) mem->dirty = 1;
    return slot;
}

static uint32_t get_or_alloc_lista(JMNMemoria* mem, uint32_t id) {
    if (!mem || id == 0) return 0xFFFFFFFF;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot != 0xFFFFFFFF) return slot;
    return alloc_lista_slot(mem, id);
}

static uint32_t get_or_alloc_mapa(JMNMemoria* mem, uint32_t map_id) {
    uint32_t slot = map_id % 10000;
    if (mem->mapas[slot].keys == NULL) {
        mem->mapas[slot].keys = (uint32_t*)calloc(64, sizeof(uint32_t));
        mem->mapas[slot].vals = (JMNValor*)calloc(64, sizeof(JMNValor));
        mem->mapas[slot].cap = 64;
        mem->mapas[slot].count = 0;
        mem->num_mapas++;
    }
    return slot;
}

void jmn_crear_lista(JMNMemoria* mem, uint32_t id) {
    if (!mem || id == 0) return;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot != 0xFFFFFFFF) {
        mem->listas[slot].count = 0;
        if (!mem->es_ram) mem->dirty = 1;
        return;
    }
    alloc_lista_slot(mem, id);
}

void jmn_lista_agregar(JMNMemoria* mem, uint32_t id, JMNValor val) {
    if (!mem) return;
    uint32_t slot = get_or_alloc_lista(mem, id);
    if (slot == 0xFFFFFFFF) return;
    if (mem->listas[slot].count >= mem->listas[slot].cap) {
        uint32_t ncap = mem->listas[slot].cap * 2;
        JMNValor* p = (JMNValor*)realloc(mem->listas[slot].items, ncap * sizeof(JMNValor));
        if (p) {
            mem->listas[slot].items = p;
            mem->listas[slot].cap = ncap;
        }
    }
    if (mem->listas[slot].count < mem->listas[slot].cap) {
        mem->listas[slot].items[mem->listas[slot].count++] = val;
        if (!mem->es_ram) mem->dirty = 1;
    }
}

JMNValor jmn_lista_obtener(JMNMemoria* mem, uint32_t id, uint32_t idx) {
    JMNValor v = {0};
    if (!mem) return v;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot == 0xFFFFFFFF) return v;
    if (mem->listas[slot].items && idx < mem->listas[slot].count) {
        return mem->listas[slot].items[idx];
    }
    return v;
}

int jmn_lista_indice_fuera_de_rango(JMNMemoria* mem, uint32_t id, uint32_t idx) {
    if (!mem || id == 0) return 0;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot == 0xFFFFFFFF) return 0;
    uint32_t c = mem->listas[slot].items ? mem->listas[slot].count : 0;
    return (idx >= c) ? 1 : 0;
}

uint32_t jmn_lista_tamano(JMNMemoria* mem, uint32_t id) {
    if (!mem) return 0;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot == 0xFFFFFFFF) return 0;
    return mem->listas[slot].items ? mem->listas[slot].count : 0;
}

int jmn_lista_existe(JMNMemoria* mem, uint32_t id) {
    if (!mem) return 0;
    return find_lista_slot(mem, id) != 0xFFFFFFFF;
}

void jmn_lista_poner(JMNMemoria* mem, uint32_t id, uint32_t idx, JMNValor val) {
    if (!mem) return;
    uint32_t slot = get_or_alloc_lista(mem, id);
    if (slot == 0xFFFFFFFF) return;
    if (idx < mem->listas[slot].count) {
        mem->listas[slot].items[idx] = val;
        if (!mem->es_ram) mem->dirty = 1;
    }
}

void jmn_lista_unir(JMNMemoria* mem, uint32_t id_izq, uint32_t id_der, uint32_t id_dest) {
    if (!mem) return;
    uint32_t slot_d = get_or_alloc_lista(mem, id_dest);
    if (slot_d == 0xFFFFFFFF) return;
    uint32_t sz1 = jmn_lista_tamano(mem, id_izq);
    uint32_t sz2 = jmn_lista_tamano(mem, id_der);
    for (uint32_t i = 0; i < sz1; i++) jmn_lista_agregar(mem, id_dest, jmn_lista_obtener(mem, id_izq, i));
    for (uint32_t i = 0; i < sz2; i++) jmn_lista_agregar(mem, id_dest, jmn_lista_obtener(mem, id_der, i));
}

void jmn_vector_limpiar(JMNMemoria* mem, uint32_t id) {
    if (!mem) return;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot == 0xFFFFFFFF) return;
    if (mem->listas[slot].items) mem->listas[slot].count = 0;
    if (!mem->es_ram) mem->dirty = 1;
}

void jmn_lista_liberar(JMNMemoria* mem, uint32_t id) {
    if (!mem || id == 0) return;
    uint32_t slot = find_lista_slot(mem, id);
    if (slot == 0xFFFFFFFF) return;
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    uint32_t walk = mem->hash_listas[h];
    uint32_t *pred_next = &mem->hash_listas[h];
    while (walk != 0xFFFFFFFF) {
        if (walk == slot) {
            *pred_next = mem->listas[slot].next_hash;
            break;
        }
        pred_next = &mem->listas[walk].next_hash;
        walk = mem->listas[walk].next_hash;
    }
    free(mem->listas[slot].items);
    mem->listas[slot].items = NULL;
    mem->listas[slot].id = 0;
    mem->listas[slot].count = 0;
    mem->listas[slot].cap = 0;
    mem->listas[slot].next_hash = 0xFFFFFFFF;
    if (mem->num_listas > 0) mem->num_listas--;
    if (!mem->es_ram) mem->dirty = 1;
}

void jmn_crear_mapa(JMNMemoria* mem, uint32_t map_id) {
    if (!mem) return;
    uint32_t slot = map_id % 10000u;
    int had = mem->mapas && mem->mapas[slot].keys != NULL;
    get_or_alloc_mapa(mem, map_id);
    if (!mem->es_ram && !had) mem->dirty = 1;
}

uint32_t jmn_mapa_tamano(JMNMemoria* mem, uint32_t map_id) {
    if (!mem || !mem->mapas) return 0;
    uint32_t slot = map_id % 10000u;
    if (!mem->mapas[slot].keys) return 0;
    return mem->mapas[slot].count;
}

void jmn_mapa_insertar(JMNMemoria* mem, uint32_t map_id, uint32_t key, JMNValor val) {
    if (!mem) return;
    uint32_t slot = get_or_alloc_mapa(mem, map_id);
    for (uint32_t i = 0; i < mem->mapas[slot].count; i++) {
        if (mem->mapas[slot].keys[i] == key) {
            mem->mapas[slot].vals[i] = val;
            if (!mem->es_ram) mem->dirty = 1;
            return;
        }
    }
    if (mem->mapas[slot].count >= mem->mapas[slot].cap) {
        uint32_t ncap = mem->mapas[slot].cap * 2;
        uint32_t* pk = (uint32_t*)realloc(mem->mapas[slot].keys, ncap * sizeof(uint32_t));
        JMNValor* pv = (JMNValor*)realloc(mem->mapas[slot].vals, ncap * sizeof(JMNValor));
        if (pk && pv) {
            mem->mapas[slot].keys = pk;
            mem->mapas[slot].vals = pv;
            mem->mapas[slot].cap = ncap;
        }
    }
    if (mem->mapas[slot].count < mem->mapas[slot].cap) {
        mem->mapas[slot].keys[mem->mapas[slot].count] = key;
        mem->mapas[slot].vals[mem->mapas[slot].count] = val;
        mem->mapas[slot].count++;
        if (!mem->es_ram) mem->dirty = 1;
    }
}

int jmn_mapa_obtener_si_existe(JMNMemoria* mem, uint32_t map_id, uint32_t key, JMNValor* out) {
    JMNValor z = {0};
    if (!mem || !mem->mapas) {
        if (out) *out = z;
        return 0;
    }
    uint32_t slot = map_id % 10000u;
    if (!mem->mapas[slot].keys) {
        if (out) *out = z;
        return 0;
    }
    for (uint32_t i = 0; i < mem->mapas[slot].count; i++) {
        if (mem->mapas[slot].keys[i] == key) {
            if (out) *out = mem->mapas[slot].vals[i];
            return 1;
        }
    }
    if (out) *out = z;
    return 0;
}

JMNValor jmn_mapa_obtener(JMNMemoria* mem, uint32_t map_id, uint32_t key) {
    JMNValor v = {0};
    (void)jmn_mapa_obtener_si_existe(mem, map_id, key, &v);
    return v;
}

int jmn_mapa_existe(JMNMemoria* mem, uint32_t map_id) {
    if (!mem || !mem->mapas) return 0;
    uint32_t slot = map_id % 10000u;
    return mem->mapas[slot].keys != NULL;
}
