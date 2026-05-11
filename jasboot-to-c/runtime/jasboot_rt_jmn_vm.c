/**
 * JMN AOT: misma pila que la VM (jasboot-jmn-core + hashes de texto tipo djb2/5381, case-sensitive).
 */
#include "jasboot_rt_jmn_vm.h"
#include "jasboot_rt.h"
#include "memoria_neuronal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JMNMemoria *jb_neu;
static JMNMemoria *jb_col;
static char jb_path[1024];
static uint32_t jb_eph_seq;

struct JMNMemoria *jb_jmn_rt_mem(void) { return jb_neu; }

void jb_jmn_vm_startup(void) {
    jb_neu = NULL;
    jb_col = NULL;
    jb_path[0] = 0;
    jb_eph_seq = 0;
}

void jb_jmn_vm_shutdown(void) {
    if (jb_neu) {
        jmn_finalizar_escritura(jb_neu);
        jmn_cerrar(jb_neu);
        jb_neu = NULL;
    }
    if (jb_col) {
        jmn_cerrar(jb_col);
        jb_col = NULL;
    }
    jb_path[0] = 0;
}

static void jb_ensure_col(void) {
    if (!jb_col)
        jb_col = jmn_crear_memoria_ram(100000u, 1000000u);
}

static uint32_t jb_vm_hash_texto(const char *texto) {
    uint32_t hash = 5381u;
    if (!texto)
        return 0;
    for (const unsigned char *p = (const unsigned char *)texto; *p; p++)
        hash = ((hash << 5) + hash) + *p;
    return hash;
}

static uint32_t jb_var_to_concept_id(jb_var_t v) {
    jb_var_t kt = jb_jmn_key_as_text(v);
    const char *s = (kt.type == JB_TYPE_TEXTO && kt.u.str) ? kt.u.str : "";
    uint32_t id = jb_vm_hash_texto(s);
    if (id == 0u)
        id = 5381u;
    if (jb_neu && s[0])
        (void)jmn_guardar_texto(jb_neu, id, s);
    jb_var_clear(&kt);
    return id;
}

static jb_var_t jb_id_to_texto_var(uint32_t id) {
    char buf[512];
    if (!jb_neu || jmn_obtener_texto(jb_neu, id, buf, sizeof buf) < 0 || !buf[0])
        return jb_new_texto("");
    return jb_new_texto(buf);
}

static uint32_t jb_alloc_ephemeral_list_id(void) {
    jb_eph_seq++;
    return 0xE1000000u | (jb_eph_seq & 0x00FFFFFFu);
}

static void jb_jb_list_to_col(uint32_t list_id, jb_list_t *L) {
    size_t i;
    jmn_crear_lista(jb_col, list_id);
    jmn_vector_limpiar(jb_col, list_id);
    if (!L)
        return;
    for (i = 0; i < L->len; i++) {
        uint32_t cid = jb_var_to_concept_id(L->items[i]);
        JMNValor vv;
        vv.u = cid;
        jmn_lista_agregar(jb_col, list_id, vv);
    }
}

typedef struct {
    uint32_t target_id;
    uint32_t found_id;
    uint32_t tipo_rel;
    JMNMemoria *mem;
} JMNBusquedaPadreCtx;

static int jmn_callback_buscar_padre_secuencia(JMNNodo *nodo, void *user_data) {
    JMNBusquedaPadreCtx *ctx = (JMNBusquedaPadreCtx *)user_data;
    uint32_t count = 0;
    JMNConexion *conns = jmn_obtener_conexiones(ctx->mem, nodo, &count);
    uint32_t i;
    for (i = 0; i < count; i++) {
        if (conns[i].destino_id == ctx->target_id && conns[i].key_id == ctx->tipo_rel) {
            ctx->found_id = nodo->id;
            return 1;
        }
    }
    return 0;
}

