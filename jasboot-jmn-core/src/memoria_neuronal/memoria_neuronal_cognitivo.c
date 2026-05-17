#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    int n_act = jmn_propagar_activacion(mem, origen, 1.0f, 0.8f, umbral, p, tipo_rel, act_res, 64, NULL, NULL);
    
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

void jmn_propagar_extra_init(JMNPropagarExtra* extra) {
    if (!extra) return;
    for (int i = 0; i <= JMN_RELACION_MAX; i++) {
        extra->g_tau[i] = 1.0f;
        extra->mask_tau[i] = 1.0f;
        extra->alpha_tau[i] = 1.0f;
    }
    extra->queue_mode = 0;
    extra->score_mode = 0;
    extra->inhibition_map_id = 0;
    extra->tau10_reject_threshold = -1e9f; /* Desactivado por defecto */
    extra->tau10_rewrite_threshold = -1e9f;
    extra->mmr_lambda = 1.0f; /* 1.0 = deshabilitado (solo relevancia) */
    extra->mmr_k = 0;
    extra->dmax_dinamico_activado = 0;
    extra->dmax_base = 2;
    extra->dmax_alpha = 0.5f;
    extra->dmax_beta = 1.0f;
    extra->dmax_gamma = 0.3f;
    extra->h_mode = 0;
    extra->h_lambda = 0.7f;
    extra->h_kappa = 0.15f;
    extra->audit_mode = 0;
}

void jmn_propagar_extra_normalizar(JMNPropagarExtra* extra) {
    if (!extra) return;
    const char* ev = getenv("JASBOOT_PROPAGAR_G_NORM");
    int modo_suma = ev && ev[0] && (ev[0] == 's' || ev[0] == 'S');
    if (modo_suma) {
        float s = 0.f;
        for (int i = 1; i <= JMN_RELACION_MAX; i++)
            if (extra->g_tau[i] > 0.f) s += extra->g_tau[i];
        if (s > 1e-12f) {
            for (int i = 1; i <= JMN_RELACION_MAX; i++)
                if (extra->g_tau[i] > 0.f) extra->g_tau[i] /= s;
        }
        return;
    }
    float max_v = 0.0f;
    for (int i = 1; i <= JMN_RELACION_MAX; i++) {
        if (extra->g_tau[i] > max_v) max_v = extra->g_tau[i];
    }
    if (max_v > 0.00001f) {
        for (int i = 1; i <= JMN_RELACION_MAX; i++) {
            extra->g_tau[i] /= max_v;
        }
    }
}

void jmn_propagar_extra_merge(JMNPropagarExtra* dest, const JMNPropagarExtra* src) {
    if (!dest || !src) return;
    for (int i = 0; i <= JMN_RELACION_MAX; i++) {
        /* Solo sobreescribimos si el valor en src es distinto al "defecto absoluto" 1.0 
         * o si queremos forzar un valor específico. En este modelo, P.g_override
         * suele ser ralo. */
        if (src->g_tau[i] != 1.0f) dest->g_tau[i] = src->g_tau[i];
        if (src->mask_tau[i] != 1.0f) dest->mask_tau[i] = src->mask_tau[i];
        if (src->alpha_tau[i] != 1.0f) dest->alpha_tau[i] = src->alpha_tau[i];
    }
    /* El modo de cola y puntuación se hereda si src lo especifica (no 0) */
    if (src->queue_mode != 0) dest->queue_mode = src->queue_mode;
    if (src->score_mode != 0) dest->score_mode = src->score_mode;
    if (src->inhibition_map_id != 0) dest->inhibition_map_id = src->inhibition_map_id;
}

void jmn_propagar_extra_construir(const JMNPropagarExtra* g_default, const JMNPropagarExtra* p_override,
    JMNPropagarExtra* out) {
    if (!out) return;
    if (g_default) {
        *out = *g_default;
    } else {
        jmn_propagar_extra_init(out);
    }
    if (p_override) jmn_propagar_extra_merge(out, p_override);
}

