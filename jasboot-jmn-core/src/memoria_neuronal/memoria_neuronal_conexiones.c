#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>

static uint32_t alloc_conex_slot(JMNMemoria* mem, uint32_t ori, uint32_t dest) {
    uint32_t slot = ((ori * 31u) + dest) % mem->cap_conexiones;
    while (mem->conexiones[slot].used) slot = (slot + 1) % mem->cap_conexiones;
    uint32_t bucket = ori % (mem->cap_nodos + 1);
    if (bucket > mem->cap_nodos) bucket = mem->cap_nodos;
    mem->conexiones[slot].origen_id = ori;
    mem->conexiones[slot].destino_id = dest;
    mem->conexiones[slot].used = 1;
    mem->conexiones[slot].next_origen = mem->cabeza_origen[bucket];
    mem->cabeza_origen[bucket] = slot;
    mem->num_conexiones++;
    return slot;
}

static uint32_t find_conex_slot(JMNMemoria* mem, uint32_t ori, uint32_t dest) {
    uint32_t bucket = ori % (mem->cap_nodos + 1);
    if (bucket > mem->cap_nodos) bucket = mem->cap_nodos;
    uint32_t slot = mem->cabeza_origen[bucket];
    while (slot != 0xFFFFFFFF) {
        if (mem->conexiones[slot].origen_id == ori && mem->conexiones[slot].destino_id == dest)
            return slot;
        slot = mem->conexiones[slot].next_origen;
    }
    return 0xFFFFFFFF;
}

static void unlink_conexion_origen(JMNMemoria* mem, uint32_t slot) {
    if (!mem || slot >= mem->cap_conexiones || !mem->conexiones[slot].used)
        return;
    uint32_t ori = mem->conexiones[slot].origen_id;
    uint32_t bucket = ori % (mem->cap_nodos + 1);
    if (bucket > mem->cap_nodos) bucket = mem->cap_nodos;
    uint32_t head = mem->cabeza_origen[bucket];
    if (head == slot) {
        mem->cabeza_origen[bucket] = mem->conexiones[slot].next_origen;
    } else {
        uint32_t cur = head;
        while (cur != 0xFFFFFFFF) {
            uint32_t nx = mem->conexiones[cur].next_origen;
            if (nx == slot) {
                mem->conexiones[cur].next_origen = mem->conexiones[slot].next_origen;
                break;
            }
            cur = nx;
        }
    }
    mem->conexiones[slot].used = 0;
    mem->conexiones[slot].origen_id = 0;
    mem->conexiones[slot].destino_id = 0;
    mem->conexiones[slot].key_id = 0;
    mem->conexiones[slot].fuerza.f = 0.f;
    mem->conexiones[slot].next_origen = 0xFFFFFFFF;
    if (mem->num_conexiones > 0)
        mem->num_conexiones--;
}

void jmn_agregar_conexion(JMNMemoria* mem, uint32_t origen, uint32_t dest, JMNValor fuerza, uint32_t tipo) {
    if (!mem || origen == 0 || dest == 0) return;
    uint32_t slot = find_conex_slot(mem, origen, dest);
    if (slot != 0xFFFFFFFF) {
        mem->conexiones[slot].fuerza.f += fuerza.f;
        mem->conexiones[slot].key_id = tipo;
    } else {
        slot = alloc_conex_slot(mem, origen, dest);
        mem->conexiones[slot].fuerza = fuerza;
        mem->conexiones[slot].key_id = tipo;
    }
    if (!mem->es_ram) mem->dirty = 1;
}

JMNConexion* jmn_obtener_conexiones(JMNMemoria* mem, JMNNodo* nodo, uint32_t* count) {
    static JMNConexion out_buf[256];
    if (!mem || !nodo || !count) return NULL;
    *count = 0;
    uint32_t ori = nodo->id;
    uint32_t bucket = ori % (mem->cap_nodos + 1);
    if (bucket > mem->cap_nodos) bucket = mem->cap_nodos;
    uint32_t slot = mem->cabeza_origen[bucket];
    while (slot != 0xFFFFFFFF && *count < 256) {
        if (mem->conexiones[slot].origen_id == ori) {
            out_buf[*count].destino_id = mem->conexiones[slot].destino_id;
            out_buf[*count].key_id = mem->conexiones[slot].key_id;
            out_buf[*count].fuerza = mem->conexiones[slot].fuerza;
            (*count)++;
        }
        slot = mem->conexiones[slot].next_origen;
    }
    return *count > 0 ? out_buf : NULL;
}

