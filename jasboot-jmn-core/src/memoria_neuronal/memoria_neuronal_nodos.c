#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t find_nodo_slot(JMNMemoria* mem, uint32_t id) {
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    uint32_t slot = mem->hash_nodos[h];
    while (slot != 0xFFFFFFFF) {
        if (mem->nodos[slot].id == id) return slot;
        slot = mem->nodos[slot].next_hash;
    }
    return 0xFFFFFFFF;
}

static uint32_t alloc_nodo_slot(JMNMemoria* mem, uint32_t id) {
    uint32_t slot = id % mem->cap_nodos;
    while (mem->nodos[slot].used) slot = (slot + 1) % mem->cap_nodos;
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    mem->nodos[slot].id = id;
    mem->nodos[slot].peso.u = 0;
    mem->nodos[slot].peso.f = 0.5f;
    mem->nodos[slot].used = 1;
    mem->nodos[slot].next_hash = mem->hash_nodos[h];
    mem->hash_nodos[h] = slot;
    mem->num_nodos++;
    return slot;
}

void jmn_agregar_nodo(JMNMemoria* mem, uint32_t id, JMNValor peso) {
    if (!mem || id == 0) return;
    uint32_t slot = find_nodo_slot(mem, id);
    if (slot != 0xFFFFFFFF) {
        mem->nodos[slot].peso = peso;
    } else {
        slot = alloc_nodo_slot(mem, id);
        mem->nodos[slot].peso = peso;
    }
    if (getenv("JASBOOT_DEBUG")) {
        uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
        fprintf(stderr, "[JMN] Agregado nodo %u en slot %u (bucket %u)\n", id, slot, h);
    }
    if (!mem->es_ram) mem->dirty = 1;
}

JMNNodo* jmn_obtener_nodo(JMNMemoria* mem, uint32_t id) {
    if (!mem) return NULL;
    uint32_t slot = find_nodo_slot(mem, id);
    if (slot == 0xFFFFFFFF) return NULL;
    return (JMNNodo*)&mem->nodos[slot];
}

void jmn_aprender_nodo(JMNMemoria* mem, uint32_t id, JMNValor peso) {
    jmn_agregar_nodo(mem, id, peso);
}

int jmn_iterar_nodos(JMNMemoria* mem, int (*cb)(JMNNodo*, void*), void* user) {
    if (!mem || !cb) return 0;
    for (uint32_t i = 0; i < mem->cap_nodos; i++) {
        if (mem->nodos[i].used) {
            if (cb((JMNNodo*)&mem->nodos[i], user) != 0) return 1;
        }
    }
    return 0;
}