int jmn_propagar_extra_cargar_perfil(JMNPropagarExtra* extra, const char* perfil_nombre) {
    if (!extra || !perfil_nombre) return 0;
    
    /* Perfil 0: Defecto (Todo 1.0) */
    if (strcmp(perfil_nombre, "defecto") == 0 || strcmp(perfil_nombre, "estandar") == 0) {
        jmn_propagar_extra_init(extra);
        return 1;
    }
    
    /* Perfil: Explorador (Asociativo, similitud, causalidad) */
    if (strcmp(perfil_nombre, "explorador") == 0 || strcmp(perfil_nombre, "creativo") == 0) {
        jmn_propagar_extra_init(extra);
        extra->g_tau[JMN_RELACION_ASOCIACION] = 1.0f;
        extra->g_tau[JMN_RELACION_SIMILITUD] = 1.2f;
        extra->g_tau[JMN_RELACION_CAUSALIDAD] = 0.9f;
        extra->g_tau[JMN_RELACION_OPOSICION] = 0.5f;
        extra->g_tau[JMN_RELACION_VALORATIVA] = 0.3f;
        return 1;
    }
    
    /* Perfil: Analítico (Lógico, operadores, magnitudes, condiciones) */
    if (strcmp(perfil_nombre, "analitico") == 0 || strcmp(perfil_nombre, "logico") == 0) {
        jmn_propagar_extra_init(extra);
        extra->g_tau[JMN_RELACION_OPERADOR] = 1.5f;
        extra->g_tau[JMN_RELACION_MEDIDA] = 1.2f;
        extra->g_tau[JMN_RELACION_MAGNITUD] = 1.2f;
        extra->g_tau[JMN_RELACION_CONDICION] = 1.4f;
        extra->g_tau[JMN_RELACION_CUANTIFICACION] = 1.1f;
        extra->g_tau[JMN_RELACION_ASOCIACION] = 0.4f; /* Menos ruido asociativo */
        return 1;
    }
    
    /* Perfil: Secuencial (Paso a paso, temporal, narrativo) */
    if (strcmp(perfil_nombre, "secuencial") == 0 || strcmp(perfil_nombre, "narrativo") == 0) {
        jmn_propagar_extra_init(extra);
        extra->g_tau[JMN_RELACION_SECUENCIA] = 1.6f;
        extra->g_tau[JMN_RELACION_TEMPORALIDAD] = 1.4f;
        extra->g_tau[JMN_RELACION_CONSECUENCIA] = 1.3f;
        extra->g_tau[JMN_RELACION_ACCION] = 1.2f;
        extra->g_tau[JMN_RELACION_COMPLEMENTO] = 1.1f;
        extra->g_tau[JMN_RELACION_SIMILITUD] = 0.5f;
        return 1;
    }

    return 0;
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
        if (v > 0.0 && v <= 10.0) g[idx] = (float)v;
        idx++;
    }
}

static void jmn_propagar_merge_mask_env(float mask[JMN_RELACION_MAX + 1]) {
    const char* p = getenv("JASBOOT_PROPAGAR_MASK");
    if (!p || !*p) return;
    int idx = 1;
    while (*p && idx <= JMN_RELACION_MAX) {
        while (*p == ' ' || *p == ',' || *p == ';') p++;
        if (*p == '\0') break;
        char* end = NULL;
        double v = strtod(p, &end);
        if (end == p) break;
        p = end;
        if (v >= 0.0 && v <= 1.0) mask[idx] = (float)v;
        idx++;
    }
}

static void jmn_propagar_extra_resolve(const JMNPropagarExtra* opt, JMNPropagarExtra* out) {
    if (opt) {
        *out = *opt;
        return;
    }
    
    jmn_propagar_extra_init(out);
    
    /* Cargar desde variables de entorno si no se provee estructura (legacy/override global) */
    const char* q = getenv("JASBOOT_PROPAGAR_QUEUE");
    if (q && (q[0] == 'd' || q[0] == 'D' || q[0] == '1')) out->queue_mode = 1;
    
    const char* s = getenv("JASBOOT_PROPAGAR_SCORE");
    if (s && (s[0] == 's' || s[0] == 'S' || s[0] == '1')) out->score_mode = 1;
    
    jmn_propagar_merge_g_env(out->g_tau);
    jmn_propagar_merge_mask_env(out->mask_tau);

    const char* hm = getenv("JASBOOT_PROPAGAR_H_MODE");
    if (hm && *hm) out->h_mode = atoi(hm);
    
    const char* hl = getenv("JASBOOT_PROPAGAR_H_LAMBDA");
    if (hl && *hl) out->h_lambda = (float)atof(hl);
    
    const char* hk = getenv("JASBOOT_PROPAGAR_H_KAPPA");
    if (hk && *hk) out->h_kappa = (float)atof(hk);

    const char* am = getenv("JASBOOT_PROPAGAR_AUDIT");
    if (am && *am) out->audit_mode = atoi(am);
}