static void jb_exec_op_str_asociar_secuencia(uint32_t id_a, uint32_t id_b, int a_es_lista, uint32_t a_count,
                                            JMNMemoria *a_mem_src, int b_es_lista, uint32_t b_count,
                                            JMNMemoria *b_mem_src) {
    JMNMemoria *mem_neu = jb_neu;
    JMNValor v_uno;
    v_uno.f = 1.0f;
    if (!mem_neu)
        return;

    if (id_a != 0 && !jmn_obtener_nodo(mem_neu, id_a))
        jmn_agregar_nodo(mem_neu, id_a, v_uno);
    if (id_b != 0 && !jmn_obtener_nodo(mem_neu, id_b))
        jmn_agregar_nodo(mem_neu, id_b, v_uno);

    if (id_a == 0 && b_es_lista) {
        uint32_t i;
        for (i = 0; i + 1 < b_count; i++) {
            uint32_t cur = jmn_lista_obtener(b_mem_src, id_b, i).u;
            uint32_t nxt = jmn_lista_obtener(b_mem_src, id_b, i + 1).u;
            if (!jmn_obtener_nodo(mem_neu, cur))
                jmn_agregar_nodo(mem_neu, cur, v_uno);
            if (!jmn_obtener_nodo(mem_neu, nxt))
                jmn_agregar_nodo(mem_neu, nxt, v_uno);
            jmn_agregar_conexion(mem_neu, cur, nxt, v_uno, JMN_RELACION_SECUENCIA);
        }
    } else if (!a_es_lista && b_es_lista) {
        uint32_t i;
        for (i = 0; i < b_count; i++) {
            uint32_t item = jmn_lista_obtener(b_mem_src, id_b, i).u;
            if (!jmn_obtener_nodo(mem_neu, item))
                jmn_agregar_nodo(mem_neu, item, v_uno);
            jmn_agregar_conexion(mem_neu, id_a, item, v_uno, JMN_RELACION_PATRON);
            if (i < b_count - 1) {
                uint32_t nxt = jmn_lista_obtener(b_mem_src, id_b, i + 1).u;
                if (!jmn_obtener_nodo(mem_neu, nxt))
                    jmn_agregar_nodo(mem_neu, nxt, v_uno);
                jmn_agregar_conexion(mem_neu, item, nxt, v_uno, JMN_RELACION_SECUENCIA);
                {
                    uint32_t rel_sec = jmn_relacion_con_contexto(JMN_RELACION_SECUENCIA, id_a);
                    jmn_agregar_conexion(mem_neu, item, nxt, v_uno, rel_sec);
                }
            }
        }
    } else if (a_es_lista && !b_es_lista) {
        uint32_t last_item = jmn_lista_obtener(a_mem_src, id_a, a_count - 1).u;
        if (!jmn_obtener_nodo(mem_neu, last_item))
            jmn_agregar_nodo(mem_neu, last_item, v_uno);
        jmn_agregar_conexion(mem_neu, last_item, id_b, v_uno, JMN_RELACION_SECUENCIA);
        jmn_agregar_conexion(mem_neu, id_a, id_b, v_uno, JMN_RELACION_PATRON);
    } else if (a_es_lista && b_es_lista) {
        uint32_t last_a = jmn_lista_obtener(a_mem_src, id_a, a_count - 1).u;
        uint32_t first_b = jmn_lista_obtener(b_mem_src, id_b, 0).u;
        if (!jmn_obtener_nodo(mem_neu, last_a))
            jmn_agregar_nodo(mem_neu, last_a, v_uno);
        if (!jmn_obtener_nodo(mem_neu, first_b))
            jmn_agregar_nodo(mem_neu, first_b, v_uno);
        jmn_agregar_conexion(mem_neu, last_a, first_b, v_uno, JMN_RELACION_SECUENCIA);
        jmn_agregar_conexion(mem_neu, id_a, id_b, v_uno, JMN_RELACION_PATRON);
    }
}

jb_var_t jb_crear_memoria(jb_var_t path) {
    if (path.type != JB_TYPE_TEXTO || !path.u.str || !path.u.str[0])
        return jb_new_bool(0);
    jb_jmn_vm_shutdown();
    snprintf(jb_path, sizeof jb_path, "%s", path.u.str);
    jb_neu = jmn_abrir_escritura(jb_path);
    if (!jb_neu)
        return jb_new_bool(0);
    jb_ensure_col();
    return jb_new_bool(1);
}

jb_var_t jb_abrir_memoria(jb_var_t path) { return jb_crear_memoria(path); }

void jb_consolidar_memoria(void) {
    if (!jb_neu)
        return;
    jmn_consolidar_memoria_sueno(jb_neu, 0.05f, 1, 0.01f, 0.05f);
    jmn_finalizar_escritura(jb_neu);
}