void jmn_penalizar_asociacion(JMNMemoria* mem, uint32_t id_a, uint32_t id_b, float delta) {
    uint32_t slot = find_conex_slot(mem, id_a, id_b);
    if (slot != 0xFFFFFFFF && mem->conexiones[slot].fuerza.f > 0.01f) {
        mem->conexiones[slot].fuerza.f -= delta;
        if (mem->conexiones[slot].fuerza.f < 0) mem->conexiones[slot].fuerza.f = 0;
        if (!mem->es_ram) mem->dirty = 1;
    }
}

float jmn_obtener_fuerza_asociacion(JMNMemoria* mem, uint32_t id1, uint32_t id2) {
    uint32_t slot = find_conex_slot(mem, id1, id2);
    if (slot != 0xFFFFFFFF) return mem->conexiones[slot].fuerza.f;
    return 0.0f;
}

void jmn_decaer_conexiones_global(JMNMemoria* mem, float factor, int pasadas, float umbral) {
    if (!mem) return;
    float f = factor;
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    float mult = 1.0f - f;
    float u = umbral;
    if (u <= 0.f) u = 0.01f;
    int p = pasadas;
    if (p < 1) p = 1;
    if (p > 100) p = 100;
    for (int t = 0; t < p; t++) {
        for (uint32_t i = 0; i < mem->cap_conexiones; i++) {
            if (!mem->conexiones[i].used) continue;
            float val = mem->conexiones[i].fuerza.f;
            if (val <= 0.f) continue;
            val *= mult;
            if (val < u) val = 0.f;
            mem->conexiones[i].fuerza.f = val;
        }
    }
    if (!mem->es_ram) mem->dirty = 1;
}

uint32_t jmn_olvidar_conexiones_debiles(JMNMemoria* mem, float umbral) {
    if (!mem) return 0;
    float u = umbral;
    if (u < 0.f) u = 0.f;
    const float eps = 1e-6f;
    uint32_t removed = 0;
    for (uint32_t i = 0; i < mem->cap_conexiones; i++) {
        if (!mem->conexiones[i].used) continue;
        float fv = mem->conexiones[i].fuerza.f;
        if (fv <= u + eps || fv <= 0.f) {
            unlink_conexion_origen(mem, i);
            removed++;
        }
    }
    if (removed && !mem->es_ram) mem->dirty = 1;
    return removed;
}

void jmn_consolidar_conexiones_supervivientes(JMNMemoria* mem, float boost_relativo) {
    if (!mem) return;
    float b = boost_relativo;
    if (b < 0.f) b = 0.f;
    if (b > 2.f) b = 2.f;
    float mult = 1.f + b;
    for (uint32_t i = 0; i < mem->cap_conexiones; i++) {
        if (!mem->conexiones[i].used) continue;
        float val = mem->conexiones[i].fuerza.f;
        if (val <= 0.f) continue;
        val *= mult;
        if (val > 1.f) val = 1.f;
        mem->conexiones[i].fuerza.f = val;
    }
    if (!mem->es_ram) mem->dirty = 1;
}

void jmn_consolidar_memoria_sueno(JMNMemoria* mem, float factor_decay, int pasadas,
                                  float umbral_olvido, float boost_relativo) {
    if (!mem) return;
    jmn_decaer_conexiones_global(mem, factor_decay, pasadas, umbral_olvido);
    jmn_olvidar_conexiones_debiles(mem, umbral_olvido);
    jmn_consolidar_conexiones_supervivientes(mem, boost_relativo);
}

void jmn_reforzar_concepto(JMNMemoria* mem, uint32_t id, float delta) {
    if (!mem || id == 0 || delta <= 0.0f) return;
    for (uint32_t i = 0; i < mem->cap_conexiones; i++) {
        if (!mem->conexiones[i].used) continue;
        if (mem->conexiones[i].origen_id != id && mem->conexiones[i].destino_id != id) continue;
        float f = mem->conexiones[i].fuerza.f + delta;
        if (f > 1.0f) f = 1.0f;
        mem->conexiones[i].fuerza.f = f;
    }
    if (!mem->es_ram) mem->dirty = 1;
}

void jmn_penalizar_concepto(JMNMemoria* mem, uint32_t id, float delta) {
    if (!mem || id == 0 || delta <= 0.0f) return;
    for (uint32_t i = 0; i < mem->cap_conexiones; i++) {
        if (!mem->conexiones[i].used) continue;
        if (mem->conexiones[i].origen_id != id && mem->conexiones[i].destino_id != id) continue;
        float f = mem->conexiones[i].fuerza.f - delta;
        if (f < 0.0f) f = 0.0f;
        mem->conexiones[i].fuerza.f = f;
    }
    if (!mem->es_ram) mem->dirty = 1;
}