static float jmn_g_mul(const JMNPropagarExtra* ex, uint32_t tau) {
    if (!ex) return 1.f;
    if (tau > JMN_RELACION_MAX) tau = 0;
    float g = ex->g_tau[tau];
    if (g <= 0.f || g > 10.f) g = (ex->g_tau[0] > 0.f && ex->g_tau[0] <= 10.f) ? ex->g_tau[0] : 1.f;
    
    float m = ex->mask_tau[tau];
    if (m < 0.f) m = 0.f; else if (m > 1.f) m = 1.f;
    
    return g * m;
}

float jmn_propagar_factor_arista(const JMNPropagarExtra* ex, uint32_t tau) {
    return jmn_g_mul(ex, tau);
}

int jmn_propagar_activacion_semillas(JMNMemoria* mem, const uint32_t* semillas, int n_sem,
    float activacion, float factor, float umbral, uint16_t prof, uint32_t tipo_rel,
    JMNActivacionResultado* out, uint16_t max_out, JMNActivacionRastroFn rastro_fn, void* rastro_ud,
    const JMNPropagarExtra* extra) {
    if (!mem || !out || max_out == 0 || !semillas || n_sem < 1) return 0;

    JMNPropagarExtra ex0;
    jmn_propagar_extra_resolve(extra, &ex0);
    const JMNPropagarExtra* ex = &ex0;
    const int use_dfs = ex->queue_mode != 0;
    const int sum_mode = ex->score_mode != 0;

    uint32_t sem_u[16];
    int n_sem_u = 0;
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

    int h_mode = ex->h_mode;
    if (h_mode < 0 || h_mode > 3) h_mode = 0;
    float h_lambda = ex->h_lambda;
    float h_kappa = ex->h_kappa;

    float Htab[33];
    jmn_propagar_precompute_h(Htab, max_prof, h_mode, h_lambda, h_kappa);

    if (ex->audit_mode > 0) {
        printf("[AUDIT] Iniciando propagacion: n_sem=%d, activacion=%.2f, max_prof=%d, h_mode=%d\n", 
               n_sem_u, activacion, max_prof, h_mode);
    }

    uint32_t vid[256];
    float vbest[256];
    uint16_t vdepth[256];
    /* Fase 6: Matriz de evidencia multi-canal E[v,τ]. 31 canales (0=default). 
     * Reservamos en el heap para evitar stack overflow en recursiones de JMN. */
    float (*E)[JMN_RELACION_MAX + 1] = (float (*)[JMN_RELACION_MAX + 1])calloc(256, sizeof(float) * (JMN_RELACION_MAX + 1));
    if (!E) return 0;
    int vn = 0;

    JmnBfsItem buf[JMN_BFS_Q_CAP];
    size_t qh = 0, qt = 0, sp = 0;
    /* Re-encolar al mejorar na con la misma profundidad (modo legacy): acota coste y evita ciclos ruidosos. */
    int same_depth_requeue_budget = 384;

    if (!use_dfs) {
        for (int s = 0; s < n_sem_u; s++) {
            uint32_t sid = sem_u[s];
            if (rastro_fn) rastro_fn(rastro_ud, sid, activacion, 0);
            if (vn >= 256) break;
            vid[vn] = sid;
            vbest[vn] = activacion;
            vdepth[vn] = 0;
            /* Las semillas no tienen canal τ de entrada, inicializamos E a 0 */
            vn++;
            if (qt < JMN_BFS_Q_CAP)
                buf[qt++] = (JmnBfsItem){ sid, 0, activacion };
        }
    } else {
        for (int s = n_sem_u - 1; s >= 0; s--) {
            uint32_t sid = sem_u[s];
            if (rastro_fn) rastro_fn(rastro_ud, sid, activacion, 0);
            if (vn >= 256) break;
            vid[vn] = sid;
            vbest[vn] = activacion;
            vdepth[vn] = 0;
            /* Las semillas no tienen canal τ de entrada, inicializamos E a 0 */
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
            /* Fase 6: Separamos g y alpha. Alpha se aplica en la fusión final post-BFS. */
            float na = cur.act * fac * res[i].fuerza * hd * gtr;

            /* Aplicar inhibición si hay un mapa de inhibición configurado (Fase 7/8) */
            if (ex->inhibition_map_id != 0) {
                JMNValor val_inh;
                if (jmn_mapa_obtener_si_existe(mem, ex->inhibition_map_id, res[i].id, &val_inh)) {
                    /* El valor en el mapa se asume como un contador de usos (entero)
                     * o un factor de penalización (flotante < 1.0). 
                     * Como JMNValor no tiene tipo, aplicamos heurística: si > 0 y < 1.0 es float factor. */
                    float factor_inh = 1.0f;
                    if (val_inh.f > 0.0f && val_inh.f < 1.0f) {
                        factor_inh = val_inh.f;
                    } else if (val_inh.u > 0) {
                        factor_inh = 1.0f / (1.0f + (float)val_inh.u * 2.0f);
                    }
                    na *= factor_inh;
                }
            }

            if (ex->audit_mode >= 2) {
                printf("[AUDIT] Arista: u=%u -> v=%u (tau=%u, w=%.2f) | g=%.2f, h=%.2f, act_in=%.2f -> act_out=%.4f\n",
                       cur.id, res[i].id, tau, res[i].fuerza, gtr, hd, cur.act, na);
            }

            if (na < umb) {
                if (ex->audit_mode >= 2) printf("[AUDIT]   (Descartado por umbral %.4f)\n", umb);
                continue;
            }
            uint32_t nid = res[i].id;
            if (n_sem_u == 1 && nid == sem_u[0]) continue;
            int ix = jmn_vid_index(vid, vn, nid);
            if (ix < 0) {
                if (vn >= 256) continue;
                vid[vn] = nid;
                vbest[vn] = na;
                vdepth[vn] = nd;
                /* Acumular evidencia en el canal tau */
                if (tau <= JMN_RELACION_MAX) E[vn][tau] = na;
                vn++;
                if (rastro_fn) rastro_fn(rastro_ud, nid, na, nd);
                if (use_dfs) {
                    if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                } else {
                    if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                }
            } else {
                /* Actualizar evidencia por canal. */
                if (tau <= JMN_RELACION_MAX) {
                    if (sum_mode) E[ix][tau] += na;
                    else if (na > E[ix][tau]) E[ix][tau] = na;
                }

                if (sum_mode) {
                    vbest[ix] += na;
                    if (nd < vdepth[ix]) {
                        vdepth[ix] = nd;
                        if (rastro_fn) rastro_fn(rastro_ud, nid, na, nd);
                        if (use_dfs) {
                            if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                        } else {
                            if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                        }
                    }
                } else if (nd < vdepth[ix]) {
                    vdepth[ix] = nd;
                    vbest[ix] = na;
                    if (rastro_fn) rastro_fn(rastro_ud, nid, na, nd);
                    if (use_dfs) {
                        if (sp < JMN_BFS_Q_CAP) buf[sp++] = (JmnBfsItem){ nid, nd, na };
                    } else {
                        if (qt < JMN_BFS_Q_CAP) buf[qt++] = (JmnBfsItem){ nid, nd, na };
                    }
                } else if (nd == vdepth[ix] && na > vbest[ix]) {
                    vbest[ix] = na;
                    if (rastro_fn) rastro_fn(rastro_ud, nid, na, nd);
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
    }

    /* Fase 6: Fusión final φ(α · E). Implementamos Score(v) = φ(x) con x_τ = α_τ · E[v,τ]. */
    for (int i = 0; i < vn; i++) {
        float fused_score = 0.0f;
        int has_evidence = 0;
        if (sum_mode) {
            /* φ lineal (ponderada): Score(v) = Σ α_τ · E[v,τ] */
            for (int t = 1; t <= JMN_RELACION_MAX; t++) {
                if (E[i][t] > 0.0f) {
                    fused_score += ex->alpha_tau[t] * E[i][t];
                    has_evidence = 1;
                }
            }
        } else {
            /* φ max (canal dominante): Score(v) = max_τ (α_τ · E[v,τ]) */
            for (int t = 1; t <= JMN_RELACION_MAX; t++) {
                if (E[i][t] > 0.0f) {
                    float val = ex->alpha_tau[t] * E[i][t];
                    if (val > fused_score) fused_score = val;
                    has_evidence = 1;
                }
            }
        }
        /* Si hay evidencia por canales, actualizamos vbest con el score fusionado.
         * Las semillas (depth 0) no tienen evidencia por canales y conservan su activación inicial. */
        if (has_evidence) {
            vbest[i] = fused_score;
        }

        /* Fase 7: Política τ=10 (Valorativa). Si la señal valorativa es insuficiente, bloqueamos. */
        if (ex->tau10_reject_threshold > -1e6f) {
            float signal_10 = E[i][10];
            if (signal_10 < ex->tau10_reject_threshold) {
                vbest[i] = -1.0f; /* Marcar como bloqueado */
                if (ex->audit_mode >= 1) printf("[AUDIT] Nodo %u BLOQUEADO por política tau=10 (señal=%.4f < umbral=%.4f)\n", vid[i], signal_10, ex->tau10_reject_threshold);
            }
        }
    }

    free(E);

    /* Filtrar y ordenar candidatos */
    uint32_t cand_id[256];
    float cand_sc[256];
    int nc = 0;
    for (int i = 0; i < vn; i++) {
        if (jmn_id_en_lista(vid[i], sem_u, n_sem_u)) continue;
        if (vbest[i] < 0.0f) continue; /* Bloqueados por política */
        cand_id[nc] = vid[i];
        cand_sc[nc] = vbest[i];
        nc++;
    }
    jmn_sort_pairs_desc(cand_id, cand_sc, nc);

    int n_results = nc;
    JMNActivacionResultado temp_in[256];
    for (int i = 0; i < nc; i++) {
        temp_in[i].id = cand_id[i];
        temp_in[i].activacion = cand_sc[i];
    }

    if (ex->mmr_lambda < 1.0f && ex->mmr_k > 0 && nc > 1) {
        JMNActivacionResultado temp_out[256];
        int k = ex->mmr_k;
        if (k > 256) k = 256;
        n_results = jmn_diversificar_candidatos_mmr(mem, temp_in, nc, ex->mmr_lambda, k, temp_out);
        memcpy(temp_in, temp_out, n_results * sizeof(JMNActivacionResultado));
    }

    int nfill = n_results < (int)max_out ? n_results : (int)max_out;
    for (int i = 0; i < nfill; i++) {
        out[i] = temp_in[i];
    }
    return nfill;
}

int jmn_diversificar_candidatos_mmr(JMNMemoria* mem, JMNActivacionResultado* in, int n_in,
    float lambda, int K, JMNActivacionResultado* out) {
    if (!mem || !in || n_in <= 0 || K <= 0 || !out) return 0;

    int n_sel = 0;
    int remaining = n_in;
    int used[256];
    memset(used, 0, sizeof(used));
    if (n_in > 256) n_in = 256;

    // Paso 0: Normalizar relevancia (asumiendo que in ya viene ordenado por score desc)
    float max_rel = in[0].activacion;
    if (max_rel <= 0.0f) max_rel = 1.0f;

    // Paso 1: El primero es siempre el de mayor score
    out[n_sel] = in[0];
    used[0] = 1;
    n_sel++;
    remaining--;

    // Paso 2: Selección iterativa MMR
    while (n_sel < K && remaining > 0) {
        float best_mmr = -1e9f;
        int best_idx = -1;

        for (int i = 0; i < n_in; i++) {
            if (used[i]) continue;

            float rel = in[i].activacion / max_rel;
            float max_sim = 0.0f;

            // Calcular similitud máxima con los ya seleccionados
            for (int j = 0; j < n_sel; j++) {
                float sim = jmn_similitud_coseno(mem, in[i].id, out[j].id);
                if (sim > max_sim) max_sim = sim;
            }

            float mmr_score = lambda * rel - (1.0f - lambda) * max_sim;
            if (mmr_score > best_mmr) {
                best_mmr = mmr_score;
                best_idx = i;
            }
        }

        if (best_idx >= 0) {
            out[n_sel] = in[best_idx];
            used[best_idx] = 1;
            n_sel++;
            remaining--;
        } else {
            break;
        }
    }

    return n_sel;
}

int jmn_propagar_activacion(JMNMemoria* mem, uint32_t origen, float activacion, float factor,
    float umbral, uint16_t prof, uint32_t tipo_rel, JMNActivacionResultado* out, uint16_t max_out,
    JMNActivacionRastroFn rastro_fn, void* rastro_ud) {
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

int jmn_inferir_relaciones_mil(JMNMemoria* mem, uint32_t origen, uint16_t d_max, 
    const float* factor_delta, JMNInferenciaResultado* out, int max_out) {
    if (!mem || origen == 0 || !out || max_out <= 0) return 0;
    
    /* BFS para encontrar caminos lógicos */
    typedef struct {
        uint32_t id;
        float confianza;
        uint16_t depth;
        uint32_t path[8];
    } QNode;
    
    QNode queue[256];
    int head = 0, tail = 0;
    
    queue[tail].id = origen;
    queue[tail].confianza = 1.0f;
    queue[tail].depth = 0;
    queue[tail].path[0] = origen;
    tail++;
    
    int n_out = 0;

    while (head < tail && head < 256) {
        QNode curr = queue[head++];
        
        if (curr.depth >= d_max || curr.depth >= 7) continue;
        
        /* Explorar conexiones salientes */
        uint32_t bucket = curr.id % (mem->cap_nodos + 1);
        if (bucket > mem->cap_nodos) bucket = mem->cap_nodos;
        uint32_t c_idx = mem->cabeza_origen[bucket];
        
        while (c_idx != 0xFFFFFFFF && c_idx < mem->cap_conexiones) {
            JMNEntradaConexion* c = &mem->conexiones[c_idx];
            if (c->used && c->origen_id == curr.id) {
                uint32_t tau = c->key_id;
                /* Factor delta: por defecto 0.3 si no se especifica. */
                float delta = (factor_delta && tau <= JMN_RELACION_MAX) ? factor_delta[tau] : 0.3f;
                float new_conf = curr.confianza * c->fuerza.f * delta;
                
                if (new_conf > 0.05f) {
                    uint32_t dest = c->destino_id;
                    
                    /* Evitar ciclos en el camino actual */
                    int in_path = 0;
                    for(int p=0; p<=curr.depth; p++) if(curr.path[p] == dest) in_path = 1;
                    
                    if (!in_path) {
                        /* Filtrar Contradicción (Tipo 5) desde el ORIGEN al DESTINO actual */
                        int contradiccion = 0;
                        uint32_t b_neg = origen % (mem->cap_nodos + 1);
                        if (b_neg > mem->cap_nodos) b_neg = mem->cap_nodos;
                        uint32_t c_neg = mem->cabeza_origen[b_neg];
                        while(c_neg != 0xFFFFFFFF && c_neg < mem->cap_conexiones) {
                            JMNEntradaConexion* cn = &mem->conexiones[c_neg];
                            if (cn->used && cn->origen_id == origen && cn->destino_id == dest && cn->key_id == JMN_RELACION_OPOSICION) {
                                if (cn->fuerza.f > new_conf) contradiccion = 1;
                                break;
                            }
                            c_neg = cn->next_origen;
                        }
                        
                        if (!contradiccion) {
                            /* Guardar resultado si es un salto (no el origen) */
                            if (curr.depth >= 1 && n_out < max_out) {
                                /* Comprobar si ya tenemos este destino con mejor confianza */
                                int exists = -1;
                                for(int k=0; k<n_out; k++) if(out[k].id_conclusion == dest) { exists = k; break; }
                                
                                if (exists == -1 || new_conf > out[exists].confianza) {
                                    int target = (exists == -1) ? n_out : exists;
                                    out[target].id_conclusion = dest;
                                    out[target].confianza = new_conf;
                                    out[target].path_len = curr.depth + 2;
                                    memcpy(out[target].path, curr.path, (curr.depth + 1) * sizeof(uint32_t));
                                    out[target].path[curr.depth + 1] = dest;
                                    if (exists == -1) n_out++;
                                }
                            }
                            
                            /* Seguir explorando (si hay espacio en cola) */
                            if (tail < 256) {
                                queue[tail].id = dest;
                                queue[tail].confianza = new_conf;
                                queue[tail].depth = curr.depth + 1;
                                memcpy(queue[tail].path, curr.path, (curr.depth + 1) * sizeof(uint32_t));
                                queue[tail].path[curr.depth + 1] = dest;
                                tail++;
                            }
                        }
                    }
                }
            }
            c_idx = c->next_origen;
        }
    }
    
    return n_out;
}

void jmn_asociar_relacion_efimera(JMNMemoria* mem, uint32_t id_a, uint32_t id_b, uint32_t tipo, float fuerza) {
    if (!mem || !mem->conexiones_efimeras) return;
    if (mem->num_conexiones_efimeras >= mem->cap_conexiones_efimeras) return;

    uint32_t idx = mem->num_conexiones_efimeras++;
    JMNEntradaConexion* c = &mem->conexiones_efimeras[idx];
    c->origen_id = id_a;
    c->destino_id = id_b;
    c->key_id = tipo;
    c->fuerza.f = fuerza;
    c->used = 1;
}

float jmn_evaluar_metacognicion(JMNMemoria* mem, const uint32_t* nodos, int num_nodos, const float* pesos_objetivo) {
    if (!mem || !nodos || num_nodos <= 0) return 0.0f;

    /* Pesos por defecto si no se proveen (Coherencia, Seguridad, Concisión) */
    float w_coh = pesos_objetivo ? pesos_objetivo[0] : 0.4f;
    float w_safe = pesos_objetivo ? pesos_objetivo[1] : 0.4f;
    float w_len = pesos_objetivo ? pesos_objetivo[2] : 0.2f;

    float e_coh = 0.0f;
    float e_safe = 1.0f;
    float e_len = 1.0f;

    /* 1. Coherencia Interna: ¿Están los nodos relacionados entre sí? (Incluye efímeras) */
    float sum_rel = 0.0f;
    int count_rel = 0;
    for (int i = 0; i < num_nodos; i++) {
        for (int j = 0; j < num_nodos; j++) {
            if (i == j) continue;
            
            /* A. Buscar en conexiones persistentes */
            uint32_t b = nodos[i] % (mem->cap_nodos + 1);
            if (mem->cabeza_origen) {
                uint32_t c_idx = mem->cabeza_origen[b];
                while (c_idx != 0xFFFFFFFF && c_idx < mem->cap_conexiones) {
                    JMNEntradaConexion* c = &mem->conexiones[c_idx];
                    if (c->used && c->origen_id == nodos[i] && c->destino_id == nodos[j]) {
                        sum_rel += c->fuerza.f;
                        count_rel++;
                    }
                    c_idx = c->next_origen;
                }
            }
            
            /* B. Buscar en conexiones efímeras (Working Memory) */
            if (mem->conexiones_efimeras) {
                for (uint32_t k = 0; k < mem->num_conexiones_efimeras; k++) {
                    JMNEntradaConexion* ce = &mem->conexiones_efimeras[k];
                    if (ce->used && ce->origen_id == nodos[i] && ce->destino_id == nodos[j]) {
                        sum_rel += ce->fuerza.f;
                        count_rel++;
                    }
                }
            }
        }
    }
    if (num_nodos > 1) {
        /* Normalización: Relaciones encontradas vs relaciones posibles (N*N-1) */
        float max_posible = (float)(num_nodos * (num_nodos - 1));
        e_coh = (sum_rel / (max_posible + 1.0f)) * 2.0f; /* Factor 2.0 para compensar raleidad */
        if (e_coh > 1.0f) e_coh = 1.0f;
    } else {
        e_coh = 1.0f;
    }

    /* 2. Seguridad: Detectar sentimientos negativos o juicios de valor extremos (tau=10) */
    float max_unsafe = 0.0f;
    for (int i = 0; i < num_nodos; i++) {
        /* Buscar en persistentes */
        uint32_t b = nodos[i] % (mem->cap_nodos + 1);
        if (mem->cabeza_origen) {
            uint32_t c_idx = mem->cabeza_origen[b];
            while (c_idx != 0xFFFFFFFF && c_idx < mem->cap_conexiones) {
                JMNEntradaConexion* c = &mem->conexiones[c_idx];
                if (c->used && c->origen_id == nodos[i] && c->key_id == 10) {
                    if (c->fuerza.f > max_unsafe) max_unsafe = c->fuerza.f;
                }
                c_idx = c->next_origen;
            }
        }
        /* Buscar en efímeras */
        if (mem->conexiones_efimeras) {
            for (uint32_t k = 0; k < mem->num_conexiones_efimeras; k++) {
                JMNEntradaConexion* ce = &mem->conexiones_efimeras[k];
                if (ce->used && ce->origen_id == nodos[i] && ce->key_id == 10) {
                    if (ce->fuerza.f > max_unsafe) max_unsafe = ce->fuerza.f;
                }
            }
        }
    }
    e_safe = 1.0f - max_unsafe;

    /* 3. Concisión: Penalizar exceso de tokens */
    if (num_nodos > 20) e_len = 0.5f;
    else if (num_nodos > 12) e_len = 0.8f;
    else if (num_nodos < 2) e_len = 0.6f;

    float score = (w_coh * e_coh) + (w_safe * e_safe) + (w_len * e_len);
    return score;
}
