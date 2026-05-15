#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>

int jmn_buscar_asociaciones(JMNMemoria* mem, uint32_t origen, uint32_t tipo_rel, float umbral,
    uint16_t profundidad, JMNBusquedaResultado* out, uint16_t max_out) {
    if (!mem || !out || max_out == 0) return 0;
    
    uint16_t p = profundidad;
    if (p == 0) p = 1;
    if (p > 5) p = 5; // Limite de seguridad

    // Para profundidad 1, usamos la búsqueda directa rápida
    if (p == 1) {
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

    // Para profundidad > 1, usamos una búsqueda por propagación (BFS simplificado)
    JMNActivacionResultado act_res[64];
    int n_act = jmn_propagar_activacion(mem, origen, 1.0f, 0.8f, umbral, p, tipo_rel, act_res, 64, NULL, 0, NULL);
    
    int n = 0;
    for (int i = 0; i < n_act && n < (int)max_out; i++) {
        // No incluir el origen mismo
        if (act_res[i].id == origen) continue;
        
        out[n].id = act_res[i].id;
        out[n].fuerza = act_res[i].activacion;
        // El tipo de relación es difuso en propagación, ponemos 0 o el tipo solicitado
        out[n].tipo_relacion = tipo_rel; 
        n++;
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

static int jmn_id_en_lista(uint32_t id, const uint32_t* arr, int n) {
    for (int i = 0; i < n; i++)
        if (arr[i] == id) return 1;
    return 0;
}

/** h(d_v) precomputada [0..32]; ver flujo_model_IA/04_h_d.md. Modos: 0=1, 1=λ^d, 2=1/(1+κd), 3=lineal. */
static void jmn_propagar_precompute_h(float H[33], uint16_t d_max, int mode, float lambda, float kappa) {
    int dm = (int)d_max;
    if (dm > 32) dm = 32;
    for (int d = 0; d < 33; d++)
        H[d] = 1.f;
    if (mode <= 0 || mode > 3) return;
    if (mode == 1) {
        if (lambda <= 0.f || lambda > 1.f) lambda = 0.7f;
        H[0] = 1.f;
        float acc = 1.f;
        for (int d = 1; d <= dm && d < 33; d++) {
            acc *= lambda;
            H[d] = acc;
        }
    } else if (mode == 2) {
        if (kappa < 0.f) kappa = 0.f;
        if (kappa == 0.f) kappa = 0.15f;
        for (int d = 0; d <= dm && d < 33; d++)
            H[d] = 1.f / (1.f + kappa * (float)d);
    } else {
        int denom = dm + 1;
        if (denom < 1) denom = 1;
        for (int d = 0; d <= dm && d < 33; d++) {
            float v = 1.f - (float)d / (float)denom;
            H[d] = v > 0.f ? v : 0.f;
        }
    }
    for (int d = dm + 1; d < 33; d++) H[d] = 0.f;
}

static void jmn_propagar_merge_g_env(float g[JMN_RELACION_MAX + 1]) {
    const char* p = getenv("JASBOOT_PROPAGAR_G");
    if (!p || !*p) return;
    int idx = 1;
    while (*p && idx <= JMN_RELACION_MAX) {
        while (*p == ' ' || *p == ',' || *p == ';') p++;
        if (*p == '\0') break;
        char* end = NULL;
        double v = strtod(p, &end);
        if (end == p) break;
        p = end;
        if (v > 0.0 && v <= 10.0) g[idx++] = (float)v;
    }
}

static void jmn_propagar_extra_resolve(const JMNPropagarExtra* opt, JMNPropagarExtra* out) {
    for (int i = 0; i <= JMN_RELACION_MAX; i++) out->g_tau[i] = 1.f;
    out->queue_mode = 0;
    out->score_mode = 0;
    if (!opt) {
        const char* q = getenv("JASBOOT_PROPAGAR_QUEUE");
        if (q && (q[0] == 'd' || q[0] == 'D')) out->queue_mode = 1;
        const char* sc = getenv("JASBOOT_PROPAGAR_SCORE");
        if (sc && (sc[0] == 's' || sc[0] == 'S')) out->score_mode = 1;
    } else {
        *out = *opt;
    }
    jmn_propagar_merge_g_env(out->g_tau);
}

static float jmn_g_mul(const JMNPropagarExtra* ex, uint32_t tau) {
    if (!ex) return 1.f;
    if (tau > JMN_RELACION_MAX) tau = 0;
    float g = ex->g_tau[tau];
    if (g <= 0.f || g > 10.f) g = (ex->g_tau[0] > 0.f && ex->g_tau[0] <= 10.f) ? ex->g_tau[0] : 1.f;
    return g;
}

int jmn_propagar_activacion_semillas(JMNMemoria* mem, const uint32_t* semillas, int n_sem,
    float activacion, float factor, float umbral, uint16_t prof, uint32_t tipo_rel,
    JMNActivacionResultado* out, uint16_t max_out, JMNActivacionRastroFn rastro_fn, void* rastro_ud,
    const JMNPropagarExtra* extra) {
    if (!mem || !out || max_out == 0) return 0;

    JMNPropagarExtra ex0;
    jmn_propagar_extra_resolve(extra, &ex0);
    const JMNPropagarExtra* ex = &ex0;
    const int use_dfs = ex->queue_mode != 0;
    const int sum_mode = ex->score_mode != 0;

    uint32_t sem_u[16];
    int n_sem_u = 0;
    if (!semillas || n_sem < 1) return 0;
    for (int s = 0; s < n_sem && n_sem_u < 16; s++) {
        uint32_t id = semillas[s];
        if (id == 0) continue;
        int dup = 0;
        for (int t = 0; t < n_sem_u; t++) {
            if (sem_u[t] == id) {
                dup = 1;
                break;
            }
        }
        if (!dup) sem_u[n_sem_u++] = id;
    }
    if (n_sem_u == 0) return 0;

    uint16_t max_prof = prof;
    if (max_prof > 32) max_prof = 32;

    float fac = factor;
    if (fac <= 0.f || fac > 1.f) fac = 0.8f;

    float umb = umbral;
    if (umb < 0.f) umb = 0.f;

    if (tipo_rel > JMN_RELACION_MAX) tipo_rel = 0;

    int h_mode = 0;
    const char* hm = getenv("JASBOOT_PROPAGAR_H_MODE");
    if (hm && *hm && hm[0] != '0') h_mode = atoi(hm);
    if (h_mode < 0 || h_mode > 3) h_mode = 0;
    float h_lambda = 0.7f;
    float h_kappa = 0.15f;
    const char* hl = getenv("JASBOOT_PROPAGAR_H_LAMBDA");
    if (hl && *hl) {
        char* end = NULL;
        double v = strtod(hl, &end);
        if (end != hl) h_lambda = (float)v;
    }
    const char* hk = getenv("JASBOOT_PROPAGAR_H_KAPPA");
    if (hk && *hk) {
        char* end = NULL;
        double v = strtod(hk, &end);
        if (end != hk) h_kappa = (float)v;
    }
    float Htab[33];
    jmn_propagar_precompute_h(Htab, max_prof, h_mode, h_lambda, h_kappa);

    uint32_t vid[256];
    float vbest[256];
    uint16_t vdepth[256];
    int vn = 0;

    JmnBfsItem buf[JMN_BFS_Q_CAP];
    size_t qh = 0, qt = 0, sp = 0;
    /* Re-encolar al mejorar na con la misma profundidad (modo legacy): acota coste y evita ciclos ruidosos. */
    int same_depth_requeue_budget = 384;

    if (!use_dfs) {
        for (int s = 0; s < n_sem_u; s++) {
            uint32_t sid = sem_u[s];
            if (rastro_fn) rastro_fn(rastro_ud, sid, activacion);
            if (vn >= 256) break;
            vid[vn] = sid;
            vbest[vn] = activacion;
            vdepth[vn] = 0;
            vn++;
            if (qt < JMN_BFS_Q_CAP)
                buf[qt++] = (JmnBfsItem){ sid, 0, activacion };
        }
    } else {
        for (int s = n_sem_u - 1; s >= 0; s--) {
            uint32_t sid = sem_u[s];
            if (rastro_fn) rastro_fn(rastro_ud, sid, activacion);
            if (vn >= 256) break;
            vid[vn] = sid;
            vbest[vn] = activacion;
            vdepth[vn] = 0;
            vn++;
            if (sp < JMN_BFS_Q_CAP)
                buf[sp++] = (JmnBfsItem){ sid, 0, activacion };
        }
    }

    for (;;) {
        JmnBfsItem cur;
        int has = 0;
        if (!use_dfs) {
            if (qh < qt) {
                cur = buf[qh++];
                has = 1;
            }
        } else {
            if (sp > 0) {
                cur = buf[--sp];
                has = 1;
            }
        }
        if (!has) break;
        if (cur.depth >= max_prof) continue;

        JMNBusquedaResultado res[64];
        int ne = jmn_buscar_asociaciones(mem, cur.id, tipo_rel, 0.01f, 1, res, 64);
        int i0 = 0, i1 = ne, step = 1;
        if (use_dfs) {
            i0 = ne - 1;
            i1 = -1;
            step = -1;
        }
        for (int i = i0; i != i1; i += step) {
            uint16_t nd = (uint16_t)(cur.depth + 1u);
            if (nd > max_prof) continue;
            float hd = (nd < 33) ? Htab[nd] : 0.f;
            uint32_t tau = res[i].tipo_relacion;
            float gtr = jmn_g_mul(ex, tau);
            float na = cur.act * fac * res[i].fuerza * hd * gtr;
            if (na < umb) continue;
            uint32_t nid = res[i].id;
            if (n_sem_u == 1 && nid == sem_u[0]) continue;
            int ix = jmn_vid_index(vid, vn, nid);
            if (ix < 0) {
                if (vn >= 256) continue;
                vid[vn] = nid;
                vbest[vn] = na;
                vdepth[vn] = nd;
                vn++;
                if (rastro_fn) rastro_fn(rastro_ud, nid, na);
                if (use_dfs) {
                    if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                } else {
                    if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                }
            } else if (sum_mode) {
                vbest[ix] += na;
                if (nd < vdepth[ix]) {
                    vdepth[ix] = nd;
                    if (rastro_fn) rastro_fn(rastro_ud, nid, na);
                    if (use_dfs) {
                        if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                    } else {
                        if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                    }
                }
            } else if (nd < vdepth[ix]) {
                vdepth[ix] = nd;
                vbest[ix] = na;
                if (rastro_fn) rastro_fn(rastro_ud, nid, na);
                if (use_dfs) {
                    if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                } else {
                    if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                }
            } else if (nd == vdepth[ix] && na > vbest[ix]) {
                vbest[ix] = na;
                if (rastro_fn) rastro_fn(rastro_ud, nid, na);
                if (!sum_mode && same_depth_requeue_budget > 0) {
                    same_depth_requeue_budget--;
                    if (use_dfs) {
                        if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                    } else {
                        if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                    }
                }
            }
        }
    }

    uint32_t cand_id[256];
    float cand_sc[256];
    int nc = 0;
    for (int i = 0; i < vn; i++) {
        if (jmn_id_en_lista(vid[i], sem_u, n_sem_u)) continue;
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

int jmn_propagar_activacion(JMNMemoria* mem, uint32_t origen, float activacion, float factor,
    float umbral, uint16_t prof, uint32_t tipo_rel, JMNActivacionResultado* out, uint16_t max_out,
    JMNActivacionRastroFn rastro_fn, int reserved, void* rastro_ud) {
    (void)reserved;
    uint32_t one = origen;
    return jmn_propagar_activacion_semillas(mem, &one, 1, activacion, factor, umbral, prof, tipo_rel, out, max_out,
        rastro_fn, rastro_ud, NULL);
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