void jb_cerrar_memoria(void) {
    jb_consolidar_memoria();
    jb_jmn_vm_shutdown();
}

jb_var_t jb_aprender_concepto(jb_var_t concepto, jb_var_t peso) {
    uint32_t id = jb_var_to_concept_id(concepto);
    float w = (float)jb_jmn_clamp01(jb_jmn_as_f64(peso, 0.1));
    if (jb_neu) {
        JMNValor vp;
        vp.f = w;
        jmn_aprender_nodo(jb_neu, id, vp);
    }
    return jb_new_bool(1);
}

jb_var_t jb_recordar(jb_var_t key, jb_var_t val) {
    uint32_t id_k = jb_var_to_concept_id(key);
    uint32_t id_v = jb_var_to_concept_id(val);
    JMNValor v_media;
    JMNValor v_w;
    v_media.f = 0.5f;
    v_w.f = 0.9f;
    if (!jb_neu)
        return jb_new_nulo();
    if (!jmn_obtener_nodo(jb_neu, id_k))
        jmn_agregar_nodo(jb_neu, id_k, v_media);
    if (!jmn_obtener_nodo(jb_neu, id_v))
        jmn_agregar_nodo(jb_neu, id_v, v_media);
    jmn_agregar_conexion(jb_neu, id_k, id_v, v_w, 1u);
    jb_jmn_set_resultado(jb_var_clone(val));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_define_concepto(jb_var_t concepto, jb_var_t descripcion) {
    (void)descripcion;
    return jb_recordar(concepto, jb_var_clone(concepto));
}

jb_var_t jb_buscar(jb_var_t key) {
    uint32_t key_id = jb_var_to_concept_id(key);
    JMNBusquedaResultado res[32];
    int n;
    uint32_t chosen = 5381u;
    if (!jb_neu) {
        jb_jmn_set_resultado(jb_new_texto(""));
        return jb_var_clone(jb_resultado_global);
    }
    if (!jmn_obtener_nodo(jb_neu, key_id)) {
        char trymsg[512];
        jb_var_t kt = jb_jmn_key_as_text(key);
        const char *key_txt = (kt.type == JB_TYPE_TEXTO && kt.u.str) ? kt.u.str : "";
        if (key_txt[0])
            snprintf(trymsg, sizeof trymsg, "buscar: clave inexistente `%s`.", key_txt);
        else
            snprintf(trymsg, sizeof trymsg, "buscar: clave inexistente (id %u).", (unsigned)key_id);
        jb_var_clear(&kt);
        jb_throw_val(jb_new_texto(trymsg));
    }
    n = jmn_buscar_asociaciones(jb_neu, key_id, 1u, 0.01f, 1, res, 32);
    if (n > 0) {
        int i;
        for (i = 0; i < n; i++) {
            uint32_t cand = res[i].id;
            char buf_txt[512];
            if (cand == 0 || cand == key_id)
                continue;
            if (jmn_obtener_texto(jb_neu, cand, buf_txt, sizeof buf_txt) >= 0 && buf_txt[0]) {
                chosen = cand;
                break;
            }
        }
    }
    {
        jb_var_t out = jb_id_to_texto_var(chosen);
        jb_jmn_set_resultado(out);
        jb_var_clear(&out);
    }
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_asociar(jb_var_t origen, jb_var_t destino, jb_var_t fuerza) {
    uint32_t id1 = jb_var_to_concept_id(origen);
    uint32_t id2 = jb_var_to_concept_id(destino);
    float w = (float)jb_jmn_clamp01(jb_jmn_as_f64(fuerza, 1.0));
    JMNValor v_media;
    JMNValor v_w;
    v_media.f = 0.5f;
    v_w.f = w;
    if (!jb_neu)
        return jb_new_nulo();
    if (!jmn_obtener_nodo(jb_neu, id1))
        jmn_agregar_nodo(jb_neu, id1, v_media);
    if (!jmn_obtener_nodo(jb_neu, id2))
        jmn_agregar_nodo(jb_neu, id2, v_media);
    jmn_agregar_conexion(jb_neu, id1, id2, v_w, 1u);
    jb_jmn_set_resultado(jb_new_flotante_scalar((double)w));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_asociar_secuencia_solo(jb_var_t lista) {
    uint32_t id_b;
    uint32_t b_count = 0;
    JMNMemoria *b_mem = NULL;
    if (!jb_neu)
        return jb_new_bool(0);
    jb_ensure_col();
    if (lista.type != JB_TYPE_LIST || !lista.u.lst)
        return jb_new_bool(0);
    id_b = jb_alloc_ephemeral_list_id();
    jb_jb_list_to_col(id_b, lista.u.lst);
    b_count = jmn_lista_tamano(jb_col, id_b);
    b_mem = jb_col;
    jb_exec_op_str_asociar_secuencia(0u, id_b, 0, 0, NULL, 1, b_count, b_mem);
    jb_jmn_set_resultado(jb_new_bool(1));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_asociar_secuencia(jb_var_t a, jb_var_t b) {
    uint32_t id_a = 0, id_b = 0;
    int a_es = 0, b_es = 0;
    uint32_t a_cnt = 0, b_cnt = 0;
    JMNMemoria *a_ms = NULL, *b_ms = NULL;
    if (!jb_neu)
        return jb_new_bool(0);
    jb_ensure_col();

    if (a.type == JB_TYPE_TEXTO && b.type == JB_TYPE_LIST && b.u.lst) {
        id_a = jb_var_to_concept_id(a);
        id_b = jb_alloc_ephemeral_list_id();
        jb_jb_list_to_col(id_b, b.u.lst);
        b_es = 1;
        b_cnt = jmn_lista_tamano(jb_col, id_b);
        b_ms = jb_col;
        jb_exec_op_str_asociar_secuencia(id_a, id_b, 0, 0, NULL, 1, b_cnt, b_ms);
        jb_jmn_set_resultado(jb_new_bool(1));
        return jb_var_clone(jb_resultado_global);
    }
    if (a.type == JB_TYPE_LIST && a.u.lst && b.type == JB_TYPE_LIST && b.u.lst) {
        id_a = jb_alloc_ephemeral_list_id();
        id_b = jb_alloc_ephemeral_list_id();
        jb_jb_list_to_col(id_a, a.u.lst);
        jb_jb_list_to_col(id_b, b.u.lst);
        a_es = 1;
        b_es = 1;
        a_cnt = jmn_lista_tamano(jb_col, id_a);
        b_cnt = jmn_lista_tamano(jb_col, id_b);
        a_ms = jb_col;
        b_ms = jb_col;
        jb_exec_op_str_asociar_secuencia(id_a, id_b, a_es, a_cnt, a_ms, b_es, b_cnt, b_ms);
        jb_jmn_set_resultado(jb_new_bool(1));
        return jb_var_clone(jb_resultado_global);
    }
    if (a.type == JB_TYPE_LIST && a.u.lst && b.type == JB_TYPE_TEXTO) {
        id_a = jb_alloc_ephemeral_list_id();
        jb_jb_list_to_col(id_a, a.u.lst);
        id_b = jb_var_to_concept_id(b);
        a_es = 1;
        a_cnt = jmn_lista_tamano(jb_col, id_a);
        a_ms = jb_col;
        jb_exec_op_str_asociar_secuencia(id_a, id_b, a_es, a_cnt, a_ms, 0, 0, NULL);
        jb_jmn_set_resultado(jb_new_bool(1));
        return jb_var_clone(jb_resultado_global);
    }
    jb_jmn_set_resultado(jb_new_bool(0));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_obtener_secuencia(jb_var_t contexto) {
    uint32_t context_id = jb_var_to_concept_id(contexto);
    jb_var_t out = jb_new_list();
    uint32_t lista_id = 0;
    if (jb_neu && context_id != 0) {
        JMNMemoria *mem = jb_neu;
        JMNBusquedaResultado asoc[64];
        int n_as = jmn_buscar_asociaciones(mem, context_id, JMN_RELACION_PATRON, 0.01f, 1, asoc, 64);
        uint32_t inicio = 0;
        uint32_t tipo_sec_ctx = jmn_relacion_con_contexto(JMN_RELACION_SECUENCIA, context_id);
        int i;
        for (i = 0; i < n_as; i++) {
            JMNBusquedaPadreCtx pctx;
            pctx.target_id = asoc[i].id;
            pctx.found_id = 0;
            pctx.tipo_rel = tipo_sec_ctx;
            pctx.mem = mem;
            jmn_iterar_nodos(mem, jmn_callback_buscar_padre_secuencia, &pctx);
            if (pctx.found_id == 0) {
                inicio = asoc[i].id;
                break;
            }
        }
        if (inicio != 0) {
            lista_id = 0x5EC00000u | (context_id & 0xFFFFFFu);
            jmn_crear_lista(mem, lista_id);
            jmn_vector_limpiar(mem, lista_id);
            {
                uint32_t actual = inicio;
                int safety = 0;
                while (actual != 0 && safety < 100) {
                    JMNValor v;
                    v.u = actual;
                    jmn_lista_agregar(mem, lista_id, v);
                    {
                        JMNBusquedaResultado res_nxt[1];
                        int found = jmn_buscar_asociaciones(mem, actual, tipo_sec_ctx, 0.01f, 1, res_nxt, 1);
                        if (found > 0)
                            actual = res_nxt[0].id;
                        else
                            actual = 0;
                    }
                    safety++;
                }
            }
        }
    }
    if (lista_id != 0 && jb_neu && jmn_lista_existe(jb_neu, lista_id)) {
        uint32_t tam = jmn_lista_tamano(jb_neu, lista_id);
        uint32_t j;
        for (j = 0; j < tam; j++) {
            uint32_t cid = jmn_lista_obtener(jb_neu, lista_id, j).u;
            jb_list_push(&out, jb_id_to_texto_var(cid));
        }
    }
    jb_jmn_set_resultado(jb_var_clone(out));
    jb_var_clear(&out);
    return jb_var_clone(jb_resultado_global);
}

static jb_var_t jb_pensar_siguiente_impl(uint32_t paso_id, uint32_t context_id) {
    JMNBusquedaResultado resultados[8];
    int n = 0;
    uint32_t out_id = 0;
    uint32_t tipo = JMN_RELACION_SECUENCIA;
    if (!jb_neu)
        return jb_new_texto("");
    if (context_id != 0)
        tipo = jmn_relacion_con_contexto(JMN_RELACION_SECUENCIA, context_id);
    n = jmn_buscar_asociaciones(jb_neu, paso_id, tipo, 0.01f, 1, resultados, 8);
    if (n > 1) {
        int i, j;
        for (i = 0; i < n - 1; i++) {
            for (j = i + 1; j < n; j++) {
                if (resultados[j].fuerza > resultados[i].fuerza) {
                    JMNBusquedaResultado temp = resultados[i];
                    resultados[i] = resultados[j];
                    resultados[j] = temp;
                }
            }
        }
    }
    if (n <= 0 && jb_col)
        n = jmn_buscar_asociaciones(jb_col, paso_id, tipo, 0.01f, 1, resultados, 8);
    if (n > 0)
        out_id = resultados[0].id;
    return jb_id_to_texto_var(out_id);
}

jb_var_t jb_pensar_siguiente(jb_var_t paso, jb_var_t contexto_opt) {
    jb_var_t out;
    uint32_t pid = jb_var_to_concept_id(paso);
    if (contexto_opt.type != JB_TYPE_NULL)
        out = jb_pensar_siguiente_impl(pid, jb_var_to_concept_id(contexto_opt));
    else
        out = jb_pensar_siguiente_impl(pid, 0u);
    jb_jmn_set_resultado(out);
    jb_var_clear(&out);
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_pensar_anterior(jb_var_t paso, jb_var_t contexto_opt) {
    uint32_t tipo = JMN_RELACION_SECUENCIA;
    JMNBusquedaPadreCtx ctx;
    jb_var_t out;
    uint32_t pid = jb_var_to_concept_id(paso);
    if (!jb_neu)
        return jb_new_texto("");
    if (contexto_opt.type != JB_TYPE_NULL)
        tipo = jmn_relacion_con_contexto(JMN_RELACION_SECUENCIA, jb_var_to_concept_id(contexto_opt));
    ctx.target_id = pid;
    ctx.found_id = 0;
    ctx.tipo_rel = tipo;
    ctx.mem = jb_neu;
    jmn_iterar_nodos(jb_neu, jmn_callback_buscar_padre_secuencia, &ctx);
    out = jb_id_to_texto_var(ctx.found_id);
    jb_jmn_set_resultado(out);
    jb_var_clear(&out);
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_corregir_secuencia(jb_var_t anterior, jb_var_t incorrecto, jb_var_t correcto) {
    uint32_t id_ant = jb_var_to_concept_id(anterior);
    uint32_t id_inc = jb_var_to_concept_id(incorrecto);
    uint32_t id_cor = jb_var_to_concept_id(correcto);
    JMNValor v_cor;
    v_cor.f = 1.0f;
    if (jb_neu) {
        /* Anular la transición errónea y fijar la correcta en 1.0 (evita empate 0.5/0.5 en pensar_siguiente). */
        float w_bad = jmn_obtener_fuerza_asociacion(jb_neu, id_ant, id_inc);
        if (w_bad > 0.f)
            jmn_penalizar_asociacion(jb_neu, id_ant, id_inc, w_bad);
        jmn_agregar_conexion(jb_neu, id_ant, id_cor, v_cor, JMN_RELACION_SECUENCIA);
    }
    jb_jmn_set_resultado(jb_new_bool(1));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_reforzar(jb_var_t origen, jb_var_t destino, jb_var_t delta) {
    uint32_t a = jb_var_to_concept_id(origen);
    uint32_t b = jb_var_to_concept_id(destino);
    float d = (float)jb_jmn_as_f64(delta, 0.1);
    JMNValor add;
    float w = 0.f;
    add.f = d;
    if (jb_neu)
        jmn_agregar_conexion(jb_neu, a, b, add, 0u);
    if (jb_neu)
        w = jmn_obtener_fuerza_asociacion(jb_neu, a, b);
    jb_jmn_set_resultado(jb_new_flotante_scalar((double)w));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_penalizar(jb_var_t origen, jb_var_t destino, jb_var_t delta) {
    uint32_t a = jb_var_to_concept_id(origen);
    uint32_t b = jb_var_to_concept_id(destino);
    float d = (float)jb_jmn_as_f64(delta, 0.1);
    if (jb_neu)
        jmn_penalizar_asociacion(jb_neu, a, b, d);
    jb_jmn_set_resultado(jb_new_flotante_scalar((double)jmn_obtener_fuerza_asociacion(jb_neu, a, b)));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_buscar_asociados(jb_var_t origen, jb_var_t min_peso) {
    uint32_t oid = jb_var_to_concept_id(origen);
    JMNBusquedaResultado res[64];
    int n, i;
    float th = (float)jb_jmn_clamp01(jb_jmn_as_f64(min_peso, 0.0));
    jb_var_t out = jb_new_list();
    if (!jb_neu) {
        jb_jmn_set_resultado(jb_var_clone(out));
        jb_var_clear(&out);
        return jb_var_clone(jb_resultado_global);
    }
    n = jmn_buscar_asociaciones(jb_neu, oid, 0u, th, 1, res, 64);
    for (i = 0; i < n; i++) {
        jb_list_push(&out, jb_id_to_texto_var(res[i].id));
    }
    jb_jmn_set_resultado(out);
    jb_var_clear(&out);
    return jb_var_clone(jb_resultado_global);
}

static void jb_sort_busqueda_desc(JMNBusquedaResultado *res, int n) {
    int i, j;
    if (n <= 1)
        return;
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            if (res[j].fuerza > res[i].fuerza) {
                JMNBusquedaResultado t = res[i];
                res[i] = res[j];
                res[j] = t;
            }
        }
    }
}

jb_var_t jb_buscar_asociados_lista(jb_var_t origen, jb_var_t k, jb_var_t tipo_rel_opt) {
    uint32_t oid = jb_var_to_concept_id(origen);
    int K = (int)(jb_jmn_as_f64(k, 16.0) + 0.5);
    uint32_t tipo_rel;
    JMNBusquedaResultado res[64];
    int n, i;
    jb_var_t out = jb_new_list();

    if (K < 1)
        K = 1;
    if (K > 64)
        K = 64;
    if (tipo_rel_opt.type == JB_TYPE_NULL)
        tipo_rel = 1u;
    else if (tipo_rel_opt.type == JB_TYPE_ENTERO)
        tipo_rel = (uint32_t)tipo_rel_opt.u.i64;
    else
        tipo_rel = (uint32_t)(jb_jmn_as_f64(tipo_rel_opt, 1.0) + 0.5);
    if (tipo_rel > JMN_RELACION_MAX)
        tipo_rel = 0u;

    if (!jb_neu) {
        jb_jmn_set_resultado(jb_var_clone(out));
        jb_var_clear(&out);
        return jb_var_clone(jb_resultado_global);
    }

    n = jmn_buscar_asociaciones(jb_neu, oid, tipo_rel, 0.01f, 1, res, (uint16_t)K);
    jb_sort_busqueda_desc(res, n);
    for (i = 0; i < n; i++)
        jb_list_push(&out, jb_id_to_texto_var(res[i].id));
    jb_jmn_set_resultado(out);
    jb_var_clear(&out);
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_buscar_asociados_rango(jb_var_t lista_conceptos, jb_var_t min_p, jb_var_t max_p) {
    float min_pf = (float)jb_jmn_clamp01(jb_jmn_as_f64(min_p, 0.0));
    float max_pf = (float)jb_jmn_clamp01(jb_jmn_as_f64(max_p, 1.0));
    jb_var_t map_out = jb_new_map();
    size_t li;

    if (max_pf == 0.0f)
        max_pf = 1.0f;
    if (!jb_neu || lista_conceptos.type != JB_TYPE_LIST || !lista_conceptos.u.lst) {
        jb_jmn_set_resultado(map_out);
        jb_var_clear(&map_out);
        return jb_var_clone(jb_resultado_global);
    }

    for (li = 0; li < lista_conceptos.u.lst->len; li++) {
        jb_var_t item = lista_conceptos.u.lst->items[li];
        uint32_t concept_id = jb_var_to_concept_id(item);
        JMNBusquedaResultado res[128];
        int n_asoc, j, count;
        jb_var_t sub = jb_new_list();

        if (concept_id == 0u) {
            jb_var_clear(&sub);
            continue;
        }

        n_asoc = jmn_buscar_asociaciones(jb_neu, concept_id, 0u, min_pf - 0.005f, 1, res, 128);
        if (n_asoc <= 1)
            /* ok */;
        else {
            int i_sort, j_sort;
            for (i_sort = 0; i_sort < n_asoc - 1; i_sort++) {
                for (j_sort = 0; j_sort < n_asoc - i_sort - 1; j_sort++) {
                    if (res[j_sort].fuerza < res[j_sort + 1].fuerza) {
                        JMNBusquedaResultado tmp = res[j_sort];
                        res[j_sort] = res[j_sort + 1];
                        res[j_sort + 1] = tmp;
                    }
                }
            }
        }

        count = 0;
        for (j = 0; j < n_asoc; j++) {
            if (res[j].fuerza >= (min_pf - 0.005f) && res[j].fuerza <= (max_pf + 0.005f)) {
                jb_list_push(&sub, jb_id_to_texto_var(res[j].id));
                count++;
            }
        }

        if (count > 0) {
            jb_var_t ktxt = jb_jmn_key_as_text(item);
            jb_map_put(&map_out, ktxt, sub);
            jb_var_clear(&ktxt);
            jb_var_clear(&sub);
        } else
            jb_var_clear(&sub);
    }

    jb_jmn_set_resultado(map_out);
    jb_var_clear(&map_out);
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_asociar_relacion_4(jb_var_t a, jb_var_t b, jb_var_t tipo_v, jb_var_t peso_v) {
    uint32_t id1 = jb_var_to_concept_id(a);
    uint32_t id2 = jb_var_to_concept_id(b);
    uint32_t tipo;
    float peso;
    JMNValor v_media;
    JMNValor v_w;

    if (tipo_v.type == JB_TYPE_ENTERO)
        tipo = (uint32_t)tipo_v.u.i64;
    else
        tipo = (uint32_t)(jb_jmn_as_f64(tipo_v, 1.0) + 0.5);
    if (tipo == 0u)
        tipo = 1u;
    if (tipo > JMN_RELACION_MAX)
        tipo = 1u;

    peso = (float)jb_jmn_clamp01(jb_jmn_as_f64(peso_v, 1.0));
    v_media.f = 0.5f;
    v_w.f = peso;

    if (!jb_neu)
        return jb_new_nulo();
    if (!jmn_obtener_nodo(jb_neu, id1))
        jmn_agregar_nodo(jb_neu, id1, v_media);
    if (!jmn_obtener_nodo(jb_neu, id2))
        jmn_agregar_nodo(jb_neu, id2, v_media);
    jmn_agregar_conexion(jb_neu, id1, id2, v_w, tipo);
    if (tipo == JMN_RELACION_SIMILITUD || tipo == JMN_RELACION_OPOSICION)
        jmn_agregar_conexion(jb_neu, id2, id1, v_w, tipo);
    jb_jmn_set_resultado(jb_new_flotante_scalar((double)peso));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_propagar_activacion(jb_var_t origen, jb_var_t decaimiento) {
    uint32_t oid = jb_var_to_concept_id(origen);
    float decay = (float)jb_jmn_clamp01(jb_jmn_as_f64(decaimiento, 0.5));
    JMNActivacionResultado ar[32];
    int nfill, i;
    jb_var_t out = jb_new_map();
    if (!jb_neu)
        goto done;
    nfill = jmn_propagar_activacion(jb_neu, oid, 1.0f, 0.8f, 0.1f, 3u, 0u, ar, 32u, NULL, 0, NULL);
    for (i = 0; i < nfill; i++) {
        jb_var_t k = jb_id_to_texto_var(ar[i].id);
        jb_map_put(&out, k, jb_new_flotante_scalar((double)jb_jmn_clamp01((double)ar[i].activacion * (double)decay)));
        jb_var_clear(&k);
    }
done:
    jb_jmn_set_resultado(out);
    jb_var_clear(&out);
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_resolver_conflictos(jb_var_t origen) {
    uint32_t oid = jb_var_to_concept_id(origen);
    JMNBusquedaResultado res[32];
    int n = 0;
    jb_var_t best = jb_new_nulo();
    if (jb_neu)
        n = jmn_buscar_asociaciones(jb_neu, oid, 0u, 0.01f, 1, res, 32);
    if (n > 0) {
        jb_var_clear(&best);
        best = jb_id_to_texto_var(res[0].id);
    }
    jb_jmn_set_resultado(best);
    jb_var_clear(&best);
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_buscar_peso(jb_var_t concepto) {
    uint32_t id = jb_var_to_concept_id(concepto);
    float w = 0.f;
    if (jb_neu) {
        JMNNodo *n = jmn_obtener_nodo(jb_neu, id);
        if (n)
            w = n->peso.f;
    }
    return jb_new_flotante_scalar((double)w);
}

jb_var_t jb_mem_obtener_fuerza(jb_var_t origen, jb_var_t destino) {
    uint32_t a = jb_var_to_concept_id(origen);
    uint32_t b = jb_var_to_concept_id(destino);
    float f = 0.f;
    if (jb_neu)
        f = jmn_obtener_fuerza_asociacion(jb_neu, a, b);
    jb_jmn_set_resultado(jb_new_flotante_scalar((double)f));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_reforzar_concepto(jb_var_t concepto, jb_var_t magnitud) {
    uint32_t id = jb_var_to_concept_id(concepto);
    int mag = (int)jb_jmn_as_f64(magnitud, 10.0);
    float delta;
    if (mag < 1)
        mag = 1;
    if (mag > 100)
        mag = 100;
    delta = (float)mag / 100.0f;
    if (jb_neu && id != 0)
        jmn_reforzar_concepto(jb_neu, id, delta);
    jb_jmn_set_resultado(jb_new_bool(1));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_penalizar_concepto(jb_var_t concepto, jb_var_t magnitud) {
    uint32_t id = jb_var_to_concept_id(concepto);
    int mag = (int)jb_jmn_as_f64(magnitud, 10.0);
    float delta;
    if (mag < 1)
        mag = 1;
    if (mag > 100)
        mag = 100;
    delta = (float)mag / 100.0f;
    if (jb_neu && id != 0)
        jmn_penalizar_concepto(jb_neu, id, delta);
    jb_jmn_set_resultado(jb_new_bool(1));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_olvidar_debiles(jb_var_t umbral_v) {
    float th = (float)jb_jmn_clamp01(jb_jmn_as_f64(umbral_v, 0.1));
    if (jb_neu)
        (void)jmn_olvidar_conexiones_debiles(jb_neu, th);
    return jb_new_bool(1);
}

jb_var_t jb_decaer_conexiones(jb_var_t factor) {
    float f = (float)jb_jmn_clamp01(jb_jmn_as_f64(factor, 0.05));
    if (jb_neu)
        jmn_decaer_conexiones_global(jb_neu, f, 1, 0.01f);
    return jb_new_bool(1);
}
