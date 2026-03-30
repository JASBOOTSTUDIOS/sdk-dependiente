#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>

int jmn_buscar_asociaciones(JMNMemoria* mem, uint32_t origen, uint32_t tipo_rel, float umbral,
    uint16_t profundidad, JMNBusquedaResultado* out, uint16_t max_out) {
    (void)profundidad;
    if (!mem || !out || max_out == 0) return 0;
    uint32_t bucket = origen % (mem->cap_nodos + 1);
    if (bucket > mem->cap_nodos) bucket = mem->cap_nodos;
    uint32_t slot = mem->cabeza_origen[bucket];
    int n = 0;
    while (slot != 0xFFFFFFFF && n < (int)max_out) {
        if (mem->conexiones[slot].origen_id == origen &&
            mem->conexiones[slot].fuerza.f >= umbral) {
            if (tipo_rel == 0 || mem->conexiones[slot].key_id == tipo_rel) {
                out[n].id = mem->conexiones[slot].destino_id;
                out[n].tipo_relacion = mem->conexiones[slot].key_id;
                out[n].fuerza = mem->conexiones[slot].fuerza.f;
                n++;
            }
        }
        slot = mem->conexiones[slot].next_origen;
    }
    return n;
}

#define JMN_BFS_Q_CAP 384

typedef struct {
    uint32_t id;
    uint16_t depth;
    float act;
} JmnBfsItem;

static int jmn_vid_index(const uint32_t* vid, int vn, uint32_t id) {
    for (int i = 0; i < vn; i++)
        if (vid[i] == id) return i;
    return -1;
}

static void jmn_sort_pairs_desc(uint32_t* ids, float* sc, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (sc[j] > sc[i]) {
                float ts = sc[i];
                sc[i] = sc[j];
                sc[j] = ts;
                uint32_t tid = ids[i];
                ids[i] = ids[j];
                ids[j] = tid;
            }
        }
    }
}

int jmn_propagar_activacion(JMNMemoria* mem, uint32_t origen, float activacion, float factor,
    float umbral, uint16_t prof, uint32_t tipo_rel, JMNActivacionResultado* out, uint16_t max_out,
    JMNActivacionRastroFn rastro_fn, int reserved, void* rastro_ud) {
    (void)reserved;
    if (!mem || !out || max_out == 0) return 0;

    uint16_t max_prof = prof;
    if (max_prof == 0) max_prof = 3;
    if (max_prof > 32) max_prof = 32;

    float fac = factor;
    if (fac <= 0.f || fac > 1.f) fac = 0.8f;

    float umb = umbral;
    if (umb < 0.f) umb = 0.f;

    if (tipo_rel > JMN_RELACION_MAX) tipo_rel = 0;

    uint32_t vid[256];
    float vbest[256];
    int vn = 0;

    JmnBfsItem q[JMN_BFS_Q_CAP];
    size_t qh = 0, qt = 0;

    if (rastro_fn) rastro_fn(rastro_ud, origen, activacion);
    vid[0] = origen;
    vbest[0] = activacion;
    vn = 1;
    q[qt++] = (JmnBfsItem){ origen, 0, activacion };

    while (qh < qt) {
        JmnBfsItem cur = q[qh++];
        if (cur.depth >= max_prof) continue;

        JMNBusquedaResultado res[64];
        int ne = jmn_buscar_asociaciones(mem, cur.id, tipo_rel, 0.01f, 1, res, 64);
        for (int i = 0; i < ne; i++) {
            float na = cur.act * fac * res[i].fuerza;
            if (na < umb) continue;
            uint32_t nid = res[i].id;
            int ix = jmn_vid_index(vid, vn, nid);
            if (ix < 0) {
                if (vn >= 256) continue;
                vid[vn] = nid;
                vbest[vn] = na;
                vn++;
                if (rastro_fn) rastro_fn(rastro_ud, nid, na);
                if (qt < JMN_BFS_Q_CAP)
                    q[qt++] = (JmnBfsItem){ nid, (uint16_t)(cur.depth + 1u), na };
            } else if (na > vbest[ix]) {
                vbest[ix] = na;
            }
        }
    }

    uint32_t cand_id[256];
    float cand_sc[256];
    int nc = 0;
    for (int i = 0; i < vn; i++) {
        if (vid[i] == origen) continue;
        cand_id[nc] = vid[i];
        cand_sc[nc] = vbest[i];
        nc++;
    }
    jmn_sort_pairs_desc(cand_id, cand_sc, nc);

    int nfill = nc < (int)max_out ? nc : (int)max_out;
    for (int i = 0; i < nfill; i++) {
        out[i].id = cand_id[i];
        out[i].activacion = cand_sc[i];
    }
    return nfill;
}

void jmn_resolver_conflictos(JMNMemoria* mem, uint32_t origen, uint32_t tipo_rel, float umbral,
    uint16_t prof, JMNBusquedaResultado* resultados, uint16_t n, float w1, float w2, JMNConflictoResultado* out) {
    (void)mem;
    (void)origen;
    (void)tipo_rel;
    (void)umbral;
    (void)prof;
    (void)w1;
    (void)w2;
    if (out && resultados && n > 0) {
        out->id_ganador = resultados[0].id;
        out->confianza = resultados[0].fuerza;
    }
}
