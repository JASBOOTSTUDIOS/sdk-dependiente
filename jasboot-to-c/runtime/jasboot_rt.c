#include "jasboot_rt.h"
#include "jasboot_rt_jmn_vm.h"
#include "memoria_neuronal.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

jb_var_t jb_resultado_global;
jb_var_t g_last_throw;
int jb_try_depth;
jmp_buf jb_try_stack[32];

static int g_argc;
static char **g_argv;

/* --- ventanas circulares --- */
#define JB_RING_MAX 4096
static int64_t g_perc_ring[JB_RING_MAX];
static size_t g_perc_cap, g_perc_head, g_perc_count;

static int64_t g_rastro_ring[JB_RING_MAX];
static double g_rastro_w[JB_RING_MAX];
static size_t g_rastro_cap, g_rastro_head, g_rastro_count;

static void jb_fputs_esc_texto(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    if (!p) return;
    fputc('"', stdout);
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', stdout);
            fputc(*p, stdout);
        } else if (*p < 32) {
            fprintf(stdout, "\\u%04x", (unsigned)*p);
        } else
            fputc((int)*p, stdout);
    }
    fputc('"', stdout);
}

static void jb_print_scalar_json_value(jb_var_t v) {
    switch (v.type) {
        case JB_TYPE_TEXTO:
            jb_fputs_esc_texto(v.u.str ? v.u.str : "");
            break;
        case JB_TYPE_ENTERO:
            printf("%lld", (long long)v.u.i64);
            break;
        case JB_TYPE_FLOTANTE:
            printf("%.15g", v.u.f64);
            break;
        case JB_TYPE_BOOL:
            jb_fputs_esc_texto(v.u.b ? "verdadero" : "falso");
            break;
        default:
            printf("null");
            break;
    }
}

static void jb_print_map_json(jb_map_t *M) {
    size_t i;
    fputc('{', stdout);
    if (M)
        for (i = 0; i < M->len; i++) {
            if (i) fputs(", ", stdout);
            jb_fputs_esc_texto(M->keys[i].type == JB_TYPE_TEXTO && M->keys[i].u.str ? M->keys[i].u.str : "");
            fputs(": ", stdout);
            jb_print_scalar_json_value(M->vals[i]);
        }
    fputc('}', stdout);
}

void jb_var_clear(jb_var_t *v) {
    size_t i;
    if (!v) return;
    switch (v->type) {
        case JB_TYPE_TEXTO:
            free(v->u.str);
            v->u.str = NULL;
            break;
        case JB_TYPE_LIST:
            if (v->u.lst) {
                for (i = 0; i < v->u.lst->len; i++) jb_var_clear(&v->u.lst->items[i]);
                free(v->u.lst->items);
                free(v->u.lst);
                v->u.lst = NULL;
            }
            break;
        case JB_TYPE_MAP:
            if (v->u.map) {
                for (i = 0; i < v->u.map->len; i++) {
                    jb_var_clear(&v->u.map->keys[i]);
                    jb_var_clear(&v->u.map->vals[i]);
                }
                free(v->u.map->keys);
                free(v->u.map->vals);
                free(v->u.map);
                v->u.map = NULL;
            }
            break;
        case JB_TYPE_JSON:
            jb_json_free_tree(v->u.json);
            v->u.json = NULL;
            break;
        default:
            break;
    }
    v->type = JB_TYPE_NULL;
}

jb_var_t jb_var_clone(jb_var_t v) {
    size_t i;
    jb_var_t r = jb_new_nulo();
    switch (v.type) {
        case JB_TYPE_NULL:
            return jb_new_nulo();
        case JB_TYPE_ENTERO:
            return jb_new_entero(v.u.i64);
        case JB_TYPE_FLOTANTE:
            return jb_new_flotante_scalar(v.u.f64);
        case JB_TYPE_BOOL:
            return jb_new_bool(v.u.b);
        case JB_TYPE_TEXTO:
            return jb_new_texto(v.u.str ? v.u.str : "");
        case JB_TYPE_VEC2:
        case JB_TYPE_VEC3:
        case JB_TYPE_VEC4:
        case JB_TYPE_MAT3:
        case JB_TYPE_MAT4:
            r.type = v.type;
            r.u.vmat = v.u.vmat;
            return r;
        case JB_TYPE_LIST:
            r = jb_new_list();
            if (v.u.lst)
                for (i = 0; i < v.u.lst->len; i++) jb_list_push(&r, jb_var_clone(v.u.lst->items[i]));
            return r;
        case JB_TYPE_MAP:
            r = jb_new_map();
            if (v.u.map)
                for (i = 0; i < v.u.map->len; i++) jb_map_put(&r, jb_var_clone(v.u.map->keys[i]), jb_var_clone(v.u.map->vals[i]));
            return r;
        case JB_TYPE_JSON:
            if (v.u.json) {
                jb_json_node_t *c = jb_json_clone_subtree(v.u.json);
                r.type = JB_TYPE_JSON;
                r.u.json = c;
            }
            return r;
        default:
            return jb_new_nulo();
    }
}

jb_var_t jb_new_nulo(void) {
    jb_var_t v = { JB_TYPE_NULL };
    return v;
}

jb_var_t jb_new_entero(int64_t x) {
    jb_var_t v = { JB_TYPE_ENTERO, .u.i64 = x };
    return v;
}

jb_var_t jb_new_flotante_scalar(double x) {
    jb_var_t v = { JB_TYPE_FLOTANTE, .u.f64 = x };
    return v;
}

jb_var_t jb_new_flotante_from_var(jb_var_t x) {
    jb_var_t v = { JB_TYPE_FLOTANTE, .u.f64 = jb_jmn_as_f64(x, 0.0) };
    return v;
}

jb_var_t jb_new_flotante_dispatch(int kind, const void *addr) {
    jb_var_t vtmp;
    switch (kind) {
    case JB_NF_JBVAR:
        memcpy(&vtmp, addr, sizeof(jb_var_t));
        return jb_new_flotante_from_var(vtmp);
    case JB_NF_FLOAT:
        return jb_new_flotante_scalar((double)*(const float *)addr);
    case JB_NF_DOUBLE:
        return jb_new_flotante_scalar(*(const double *)addr);
    case JB_NF_LONGDOUBLE:
        return jb_new_flotante_scalar((double)*(const long double *)addr);
    case JB_NF_LLONG:
        return jb_new_flotante_scalar((double)*(const long long *)addr);
    case JB_NF_ULLONG:
        return jb_new_flotante_scalar((double)*(const unsigned long long *)addr);
    case JB_NF_LONG:
        return jb_new_flotante_scalar((double)*(const long *)addr);
    case JB_NF_ULONG:
        return jb_new_flotante_scalar((double)*(const unsigned long *)addr);
    case JB_NF_INT:
        return jb_new_flotante_scalar((double)*(const int *)addr);
    case JB_NF_UINT:
        return jb_new_flotante_scalar((double)*(const unsigned int *)addr);
    case JB_NF_SHORT:
        return jb_new_flotante_scalar((double)*(const short *)addr);
    case JB_NF_USHORT:
        return jb_new_flotante_scalar((double)*(const unsigned short *)addr);
    case JB_NF_SCHAR:
        return jb_new_flotante_scalar((double)*(const signed char *)addr);
    case JB_NF_UCHAR:
        return jb_new_flotante_scalar((double)*(const unsigned char *)addr);
    case JB_NF_CHAR:
        return jb_new_flotante_scalar((double)*(const char *)addr);
    case JB_NF_BOOL:
        return jb_new_flotante_scalar((double)*(const _Bool *)addr);
    default:
        return jb_new_flotante_scalar(0);
    }
}

jb_var_t jb_new_bool(bool b) {
    jb_var_t v = { JB_TYPE_BOOL, .u.b = b };
    return v;
}

jb_var_t jb_new_texto(const char *s) {
    jb_var_t v = { JB_TYPE_TEXTO, .u.str = strdup(s ? s : "") };
    return v;
}

jb_var_t jb_new_list(void) {
    jb_var_t v = { JB_TYPE_LIST };
    v.u.lst = (jb_list_t *)calloc(1, sizeof(jb_list_t));
    return v;
}

jb_var_t jb_new_map(void) {
    jb_var_t v = { JB_TYPE_MAP };
    v.u.map = (jb_map_t *)calloc(1, sizeof(jb_map_t));
    return v;
}

static int jb_text_key_eq(jb_var_t a, jb_var_t b) {
    char ta[256], tb[256];
    jb_var_t ca = jb_jmn_key_as_text(a);
    jb_var_t cb = jb_jmn_key_as_text(b);
    snprintf(ta, sizeof ta, "%s", ca.type == JB_TYPE_TEXTO && ca.u.str ? ca.u.str : "");
    snprintf(tb, sizeof tb, "%s", cb.type == JB_TYPE_TEXTO && cb.u.str ? cb.u.str : "");
    jb_var_clear(&ca);
    jb_var_clear(&cb);
    return strcmp(ta, tb) == 0;
}

jb_var_t jb_map_get(jb_var_t map, jb_var_t key) {
    size_t i;
    if (map.type != JB_TYPE_MAP || !map.u.map) return jb_new_nulo();
    for (i = 0; i < map.u.map->len; i++) {
        if (jb_text_key_eq(map.u.map->keys[i], key)) return jb_var_clone(map.u.map->vals[i]);
    }
    return jb_new_nulo();
}

void jb_map_put(jb_var_t *map_var, jb_var_t key, jb_var_t val) {
    jb_map_t *M;
    size_t i;
    if (!map_var) return;
    if (map_var->type != JB_TYPE_MAP || !map_var->u.map) {
        jb_var_clear(map_var);
        *map_var = jb_new_map();
    }
    M = map_var->u.map;
    for (i = 0; i < M->len; i++) {
        if (jb_text_key_eq(M->keys[i], key)) {
            jb_var_clear(&M->vals[i]);
            M->vals[i] = jb_var_clone(val);
            return;
        }
    }
    if (M->len + 1 > M->cap) {
        size_t nc = M->cap ? M->cap * 2 : 8;
        jb_var_t *nk = (jb_var_t *)realloc(M->keys, nc * sizeof(jb_var_t));
        jb_var_t *nv = (jb_var_t *)realloc(M->vals, nc * sizeof(jb_var_t));
        if (!nk || !nv) return;
        M->keys = nk;
        M->vals = nv;
        M->cap = nc;
    }
    M->keys[M->len] = jb_var_clone(key);
    M->vals[M->len] = jb_var_clone(val);
    M->len++;
}

jb_var_t jb_map_len(jb_var_t map) {
    if (map.type != JB_TYPE_MAP || !map.u.map) return jb_new_entero(0);
    return jb_new_entero((int64_t)map.u.map->len);
}

jb_var_t jb_map_has(jb_var_t map, jb_var_t key) {
    size_t i;
    if (map.type != JB_TYPE_MAP || !map.u.map) return jb_new_bool(false);
    for (i = 0; i < map.u.map->len; i++) {
        if (jb_text_key_eq(map.u.map->keys[i], key)) return jb_new_bool(true);
    }
    return jb_new_bool(false);
}

void jb_map_remove(jb_var_t *map_var, jb_var_t key) {
    jb_map_t *M;
    size_t i, j;
    if (!map_var || map_var->type != JB_TYPE_MAP || !map_var->u.map) return;
    M = map_var->u.map;
    for (i = 0; i < M->len; i++) {
        if (jb_text_key_eq(M->keys[i], key)) {
            jb_var_clear(&M->keys[i]);
            jb_var_clear(&M->vals[i]);
            for (j = i + 1; j < M->len; j++) {
                M->keys[j - 1] = M->keys[j];
                M->vals[j - 1] = M->vals[j];
            }
            M->len--;
            return;
        }
    }
}

jb_var_t jb_map_val_at(jb_var_t map, jb_var_t idx) {
    int64_t i = 0;
    if (map.type != JB_TYPE_MAP || !map.u.map) return jb_new_nulo();
    if (idx.type == JB_TYPE_ENTERO) i = idx.u.i64;
    else if (idx.type == JB_TYPE_FLOTANTE) i = (int64_t)idx.u.f64;
    if (i < 0 || (size_t)i >= map.u.map->len) return jb_new_nulo();
    return jb_var_clone(map.u.map->vals[(size_t)i]);
}

jb_var_t jb_map_key_at(jb_var_t map, jb_var_t idx) {
    int64_t i = 0;
    if (map.type != JB_TYPE_MAP || !map.u.map) return jb_new_nulo();
    if (idx.type == JB_TYPE_ENTERO) i = idx.u.i64;
    else if (idx.type == JB_TYPE_FLOTANTE) i = (int64_t)idx.u.f64;
    if (i < 0 || (size_t)i >= map.u.map->len) return jb_new_nulo();
    return jb_var_clone(map.u.map->keys[(size_t)i]);
}

void jb_list_push(jb_var_t *list_var, jb_var_t item) {
    jb_list_t *L;
    if (!list_var) return;
    if (list_var->type != JB_TYPE_LIST || !list_var->u.lst) {
        jb_var_clear(list_var);
        *list_var = jb_new_list();
    }
    L = list_var->u.lst;
    if (L->len + 1 > L->cap) {
        size_t nc = L->cap ? L->cap * 2 : 8;
        jb_var_t *ni = (jb_var_t *)realloc(L->items, nc * sizeof(jb_var_t));
        if (!ni) return;
        L->items = ni;
        L->cap = nc;
    }
    L->items[L->len++] = jb_var_clone(item);
}

jb_var_t jb_list_get(jb_var_t list, jb_var_t idx) {
    int64_t i = 0;
    if (list.type != JB_TYPE_LIST || !list.u.lst) return jb_new_nulo();
    if (idx.type == JB_TYPE_ENTERO) i = idx.u.i64;
    else if (idx.type == JB_TYPE_FLOTANTE) i = (int64_t)idx.u.f64;
    if (i < 0 || (size_t)i >= list.u.lst->len) return jb_new_nulo();
    return jb_var_clone(list.u.lst->items[(size_t)i]);
}

jb_var_t jb_list_len(jb_var_t list) {
    if (list.type != JB_TYPE_LIST || !list.u.lst) return jb_new_entero(0);
    return jb_new_entero((int64_t)list.u.lst->len);
}

void jb_list_clear(jb_var_t *list_var) {
    size_t i;
    if (!list_var || list_var->type != JB_TYPE_LIST || !list_var->u.lst) return;
    for (i = 0; i < list_var->u.lst->len; i++) jb_var_clear(&list_var->u.lst->items[i]);
    list_var->u.lst->len = 0;
}

void jb_list_release_in_place(jb_var_t *list_var) { jb_list_clear(list_var); }

void jb_list_set(jb_var_t *list_var, jb_var_t idx, jb_var_t val) {
    int64_t i = 0;
    if (!list_var || list_var->type != JB_TYPE_LIST || !list_var->u.lst) return;
    if (idx.type == JB_TYPE_ENTERO) i = idx.u.i64;
    else if (idx.type == JB_TYPE_FLOTANTE) i = (int64_t)idx.u.f64;
    if (i < 0 || (size_t)i >= list_var->u.lst->len) return;
    jb_var_clear(&list_var->u.lst->items[(size_t)i]);
    list_var->u.lst->items[(size_t)i] = jb_var_clone(val);
}

void jb_throw_val(jb_var_t err) {
    jb_var_clear(&g_last_throw);
    g_last_throw = jb_var_clone(err);
    if (jb_try_depth > 0) longjmp(jb_try_stack[jb_try_depth - 1], 1);
    fputs("Excepcion AOT no atrapada\n", stderr);
    jb_cleanup();
    exit(1);
}

void jb_warn_aot(const char *msg) { fprintf(stderr, "[AOT] %s\n", msg ? msg : "?"); }

jb_var_t jb_warn_aot_expr(const char *msg) {
    jb_warn_aot(msg);
    return jb_new_nulo();
}

jb_var_t jb_consolidar_memoria_expr(void) {
    jb_consolidar_memoria();
    return jb_new_nulo();
}

int jb_truthy(jb_var_t v) {
    switch (v.type) {
        case JB_TYPE_NULL:
            return 0;
        case JB_TYPE_ENTERO:
            return v.u.i64 != 0;
        case JB_TYPE_FLOTANTE:
            return v.u.f64 != 0.0;
        case JB_TYPE_BOOL:
            return v.u.b ? 1 : 0;
        case JB_TYPE_TEXTO:
            return v.u.str && v.u.str[0];
        case JB_TYPE_LIST:
            return v.u.lst && v.u.lst->len > 0;
        case JB_TYPE_MAP:
            return v.u.map && v.u.map->len > 0;
        default:
            return 1;
    }
}

void jb_imprimir(jb_var_t v) {
    switch (v.type) {
        case JB_TYPE_TEXTO:
            /* Paridad VM: texto como cadena literal (sin envoltura JSON). */
            fputs(v.u.str ? v.u.str : "", stdout);
            fputc('\n', stdout);
            break;
        case JB_TYPE_ENTERO:
            printf("%lld\n", (long long)v.u.i64);
            break;
        case JB_TYPE_FLOTANTE:
            printf("%.15g\n", v.u.f64);
            break;
        case JB_TYPE_BOOL:
            puts(v.u.b ? "verdadero" : "falso");
            break;
        case JB_TYPE_MAP:
            jb_print_map_json(v.u.map);
            fputc('\n', stdout);
            break;
        case JB_TYPE_LIST:
            fputc('[', stdout);
            if (v.u.lst) {
                size_t i;
                for (i = 0; i < v.u.lst->len; i++) {
                    if (i) fputs(", ", stdout);
                    jb_print_scalar_json_value(v.u.lst->items[i]);
                }
            }
            fputs("]\n", stdout);
            break;
        case JB_TYPE_JSON:
            jb_json_print_compact(v);
            fputc('\n', stdout);
            break;
        default:
            puts("null");
            break;
    }
}

void jb_imprimir_sin_salto(jb_var_t v) {
    switch (v.type) {
        case JB_TYPE_TEXTO:
            /* Paridad VM: sin comillas JSON (imprimir_sin_salto es prompt / prefijo). */
            fputs(v.u.str ? v.u.str : "", stdout);
            break;
        case JB_TYPE_ENTERO:
            printf("%lld", (long long)v.u.i64);
            break;
        case JB_TYPE_FLOTANTE:
            printf("%.15g", v.u.f64);
            break;
        case JB_TYPE_BOOL:
            fputs(v.u.b ? "verdadero" : "falso", stdout);
            break;
        default:
            fputs("null", stdout);
            break;
    }
    fflush(stdout);
}

void jb_imprimir_flotante(jb_var_t v) {
    /* Paridad VM: vm_escribir_flotante usa %.4f, sin salto entre llamadas consecutivas. */
    if (v.type == JB_TYPE_FLOTANTE)
        printf("%.4f", v.u.f64);
    else if (v.type == JB_TYPE_ENTERO)
        printf("%lld", (long long)v.u.i64);
    else
        jb_imprimir_sin_salto(v);
    fflush(stdout);
}

void jb_assign(jb_var_t *dst, jb_var_t src) {
    if (!dst) return;
    jb_var_clear(dst);
    *dst = jb_var_clone(src);
}

void jb_index_set(jb_var_t *container, jb_var_t idx, jb_var_t val) {
    int64_t i = 0;
    if (!container) return;
    if (container->type == JB_TYPE_LIST && container->u.lst) {
        if (idx.type == JB_TYPE_ENTERO) i = idx.u.i64;
        else if (idx.type == JB_TYPE_FLOTANTE) i = (int64_t)idx.u.f64;
        if (i < 0 || (size_t)i >= container->u.lst->len) return;
        jb_var_clear(&container->u.lst->items[(size_t)i]);
        container->u.lst->items[(size_t)i] = jb_var_clone(val);
    }
}

void jb_set_argv(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
}

void jb_init(void) {
    jb_try_depth = 0;
    jb_resultado_global = jb_new_nulo();
    g_last_throw = jb_new_nulo();
    jb_jmn_vm_startup();
    g_argc = 0;
    g_argv = NULL;
    g_perc_cap = g_perc_head = g_perc_count = 0;
    g_rastro_cap = g_rastro_head = g_rastro_count = 0;
}

void jb_cleanup(void) {
    jb_jmn_vm_shutdown();
    jb_var_clear(&jb_resultado_global);
    jb_var_clear(&g_last_throw);
}

double jb_jmn_as_f64(jb_var_t v, double def) {
    if (v.type == JB_TYPE_FLOTANTE) return v.u.f64;
    if (v.type == JB_TYPE_ENTERO) return (double)v.u.i64;
    if (v.type == JB_TYPE_BOOL) return v.u.b ? 1.0 : 0.0;
    return def;
}

double jb_jmn_clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

jb_var_t jb_jmn_key_as_text(jb_var_t key) {
    char buf[64];
    if (key.type == JB_TYPE_TEXTO) return jb_var_clone(key);
    if (key.type == JB_TYPE_ENTERO) {
        snprintf(buf, sizeof buf, "%lld", (long long)key.u.i64);
        return jb_new_texto(buf);
    }
    if (key.type == JB_TYPE_FLOTANTE) {
        snprintf(buf, sizeof buf, "%.15g", key.u.f64);
        return jb_new_texto(buf);
    }
    return jb_new_texto("");
}

void jb_jmn_set_resultado(jb_var_t v) {
    jb_var_clear(&jb_resultado_global);
    jb_resultado_global = jb_var_clone(v);
}

static void jb_line_trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\x1a')) {
        s[--n] = 0;
    }
}

static void jb_line_trim_both(char *s) {
    char *p;
    jb_line_trim(s);
    p = s;
    while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

jb_var_t jb_recordar_stub(jb_var_t key, jb_var_t val) { return jb_recordar(key, val); }
jb_var_t jb_buscar_stub(jb_var_t key) { return jb_buscar(key); }
jb_var_t jb_crear_memoria_stub(jb_var_t path) { return jb_crear_memoria(path); }

static double jb_mlp_sigmoid(double z) {
    if (z > 35.0) return 1.0;
    if (z < -35.0) return 0.0;
    return 1.0 / (1.0 + exp(-z));
}

static void jb_mlp_softmax_stable(const double *logits, int n, double *probs) {
    int i;
    double max_v, sum;
    if (!logits || !probs || n <= 0) return;
    max_v = logits[0];
    for (i = 1; i < n; i++)
        if (logits[i] > max_v) max_v = logits[i];
    sum = 0.0;
    for (i = 0; i < n; i++) {
        double e = exp(logits[i] - max_v);
        probs[i] = e;
        sum += e;
    }
    if (sum <= 0.0) {
        double inv = 1.0 / (double)n;
        for (i = 0; i < n; i++) probs[i] = inv;
        return;
    }
    for (i = 0; i < n; i++) probs[i] /= sum;
}

static jb_list_t *jb_mlp_lst(jb_var_t v) {
    if (v.type != JB_TYPE_LIST || !v.u.lst) return NULL;
    return v.u.lst;
}

static void jb_mlp_list_rebuild_floats(jb_var_t *list_wrap, int n, const double *vals) {
    int i;
    if (!list_wrap || list_wrap->type != JB_TYPE_LIST || !list_wrap->u.lst) return;
    jb_list_clear(list_wrap);
    for (i = 0; i < n; i++) jb_list_push(list_wrap, jb_new_flotante_scalar(vals[i]));
}

jb_var_t jb_nativo_mlp_entrenar(jb_var_t pesos_capas, jb_var_t sesgos_capas, jb_var_t X, jb_var_t lista_y,
                                jb_var_t capas_ocultas, jb_var_t learning_rate, jb_var_t epochs_v) {
    jb_list_t *mp, *ms, *mx, *my, *mc;
    double lr = jb_jmn_as_f64(learning_rate, 0.0);
    uint32_t epochs = 0;
    double *W1 = NULL, *b1 = NULL, *W2 = NULL, *b2 = NULL, *xs = NULL, *ys = NULL;
    double *z1 = NULL, *a1 = NULL, *d1 = NULL, *z2 = NULL, *out = NULL, *d2 = NULL;
    uint32_t ep, s;
    int i, j, k;
    jb_list_t *mL0, *mL1, *mS0, *mS1;
    int H, n_in, n_out, y_vectorial;
    uint32_t N;
    size_t W1sz, W2sz;
    float last_mse = 1.0f;

    if (epochs_v.type == JB_TYPE_ENTERO) {
        if (epochs_v.u.i64 > 0) epochs = (uint32_t)epochs_v.u.i64;
    } else if (epochs_v.type == JB_TYPE_FLOTANTE)
        epochs = (uint32_t)(epochs_v.u.f64 + 0.5);
    if (lr <= 0.0 || lr > 10.0 || epochs == 0 || epochs > 10000000u) return jb_new_flotante_scalar(1.0);
    mp = jb_mlp_lst(pesos_capas);
    ms = jb_mlp_lst(sesgos_capas);
    mx = jb_mlp_lst(X);
    my = jb_mlp_lst(lista_y);
    mc = jb_mlp_lst(capas_ocultas);
    if (!mp || !ms || !mx || !my || !mc || mc->len < 1) return jb_new_flotante_scalar(1.0);
    H = (int)(jb_jmn_as_f64(mc->items[0], 0.0) + 0.5);
    if (H <= 0 && mc->items[0].type == JB_TYPE_ENTERO) H = (int)mc->items[0].u.i64;
    if (H <= 0 || H > 8192) return jb_new_flotante_scalar(1.0);
    N = (uint32_t)mx->len;
    if (N == 0 || my->len != (size_t)N) return jb_new_flotante_scalar(1.0);
    if (!jb_mlp_lst(mx->items[0])) return jb_new_flotante_scalar(1.0);
    n_in = (int)jb_mlp_lst(mx->items[0])->len;
    if (n_in <= 0 || n_in > 1024) return jb_new_flotante_scalar(1.0);
    if (mp->len < 2 || ms->len < 2) return jb_new_flotante_scalar(1.0);
    n_out = 1;
    y_vectorial = 0;
    {
        jb_var_t y0 = my->items[0];
        jb_list_t *y0l = jb_mlp_lst(y0);
        if (y0l) {
            n_out = (int)y0l->len;
            y_vectorial = 1;
        }
        if (n_out <= 0 || n_out > 512) return jb_new_flotante_scalar(1.0);
    }
    W1sz = (size_t)n_in * (size_t)H;
    W2sz = (size_t)H * (size_t)n_out;
    W1 = (double *)calloc(W1sz, sizeof(double));
    b1 = (double *)calloc((size_t)H, sizeof(double));
    W2 = (double *)calloc(W2sz, sizeof(double));
    b2 = (double *)calloc((size_t)n_out, sizeof(double));
    xs = (double *)calloc((size_t)N * (size_t)n_in, sizeof(double));
    ys = (double *)calloc((size_t)N * (size_t)n_out, sizeof(double));
    z1 = (double *)calloc((size_t)H, sizeof(double));
    a1 = (double *)calloc((size_t)H, sizeof(double));
    d1 = (double *)calloc((size_t)H, sizeof(double));
    z2 = (double *)calloc((size_t)n_out, sizeof(double));
    out = (double *)calloc((size_t)n_out, sizeof(double));
    d2 = (double *)calloc((size_t)n_out, sizeof(double));
    if (!W1 || !b1 || !W2 || !b2 || !xs || !ys || !z1 || !a1 || !d1 || !z2 || !out || !d2) goto fail_free;

    for (s = 0; s < N; s++) {
        jb_list_t *mi = jb_mlp_lst(mx->items[s]);
        if (!mi || mi->len != (size_t)n_in) goto fail_free;
        for (j = 0; j < n_in; j++) xs[(size_t)s * (size_t)n_in + (size_t)j] = jb_jmn_as_f64(mi->items[j], 0.0);
        if (y_vectorial) {
            jb_list_t *myr = jb_mlp_lst(my->items[s]);
            if (!myr || myr->len != (size_t)n_out) goto fail_free;
            for (k = 0; k < n_out; k++) ys[(size_t)s * (size_t)n_out + (size_t)k] = jb_jmn_as_f64(myr->items[k], 0.0);
        } else
            ys[(size_t)s * (size_t)n_out] = jb_jmn_as_f64(my->items[s], 0.0);
    }

    mL0 = jb_mlp_lst(mp->items[0]);
    if (!mL0 || mL0->len != (size_t)n_in) goto fail_free;
    for (i = 0; i < n_in; i++) {
        jb_list_t *mr = jb_mlp_lst(mL0->items[i]);
        if (!mr || mr->len != (size_t)H) goto fail_free;
        for (j = 0; j < H; j++) W1[(size_t)i * (size_t)H + (size_t)j] = jb_jmn_as_f64(mr->items[j], 0.0);
    }
    mS0 = jb_mlp_lst(ms->items[0]);
    if (!mS0 || mS0->len != (size_t)H) goto fail_free;
    for (j = 0; j < H; j++) b1[j] = jb_jmn_as_f64(mS0->items[j], 0.0);
    mL1 = jb_mlp_lst(mp->items[1]);
    if (!mL1 || mL1->len != (size_t)H) goto fail_free;
    for (i = 0; i < H; i++) {
        jb_list_t *mr = jb_mlp_lst(mL1->items[i]);
        if (!mr || mr->len < (size_t)n_out) goto fail_free;
        for (k = 0; k < n_out; k++) W2[(size_t)i * (size_t)n_out + (size_t)k] = jb_jmn_as_f64(mr->items[k], 0.0);
    }
    mS1 = jb_mlp_lst(ms->items[1]);
    if (!mS1 || mS1->len < (size_t)n_out) goto fail_free;
    for (k = 0; k < n_out; k++) b2[k] = jb_jmn_as_f64(mS1->items[k], 0.0);

    for (ep = 0; ep < epochs; ep++) {
        double errsum = 0.0;
        for (s = 0; s < N; s++) {
            const double *xv = xs + (size_t)s * (size_t)n_in;
            const double *yv = ys + (size_t)s * (size_t)n_out;
            for (j = 0; j < H; j++) {
                double sum = b1[j];
                for (i = 0; i < n_in; i++) sum += W1[(size_t)i * (size_t)H + (size_t)j] * xv[i];
                z1[j] = sum;
                a1[j] = sum > 0.0 ? sum : 0.0;
            }
            for (k = 0; k < n_out; k++) {
                double sum2 = b2[k];
                for (j = 0; j < H; j++) sum2 += W2[(size_t)j * (size_t)n_out + (size_t)k] * a1[j];
                z2[k] = sum2;
            }
            if (n_out > 1) {
                jb_mlp_softmax_stable(z2, n_out, out);
                for (k = 0; k < n_out; k++) {
                    double e = out[k] - yv[k];
                    errsum += e * e;
                    d2[k] = e;
                }
            } else {
                out[0] = jb_mlp_sigmoid(z2[0]);
                {
                    double e = out[0] - yv[0];
                    errsum += e * e;
                    d2[0] = e * (out[0] * (1.0 - out[0]));
                }
            }
            for (i = 0; i < H; i++) {
                double chain = 0.0;
                for (k = 0; k < n_out; k++) chain += W2[(size_t)i * (size_t)n_out + (size_t)k] * d2[k];
                d1[i] = chain * (z1[i] > 0.0 ? 1.0 : 0.0);
            }
            for (i = 0; i < H; i++) {
                for (k = 0; k < n_out; k++) {
                    double g = d2[k] * a1[i];
                    W2[(size_t)i * (size_t)n_out + (size_t)k] -= lr * g;
                }
            }
            for (k = 0; k < n_out; k++) b2[k] -= lr * d2[k];
            for (i = 0; i < n_in; i++) {
                for (j = 0; j < H; j++) {
                    double g = d1[j] * xv[i];
                    W1[(size_t)i * (size_t)H + (size_t)j] -= lr * g;
                }
            }
            for (j = 0; j < H; j++) b1[j] -= lr * d1[j];
        }
        last_mse = (float)(errsum / ((double)N * (double)n_out));
    }

    for (i = 0; i < n_in; i++) {
        jb_list_t *mr = jb_mlp_lst(mL0->items[i]);
        for (j = 0; j < H; j++) {
            jb_var_clear(&mr->items[j]);
            mr->items[j] = jb_new_flotante_scalar((float)W1[(size_t)i * (size_t)H + (size_t)j]);
        }
    }
    if (sesgos_capas.type == JB_TYPE_LIST && sesgos_capas.u.lst && sesgos_capas.u.lst->len > 0)
        jb_mlp_list_rebuild_floats(&sesgos_capas.u.lst->items[0], H, b1);
    for (i = 0; i < H; i++) {
        jb_list_t *mr = jb_mlp_lst(mL1->items[i]);
        for (k = 0; k < n_out; k++) {
            jb_var_clear(&mr->items[k]);
            mr->items[k] = jb_new_flotante_scalar((float)W2[(size_t)i * (size_t)n_out + (size_t)k]);
        }
    }
    if (sesgos_capas.type == JB_TYPE_LIST && sesgos_capas.u.lst && sesgos_capas.u.lst->len > 1)
        jb_mlp_list_rebuild_floats(&sesgos_capas.u.lst->items[1], n_out, b2);

    goto mlp_cleanup;

fail_free:
    last_mse = 1.0f;

mlp_cleanup:
    free(W1);
    free(b1);
    free(W2);
    free(b2);
    free(xs);
    free(ys);
    free(z1);
    free(a1);
    free(d1);
    free(z2);
    free(out);
    free(d2);
    return jb_new_flotante_scalar((double)last_mse);
}

jb_var_t jb_add(jb_var_t a, jb_var_t b) {
    if (a.type == JB_TYPE_ENTERO && b.type == JB_TYPE_ENTERO) return jb_new_entero(a.u.i64 + b.u.i64);
    if (a.type == JB_TYPE_FLOTANTE && b.type == JB_TYPE_FLOTANTE) return jb_new_flotante_scalar(a.u.f64 + b.u.f64);
    if (a.type == JB_TYPE_ENTERO && b.type == JB_TYPE_FLOTANTE) return jb_new_flotante_scalar((double)a.u.i64 + b.u.f64);
    if (a.type == JB_TYPE_FLOTANTE && b.type == JB_TYPE_ENTERO) return jb_new_flotante_scalar(a.u.f64 + (double)b.u.i64);
    return jb_concat(a, b);
}

jb_var_t jb_sub(jb_var_t a, jb_var_t b) {
    if (a.type == JB_TYPE_ENTERO && b.type == JB_TYPE_ENTERO) return jb_new_entero(a.u.i64 - b.u.i64);
    if (a.type == JB_TYPE_FLOTANTE || b.type == JB_TYPE_FLOTANTE)
        return jb_new_flotante_scalar(jb_jmn_as_f64(a, 0) - jb_jmn_as_f64(b, 0));
    return jb_new_entero(0);
}

jb_var_t jb_mul(jb_var_t a, jb_var_t b) {
    if (a.type == JB_TYPE_ENTERO && b.type == JB_TYPE_ENTERO) return jb_new_entero(a.u.i64 * b.u.i64);
    return jb_new_flotante_scalar(jb_jmn_as_f64(a, 0) * jb_jmn_as_f64(b, 0));
}

jb_var_t jb_mod(jb_var_t a, jb_var_t b) {
    int64_t bi = b.type == JB_TYPE_ENTERO ? b.u.i64 : (int64_t)jb_jmn_as_f64(b, 1);
    if (bi == 0) jb_throw_val(jb_new_texto("division por cero"));
    if (a.type == JB_TYPE_ENTERO) return jb_new_entero(a.u.i64 % bi);
    return jb_new_entero(0);
}

jb_var_t jb_div(jb_var_t a, jb_var_t b) {
    double x = jb_jmn_as_f64(a, 0), y = jb_jmn_as_f64(b, 0);
    if (y == 0.0) jb_throw_val(jb_new_texto("division por cero"));
    return jb_new_flotante_scalar(x / y);
}

jb_var_t jb_eq(jb_var_t a, jb_var_t b) {
    if (a.type != b.type) {
        if ((a.type == JB_TYPE_ENTERO || a.type == JB_TYPE_FLOTANTE) &&
            (b.type == JB_TYPE_ENTERO || b.type == JB_TYPE_FLOTANTE))
            return jb_new_bool(fabs(jb_jmn_as_f64(a, 0) - jb_jmn_as_f64(b, 0)) < 1e-12);
        return jb_new_bool(false);
    }
    switch (a.type) {
        case JB_TYPE_ENTERO:
            return jb_new_bool(a.u.i64 == b.u.i64);
        case JB_TYPE_FLOTANTE:
            return jb_new_bool(fabs(a.u.f64 - b.u.f64) < 1e-12);
        case JB_TYPE_BOOL:
            return jb_new_bool(a.u.b == b.u.b);
        case JB_TYPE_TEXTO:
            return jb_new_bool(a.u.str && b.u.str && strcmp(a.u.str, b.u.str) == 0);
        default:
            return jb_new_bool(false);
    }
}

jb_var_t jb_ne(jb_var_t a, jb_var_t b) {
    jb_var_t e = jb_eq(a, b);
    int t = e.type == JB_TYPE_BOOL && e.u.b;
    jb_var_clear(&e);
    return jb_new_bool(!t);
}
jb_var_t jb_lt(jb_var_t a, jb_var_t b) { return jb_new_bool(jb_jmn_as_f64(a, 0) < jb_jmn_as_f64(b, 0)); }
jb_var_t jb_le(jb_var_t a, jb_var_t b) { return jb_new_bool(jb_jmn_as_f64(a, 0) <= jb_jmn_as_f64(b, 0)); }
jb_var_t jb_gt(jb_var_t a, jb_var_t b) { return jb_new_bool(jb_jmn_as_f64(a, 0) > jb_jmn_as_f64(b, 0)); }
jb_var_t jb_ge(jb_var_t a, jb_var_t b) { return jb_new_bool(jb_jmn_as_f64(a, 0) >= jb_jmn_as_f64(b, 0)); }

jb_var_t jb_land(jb_var_t a, jb_var_t b) { return jb_new_bool(jb_truthy(a) && jb_truthy(b)); }
jb_var_t jb_lor(jb_var_t a, jb_var_t b) { return jb_new_bool(jb_truthy(a) || jb_truthy(b)); }
jb_var_t jb_not(jb_var_t a) { return jb_new_bool(!jb_truthy(a)); }

jb_var_t jb_bit_shl(jb_var_t a, jb_var_t b) {
    int64_t s = b.type == JB_TYPE_ENTERO ? b.u.i64 : (int64_t)jb_jmn_as_f64(b, 0);
    if (a.type != JB_TYPE_ENTERO) return jb_new_entero(0);
    if (s < 0 || s > 62) return jb_new_entero(0);
    return jb_new_entero((uint64_t)a.u.i64 << (unsigned)s);
}

jb_var_t jb_bit_shr(jb_var_t a, jb_var_t b) {
    int64_t s = b.type == JB_TYPE_ENTERO ? b.u.i64 : (int64_t)jb_jmn_as_f64(b, 0);
    if (a.type != JB_TYPE_ENTERO) return jb_new_entero(0);
    if (s < 0 || s > 62) return jb_new_entero(0);
    return jb_new_entero((int64_t)((uint64_t)a.u.i64 >> (unsigned)s));
}

jb_var_t jb_concat(jb_var_t a, jb_var_t b) {
    char *buf;
    size_t la, lb;
    jb_var_t ta = jb_jmn_key_as_text(a);
    jb_var_t tb = jb_jmn_key_as_text(b);
    const char *sa = ta.type == JB_TYPE_TEXTO && ta.u.str ? ta.u.str : "";
    const char *sb = tb.type == JB_TYPE_TEXTO && tb.u.str ? tb.u.str : "";
    la = strlen(sa);
    lb = strlen(sb);
    buf = (char *)malloc(la + lb + 1);
    if (!buf) {
        jb_var_clear(&ta);
        jb_var_clear(&tb);
        return jb_new_texto("");
    }
    memcpy(buf, sa, la);
    memcpy(buf + la, sb, lb + 1);
    jb_var_clear(&ta);
    jb_var_clear(&tb);
    {
        jb_var_t r = jb_new_texto(buf);
        free(buf);
        return r;
    }
}

jb_var_t jb_texto_desde_numero(jb_var_t v) {
    char b[64];
    if (v.type == JB_TYPE_ENTERO) snprintf(b, sizeof b, "%lld", (long long)v.u.i64);
    else if (v.type == JB_TYPE_FLOTANTE)
        snprintf(b, sizeof b, "%.15g", v.u.f64);
    else
        snprintf(b, sizeof b, "0");
    return jb_new_texto(b);
}

jb_var_t jb_entero_a_texto(jb_var_t v) { return jb_texto_desde_numero(v); }

jb_var_t jb_str_a_entero(jb_var_t v) {
    char *end = NULL;
    long long x;
    if (v.type != JB_TYPE_TEXTO || !v.u.str) return jb_new_entero(0);
    errno = 0;
    x = strtoll(v.u.str, &end, 10);
    if (errno || end == v.u.str) return jb_new_entero(0);
    return jb_new_entero((int64_t)x);
}

jb_var_t jb_str_a_flotante(jb_var_t v) {
    char *end = NULL;
    double x;
    if (v.type != JB_TYPE_TEXTO || !v.u.str) return jb_new_flotante_scalar(0);
    errno = 0;
    x = strtod(v.u.str, &end);
    if (errno || !end || *end) return jb_new_flotante_scalar(0);
    return jb_new_flotante_scalar(x);
}

jb_var_t jb_texto_len(jb_var_t v) {
    if (v.type != JB_TYPE_TEXTO || !v.u.str) return jb_new_entero(0);
    return jb_new_entero((int64_t)strlen(v.u.str));
}

jb_var_t jb_extraer_subtexto(jb_var_t s, jb_var_t a, jb_var_t b) {
    /* Paridad VM (102): indice base 0 + longitud (no intervalo 1-based). */
    int64_t start0 = 0, len = 0, L;
    char *out;
    if (s.type != JB_TYPE_TEXTO || !s.u.str) return jb_new_texto("");
    L = (int64_t)strlen(s.u.str);
    if (a.type == JB_TYPE_ENTERO) start0 = a.u.i64;
    else if (a.type == JB_TYPE_FLOTANTE)
        start0 = (int64_t)a.u.f64;
    if (b.type == JB_TYPE_ENTERO) len = b.u.i64;
    else if (b.type == JB_TYPE_FLOTANTE)
        len = (int64_t)b.u.f64;
    if (start0 < 0) start0 = 0;
    if (len < 0) len = L - start0;
    if (start0 > L) return jb_new_texto("");
    if (start0 + len > L) len = L - start0;
    if (len <= 0) return jb_new_texto("");
    out = (char *)malloc((size_t)len + 1);
    if (!out) return jb_new_texto("");
    memcpy(out, s.u.str + (size_t)start0, (size_t)len);
    out[len] = 0;
    {
        jb_var_t r = jb_new_texto(out);
        free(out);
        return r;
    }
}

jb_var_t jb_contiene_texto(jb_var_t s, jb_var_t sub) {
    const char *a, *b;
    jb_var_t ts = jb_jmn_key_as_text(s);
    jb_var_t tsub = jb_jmn_key_as_text(sub);
    a = ts.type == JB_TYPE_TEXTO && ts.u.str ? ts.u.str : "";
    b = tsub.type == JB_TYPE_TEXTO && tsub.u.str ? tsub.u.str : "";
    {
        int ok = strstr(a, b) != NULL;
        jb_var_clear(&ts);
        jb_var_clear(&tsub);
        return jb_new_bool(ok);
    }
}

jb_var_t jb_termina_con(jb_var_t s, jb_var_t suf) {
    size_t la, lb;
    const char *a, *b;
    jb_var_t ts = jb_jmn_key_as_text(s);
    jb_var_t tsub = jb_jmn_key_as_text(suf);
    a = ts.type == JB_TYPE_TEXTO && ts.u.str ? ts.u.str : "";
    b = tsub.type == JB_TYPE_TEXTO && tsub.u.str ? tsub.u.str : "";
    la = strlen(a);
    lb = strlen(b);
    {
        int ok = lb <= la && strcmp(a + la - lb, b) == 0;
        jb_var_clear(&ts);
        jb_var_clear(&tsub);
        return jb_new_bool(ok);
    }
}

jb_var_t jb_minusculas(jb_var_t s) {
    char *p, *q;
    if (s.type != JB_TYPE_TEXTO || !s.u.str) return jb_new_texto("");
    q = strdup(s.u.str);
    if (!q) return jb_new_texto("");
    for (p = q; *p; p++) *p = (char)tolower((unsigned char)*p);
    {
        jb_var_t r = jb_new_texto(q);
        free(q);
        return r;
    }
}

jb_var_t jb_reemplazar(jb_var_t hay, jb_var_t patron, jb_var_t reemplazo) {
    jb_var_t th = jb_jmn_key_as_text(hay), tn = jb_jmn_key_as_text(patron), tr = jb_jmn_key_as_text(reemplazo);
    const char *h = th.type == JB_TYPE_TEXTO && th.u.str ? th.u.str : "";
    const char *n = tn.type == JB_TYPE_TEXTO && tn.u.str ? tn.u.str : "";
    const char *r = tr.type == JB_TYPE_TEXTO && tr.u.str ? tr.u.str : "";
    char out[8192];
    out[0] = '\0';
    size_t nlen = strlen(n);
    if (nlen == 0) {
        jb_var_t ret = jb_new_texto(h);
        jb_var_clear(&th);
        jb_var_clear(&tn);
        jb_var_clear(&tr);
        return ret;
    }
    char *wp = out;
    const char *sp = h;
    while (*sp && (size_t)(wp - out) < sizeof(out) - 1) {
        const char *pos = strstr(sp, n);
        if (!pos) {
            while (*sp && (size_t)(wp - out) < sizeof(out) - 1)
                *wp++ = *sp++;
            break;
        }
        while (sp < pos && (size_t)(wp - out) < sizeof(out) - 1)
            *wp++ = *sp++;
        for (const char *rp = r; *rp && (size_t)(wp - out) < sizeof(out) - 1; )
            *wp++ = *rp++;
        sp = pos + nlen;
    }
    *wp = '\0';
    {
        jb_var_t ret = jb_new_texto(out);
        jb_var_clear(&th);
        jb_var_clear(&tn);
        jb_var_clear(&tr);
        return ret;
    }
}

jb_var_t jb_dividir_texto(jb_var_t s, jb_var_t sep) {
    jb_var_t out = jb_new_list();
    const char *str;
    char delim[8];
    if (s.type != JB_TYPE_TEXTO || !s.u.str) return out;
    str = s.u.str;
    if (sep.type != JB_TYPE_TEXTO || !sep.u.str || !sep.u.str[0]) return out;
    delim[0] = sep.u.str[0];
    delim[1] = 0;
    while (*str) {
        size_t span = strcspn(str, delim);
        char *piece = (char *)malloc(span + 1);
        if (!piece) break;
        memcpy(piece, str, span);
        piece[span] = 0;
        jb_list_push(&out, jb_new_texto(piece));
        free(piece);
        str += span;
        if (*str == delim[0]) str++;
    }
    return out;
}

jb_var_t jb_codigo_caracter(jb_var_t s) {
    if (s.type != JB_TYPE_TEXTO || !s.u.str || !s.u.str[0]) return jb_new_entero(0);
    return jb_new_entero((unsigned char)s.u.str[0]);
}

jb_var_t jb_caracter_a_texto(jb_var_t code) {
    char b[8];
    int64_t c = code.type == JB_TYPE_ENTERO ? code.u.i64 : (int64_t)jb_jmn_as_f64(code, 0);
    b[0] = (char)(c & 255);
    b[1] = 0;
    return jb_new_texto(b);
}

jb_var_t jb_str_extraer_caracter(jb_var_t s, jb_var_t idx) {
    int64_t i = idx.type == JB_TYPE_ENTERO ? idx.u.i64 : (int64_t)jb_jmn_as_f64(idx, 0);
    if (s.type != JB_TYPE_TEXTO || !s.u.str) return jb_new_texto("");
    if (i < 0 || (size_t)i >= strlen(s.u.str)) return jb_new_texto("");
    return jb_caracter_a_texto(jb_new_entero((unsigned char)s.u.str[(size_t)i]));
}

jb_var_t jb_copiar_texto(jb_var_t s) {
    if (s.type != JB_TYPE_TEXTO || !s.u.str) return jb_new_texto("");
    return jb_new_texto(s.u.str);
}

jb_var_t jb_decimal(jb_var_t v, jb_var_t prec) {
    char b[128];
    int p = (int)(prec.type == JB_TYPE_ENTERO ? prec.u.i64 : jb_jmn_as_f64(prec, 2));
    if (p < 0) p = 0;
    if (p > 16) p = 16;
    snprintf(b, sizeof b, "%.*f", p, jb_jmn_as_f64(v, 0));
    return jb_new_texto(b);
}

jb_var_t jb_sin(jb_var_t v) { return jb_new_flotante_scalar(sin(jb_jmn_as_f64(v, 0))); }
jb_var_t jb_cos(jb_var_t v) { return jb_new_flotante_scalar(cos(jb_jmn_as_f64(v, 0))); }
jb_var_t jb_exp(jb_var_t v) { return jb_new_flotante_scalar(exp(jb_jmn_as_f64(v, 0))); }
jb_var_t jb_log10(jb_var_t v) { return jb_new_flotante_scalar(log10(jb_jmn_as_f64(v, 1))); }

jb_var_t jb_formatear_timestamp(jb_var_t ts, jb_var_t fmt) {
    const char *f = (fmt.type == JB_TYPE_TEXTO && fmt.u.str) ? fmt.u.str : "%Y-%m-%d";
    int64_t sec = (int64_t)jb_jmn_as_f64(ts, 0);
    time_t t = (time_t)sec;
    struct tm tm_buf;
    struct tm *ptm = NULL;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &t) == 0) ptm = &tm_buf;
#else
    ptm = localtime_r(&t, &tm_buf);
#endif
    char out[256];
    if (!ptm || strftime(out, sizeof out, f, ptm) == 0) return jb_new_texto(f);
    return jb_new_texto(out);
}

void jb_limpiar_consola(void) {
#ifdef _WIN32
    fputs("\033[2J\033[H", stdout);
#else
    fputs("\033[2J\033[H", stdout);
#endif
}

void jb_pausa_milisegundos(jb_var_t ms) {
    uint32_t m = (uint32_t)jb_jmn_as_f64(ms, 0);
#ifdef _WIN32
    Sleep(m);
#else
    usleep(m * 1000u);
#endif
}

jb_var_t jb_leer_entrada(void) {
    char buf[4096];
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin)) return jb_new_texto("");
    jb_line_trim_both(buf);
    return jb_new_texto(buf);
}

void jb_ingresar_texto(jb_var_t *dst) {
    fflush(stdout);
    jb_var_t ln = jb_leer_entrada();
    if (dst) {
        jb_var_clear(dst);
        *dst = ln;
    } else
        jb_var_clear(&ln);
}

static FILE *jb_handle_ptr(jb_var_t h) {
    if (h.type != JB_TYPE_ENTERO) return NULL;
    return (FILE *)(uintptr_t)(int64_t)h.u.i64;
}

jb_var_t jb_fs_abrir(jb_var_t path, jb_var_t mode) {
    const char *p, *m;
    FILE *f;
    jb_var_t pt = jb_jmn_key_as_text(path);
    jb_var_t mt = jb_jmn_key_as_text(mode);
    p = pt.type == JB_TYPE_TEXTO && pt.u.str ? pt.u.str : "";
    m = mt.type == JB_TYPE_TEXTO && mt.u.str ? mt.u.str : "r";
    f = fopen(p, m);
    jb_var_clear(&pt);
    jb_var_clear(&mt);
    if (!f) return jb_new_entero(0);
    return jb_new_entero((int64_t)(uintptr_t)f);
}

jb_var_t jb_fs_cerrar(jb_var_t h) {
    FILE *f = jb_handle_ptr(h);
    if (f) fclose(f);
    return jb_new_bool(true);
}

jb_var_t jb_fs_leer_linea(jb_var_t h) {
    char buf[4096];
    FILE *f = jb_handle_ptr(h);
    if (!f || !fgets(buf, sizeof buf, f)) return jb_new_texto("");
    jb_line_trim(buf);
    return jb_new_texto(buf);
}

jb_var_t jb_fs_escribir(jb_var_t data, jb_var_t h) {
    FILE *f = jb_handle_ptr(h);
    jb_var_t t = jb_jmn_key_as_text(data);
    const char *s = t.type == JB_TYPE_TEXTO && t.u.str ? t.u.str : "";
    if (f) fputs(s, f);
    jb_var_clear(&t);
    return jb_new_bool(f != NULL);
}

jb_var_t jb_fs_escribir_byte(jb_var_t byte_val, jb_var_t h) {
    FILE *f = jb_handle_ptr(h);
    int b = (int)jb_jmn_as_f64(byte_val, 0);
    if (f) fputc(b & 255, f);
    return jb_new_bool(f != NULL);
}

jb_var_t jb_fs_leer_byte(jb_var_t h) {
    FILE *f = jb_handle_ptr(h);
    int c = f ? fgetc(f) : EOF;
    if (c == EOF) return jb_new_entero(-1);
    return jb_new_entero(c);
}

jb_var_t jb_fs_fin_archivo(jb_var_t h) {
    FILE *f = jb_handle_ptr(h);
    return jb_new_bool(f ? feof(f) != 0 : true);
}

jb_var_t jb_existe_archivo(jb_var_t path) {
    jb_var_t pt = jb_jmn_key_as_text(path);
    const char *p = pt.type == JB_TYPE_TEXTO && pt.u.str ? pt.u.str : "";
    FILE *t = fopen(p, "rb");
    jb_var_clear(&pt);
    if (t) {
        fclose(t);
        return jb_new_entero(1);
    }
    return jb_new_entero(0);
}

jb_var_t jb_fs_leer_texto(jb_var_t path) {
    long sz;
    char *buf;
    FILE *f;
    jb_var_t pt = jb_jmn_key_as_text(path);
    const char *p = pt.type == JB_TYPE_TEXTO && pt.u.str ? pt.u.str : "";
    f = fopen(p, "rb");
    jb_var_clear(&pt);
    if (!f) return jb_new_texto("");
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return jb_new_texto("");
    }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);
    {
        jb_var_t r = jb_new_texto(buf);
        free(buf);
        return r;
    }
}

jb_var_t jb_fs_borrar(jb_var_t path) {
    jb_var_t pt = jb_jmn_key_as_text(path);
    const char *p = pt.type == JB_TYPE_TEXTO && pt.u.str ? pt.u.str : "";
    int r = remove(p);
    jb_var_clear(&pt);
    /* Paridad VM: valor devuelto es el codigo de remove (0 = exito). */
    return jb_new_entero(r);
}

jb_var_t jb_fs_copiar(jb_var_t src, jb_var_t dst) {
    char buf[8192];
    FILE *a, *b;
    jb_var_t s1 = jb_jmn_key_as_text(src);
    jb_var_t s2 = jb_jmn_key_as_text(dst);
    const char *ps = s1.type == JB_TYPE_TEXTO && s1.u.str ? s1.u.str : "";
    const char *pd = s2.type == JB_TYPE_TEXTO && s2.u.str ? s2.u.str : "";
    a = fopen(ps, "rb");
    b = fopen(pd, "wb");
    jb_var_clear(&s1);
    jb_var_clear(&s2);
    if (!a || !b) {
        if (a) fclose(a);
        if (b) fclose(b);
        return jb_new_entero(-1);
    }
    while (1) {
        size_t n = fread(buf, 1, sizeof buf, a);
        if (n == 0) break;
        fwrite(buf, 1, n, b);
    }
    fclose(a);
    fclose(b);
    /* Paridad VM OP_FS_COPIAR: 0 = exito, -1 = fallo */
    return jb_new_entero(0);
}

jb_var_t jb_fs_mover(jb_var_t src, jb_var_t dst) {
    jb_var_t c = jb_fs_copiar(src, dst);
    int ok = (c.type == JB_TYPE_ENTERO && c.u.i64 == 0);
    jb_var_clear(&c);
    if (!ok) return jb_new_entero(-1);
    return jb_fs_borrar(src);
}

jb_var_t jb_fs_tamano(jb_var_t path) {
    long sz;
    FILE *f;
    jb_var_t pt = jb_jmn_key_as_text(path);
    const char *p = pt.type == JB_TYPE_TEXTO && pt.u.str ? pt.u.str : "";
    f = fopen(p, "rb");
    jb_var_clear(&pt);
    if (!f) return jb_new_entero(0);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fclose(f);
    return jb_new_entero(sz < 0 ? 0 : sz);
}

jb_var_t jb_fs_listar(jb_var_t pattern) {
    jb_var_t pt = jb_jmn_key_as_text(pattern);
    const char *pat = pt.type == JB_TYPE_TEXTO && pt.u.str ? pt.u.str : "*";
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *buf = (char *)malloc(1);
    size_t len = 0, cap = 1;
    int first = 1;
    if (buf) buf[0] = 0;
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE || !buf) {
        free(buf);
        jb_var_clear(&pt);
        return jb_new_texto("");
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        {
            size_t nl = strlen(fd.cFileName);
            size_t add = nl + (first ? 0 : 1);
            if (len + add + 1 > cap) {
                size_t nc = cap;
                while (len + add + 1 > nc) nc = nc * 2 + 64;
                buf = (char *)realloc(buf, nc);
                if (!buf) {
                    FindClose(h);
                    jb_var_clear(&pt);
                    return jb_new_texto("");
                }
                cap = nc;
            }
            if (!first) buf[len++] = '\n';
            memcpy(buf + len, fd.cFileName, nl);
            len += nl;
            buf[len] = 0;
            first = 0;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    jb_var_clear(&pt);
    {
        jb_var_t r = jb_new_texto(buf);
        free(buf);
        return r;
    }
#else
    (void)pat;
    jb_var_clear(&pt);
    return jb_new_texto("");
#endif
}

jb_var_t jb_sistema_ejecutar(jb_var_t cmd) {
    (void)cmd;
    return jb_new_entero(-1);
}

static void jb_vec_set(jb_var_t *v, jb_type_t t, int rows, int cols, const float *d) {
    size_t n = (size_t)(rows * cols), i;
    v->type = t;
    v->u.vmat.rows = rows;
    v->u.vmat.cols = cols;
    for (i = 0; i < n && i < 16; i++) v->u.vmat.data[i] = d[i];
}

jb_var_t jb_new_vec2_from_jbvals(jb_var_t x, jb_var_t y) {
    jb_var_t v;
    float d[4] = { (float)jb_jmn_as_f64(x, 0), (float)jb_jmn_as_f64(y, 0), 0, 0 };
    jb_vec_set(&v, JB_TYPE_VEC2, 1, 2, d);
    return v;
}

jb_var_t jb_new_vec3_from_jbvals(jb_var_t x, jb_var_t y, jb_var_t z) {
    jb_var_t v;
    float d[4] = { (float)jb_jmn_as_f64(x, 0), (float)jb_jmn_as_f64(y, 0), (float)jb_jmn_as_f64(z, 0), 0 };
    jb_vec_set(&v, JB_TYPE_VEC3, 1, 3, d);
    return v;
}

jb_var_t jb_new_vec4_from_jbvals(jb_var_t x, jb_var_t y, jb_var_t z, jb_var_t w) {
    jb_var_t v;
    float d[4] = { (float)jb_jmn_as_f64(x, 0), (float)jb_jmn_as_f64(y, 0), (float)jb_jmn_as_f64(z, 0), (float)jb_jmn_as_f64(w, 0) };
    jb_vec_set(&v, JB_TYPE_VEC4, 1, 4, d);
    return v;
}

jb_var_t jb_vec_longitud(jb_var_t v) {
    double s = 0;
    int n = 0, i;
    if (v.type == JB_TYPE_VEC2) n = 2;
    else if (v.type == JB_TYPE_VEC3)
        n = 3;
    else if (v.type == JB_TYPE_VEC4)
        n = 4;
    else
        return jb_new_flotante_scalar(0);
    for (i = 0; i < n; i++) s += (double)v.u.vmat.data[i] * (double)v.u.vmat.data[i];
    return jb_new_flotante_scalar(sqrt(s));
}

void jb_vec3_normalizar(jb_var_t *out, jb_var_t *axis) {
    double L = 0;
    int i;
    if (!out || !axis) return;
    for (i = 0; i < 3; i++) L += (double)axis->u.vmat.data[i] * (double)axis->u.vmat.data[i];
    L = sqrt(L);
    if (L < 1e-12) L = 1.0;
    out->type = JB_TYPE_VEC3;
    out->u.vmat.rows = 1;
    out->u.vmat.cols = 3;
    for (i = 0; i < 3; i++) out->u.vmat.data[i] = (float)((double)axis->u.vmat.data[i] / L);
}

jb_var_t jb_new_mat4_zero(void) {
    jb_var_t m;
    float z[16] = { 0 };
    jb_vec_set(&m, JB_TYPE_MAT4, 4, 4, z);
    return m;
}

void jb_mat4_identidad(jb_var_t *m) {
    int i;
    float id[16] = { 0 };
    if (!m) return;
    for (i = 0; i < 4; i++) id[i * 4 + i] = 1.0f;
    jb_vec_set(m, JB_TYPE_MAT4, 4, 4, id);
}

void jb_mat4_mul_vec4(jb_var_t *out, jb_var_t *m, jb_var_t *v) {
    int r, c;
    float res[4];
    if (!out || !m || !v) return;
    for (r = 0; r < 4; r++) {
        double s = 0;
        for (c = 0; c < 4; c++) s += (double)m->u.vmat.data[r * 4 + c] * (double)v->u.vmat.data[c];
        res[r] = (float)s;
    }
    jb_vec_set(out, JB_TYPE_VEC4, 1, 4, res);
}

jb_var_t jb_vec_mat_component(jb_var_t v, const char *name) {
    int idx = -1;
    if (!name) return jb_new_flotante_scalar(0);
    if (strcmp(name, "x") == 0 || strcmp(name, "r") == 0 || strcmp(name, "e0") == 0) idx = 0;
    else if (strcmp(name, "y") == 0 || strcmp(name, "g") == 0 || strcmp(name, "e1") == 0)
        idx = 1;
    else if (strcmp(name, "z") == 0 || strcmp(name, "b") == 0 || strcmp(name, "e2") == 0)
        idx = 2;
    else if (strcmp(name, "w") == 0 || strcmp(name, "a") == 0 || strcmp(name, "e3") == 0)
        idx = 3;
    if (idx < 0 || idx >= 16) return jb_new_flotante_scalar(0);
    return jb_new_flotante_scalar((double)v.u.vmat.data[idx]);
}

void jb_put_vec_mat_member(jb_var_t *v, const char *name, jb_var_t val) {
    float f = (float)jb_jmn_as_f64(val, 0);
    int idx = -1;
    if (!v || !name) return;
    if (strcmp(name, "x") == 0 || strcmp(name, "e0") == 0) idx = 0;
    else if (strcmp(name, "y") == 0 || strcmp(name, "e1") == 0)
        idx = 1;
    else if (strcmp(name, "z") == 0 || strcmp(name, "e2") == 0)
        idx = 2;
    else if (strcmp(name, "w") == 0 || strcmp(name, "e3") == 0)
        idx = 3;
    if (idx >= 0 && idx < 16) v->u.vmat.data[idx] = f;
}

jb_var_t jb_member_get(jb_var_t v, const char *member) {
    jb_var_t k;
    jb_var_t r;
    if (!member) return jb_new_nulo();
    if (v.type == JB_TYPE_MAP && v.u.map) {
        k = jb_new_texto(member);
        r = jb_map_get(v, k);
        jb_var_clear(&k);
        return r;
    }
    if (v.type == JB_TYPE_VEC2 || v.type == JB_TYPE_VEC3 || v.type == JB_TYPE_VEC4 || v.type == JB_TYPE_MAT3 ||
        v.type == JB_TYPE_MAT4)
        return jb_vec_mat_component(v, member);
    return jb_new_nulo();
}

void jb_put_member_leaf(jb_var_t *obj, const char *leaf, jb_var_t val) {
    jb_var_t k;
    if (!obj || !leaf) return;
    if (obj->type == JB_TYPE_MAP && obj->u.map) {
        k = jb_new_texto(leaf);
        jb_map_put(obj, k, val);
        jb_var_clear(&k);
        return;
    }
    jb_put_vec_mat_member(obj, leaf, val);
}

static void jb_ring_push_i64(int64_t *ring, size_t cap, size_t *head, size_t *count, int64_t v) {
    if (cap == 0) return;
    ring[*head] = v;
    *head = (*head + 1) % cap;
    if (*count < cap) (*count)++;
}

static int64_t jb_ring_get_i64(const int64_t *ring, size_t cap, size_t head, size_t count, size_t idx_from_recent) {
    size_t pos;
    if (idx_from_recent >= count) return 0;
    pos = (head + cap - 1 - idx_from_recent) % cap;
    return ring[pos];
}

jb_var_t jb_ventana_percepcion(jb_var_t tam) {
    size_t n = (size_t)jb_jmn_as_f64(tam, 0);
    if (n > JB_RING_MAX) n = JB_RING_MAX;
    if (n == 0) n = 16;
    g_perc_cap = n;
    g_perc_head = g_perc_count = 0;
    return jb_new_bool(true);
}

jb_var_t jb_flujo_temporal(jb_var_t tam) { return jb_ventana_percepcion(tam); }

void jb_percepcion_limpiar(void) { g_perc_head = g_perc_count = 0; }

jb_var_t jb_percepcion_tamano(void) { return jb_new_entero((int64_t)g_perc_count); }

jb_var_t jb_percepcion_anterior(jb_var_t idx) {
    size_t i = (size_t)jb_jmn_as_f64(idx, 0);
    return jb_new_entero(jb_ring_get_i64(g_perc_ring, g_perc_cap, g_perc_head, g_perc_count, i));
}

jb_var_t jb_percepcion_recientes(void) {
    jb_var_t L = jb_new_list();
    size_t j;
    for (j = 0; j < g_perc_count && j < g_perc_cap; j++) jb_list_push(&L, jb_new_entero(jb_ring_get_i64(g_perc_ring, g_perc_cap, g_perc_head, g_perc_count, j)));
    return L;
}

jb_var_t jb_percepcion(jb_var_t idv) {
    jb_ring_push_i64(g_perc_ring, g_perc_cap, &g_perc_head, &g_perc_count, jb_jmn_as_f64(idv, 0));
    return jb_new_bool(true);
}

jb_var_t jb_ventana_rastro_activacion(jb_var_t tam) { return jb_rastro_activacion_ventana(tam); }

jb_var_t jb_rastro_activacion_ventana(jb_var_t tam) {
    size_t n = (size_t)jb_jmn_as_f64(tam, 0);
    if (n > JB_RING_MAX) n = JB_RING_MAX;
    if (n == 0) n = 64;
    g_rastro_cap = n;
    g_rastro_head = g_rastro_count = 0;
    return jb_new_bool(true);
}

void jb_rastro_activacion_limpiar(void) { g_rastro_head = g_rastro_count = 0; }

jb_var_t jb_rastro_activacion_tamano(void) { return jb_new_entero((int64_t)g_rastro_count); }

jb_var_t jb_rastro_activacion_obtener(jb_var_t idx) {
    size_t i = (size_t)jb_jmn_as_f64(idx, 0);
    return jb_new_entero(jb_ring_get_i64(g_rastro_ring, g_rastro_cap, g_rastro_head, g_rastro_count, i));
}

jb_var_t jb_rastro_activacion_peso(jb_var_t idx) {
    size_t i = (size_t)jb_jmn_as_f64(idx, 0);
    size_t pos;
    if (i >= g_rastro_count) return jb_new_flotante_scalar(0);
    pos = (g_rastro_head + g_rastro_cap - 1 - i) % g_rastro_cap;
    return jb_new_flotante_scalar(g_rastro_w[pos]);
}

jb_var_t jb_rastro_activacion_lista(void) { return jb_rastro_activacion_recientes(); }

jb_var_t jb_rastro_activacion_recientes(void) {
    jb_var_t L = jb_new_list();
    size_t j;
    for (j = 0; j < g_rastro_count && j < g_rastro_cap; j++) jb_list_push(&L, jb_new_entero(jb_ring_get_i64(g_rastro_ring, g_rastro_cap, g_rastro_head, g_rastro_count, j)));
    return L;
}

static void jb_rastro_push_id(int64_t id, double w) {
    if (g_rastro_cap == 0) return;
    g_rastro_ring[g_rastro_head] = id;
    g_rastro_w[g_rastro_head] = w;
    g_rastro_head = (g_rastro_head + 1) % g_rastro_cap;
    if (g_rastro_count < g_rastro_cap) g_rastro_count++;
}

jb_var_t jb_buscar_en_memoria(jb_var_t termino) {
    jb_var_t t = jb_jmn_key_as_text(termino);
    const char *q = t.type == JB_TYPE_TEXTO && t.u.str ? t.u.str : "";
    JMNMemoria *m = jb_jmn_rt_mem();
    if (m && q[0]) {
        JMNBusquedaIntrospectivaResultado res[4];
        int n = jmn_buscar_introspectiva(m, q, res, 1u, 0);
        if (n > 0 && res[0].texto[0]) {
            jb_var_t r = jb_new_texto(res[0].texto);
            jb_jmn_set_resultado(r);
            jb_var_clear(&r);
            jb_var_clear(&t);
            return jb_var_clone(jb_resultado_global);
        }
    }
    jb_var_clear(&t);
    jb_jmn_set_resultado(jb_new_texto(""));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_buscar_en_memoria_lista(jb_var_t termino, jb_var_t maxn) {
    jb_var_t L = jb_new_list();
    int max = (int)jb_jmn_as_f64(maxn, 10);
    jb_var_t t = jb_jmn_key_as_text(termino);
    const char *q = t.type == JB_TYPE_TEXTO && t.u.str ? t.u.str : "";
    JMNMemoria *m = jb_jmn_rt_mem();
    if (max < 1) max = 1;
    if (max > 100) max = 100;
    if (m && q[0]) {
        uint32_t *ids = (uint32_t *)calloc((size_t)max, sizeof(uint32_t));
        int n, j;
        if (ids) {
            n = jmn_buscar_introspectiva_lista(m, q, ids, (uint32_t)max, 0);
            for (j = 0; j < n && j < max; j++) {
                char buf[256];
                if (jmn_obtener_texto(m, ids[j], buf, sizeof buf) >= 0 && buf[0])
                    jb_list_push(&L, jb_new_texto(buf));
            }
            free(ids);
        }
    }
    jb_var_clear(&t);
    jb_jmn_set_resultado(L);
    return L;
}

jb_var_t jb_buscar_en_memoria_cs(jb_var_t termino, jb_var_t case_sens) {
    jb_var_t t = jb_jmn_key_as_text(termino);
    const char *q = t.type == JB_TYPE_TEXTO && t.u.str ? t.u.str : "";
    int cs = jb_truthy(case_sens);
    JMNMemoria *mem = jb_jmn_rt_mem();
    if (mem && q[0]) {
        JMNBusquedaIntrospectivaResultado res[2];
        int n = jmn_buscar_introspectiva(mem, q, res, 1u, cs);
        if (n > 0 && res[0].texto[0]) {
            jb_var_t r = jb_new_texto(res[0].texto);
            jb_jmn_set_resultado(r);
            jb_var_clear(&r);
            jb_var_clear(&t);
            return jb_var_clone(jb_resultado_global);
        }
    }
    jb_var_clear(&t);
    jb_jmn_set_resultado(jb_new_texto(""));
    return jb_var_clone(jb_resultado_global);
}

jb_var_t jb_buscar_en_memoria_detallada(jb_var_t termino, jb_var_t maxn, jb_var_t case_sens) {
    (void)case_sens;
    return jb_buscar_en_memoria_lista(termino, maxn);
}

typedef struct {
    jb_var_t *list;
} JbRecolectarTodoCtx;

static int jb_cb_recolectar_concepto_texto(JMNNodo *nodo, void *ud) {
    JbRecolectarTodoCtx *c = (JbRecolectarTodoCtx *)ud;
    JMNMemoria *m = jb_jmn_rt_mem();
    char buf[256];
    if (!m || !c || !c->list)
        return 0;
    if (jmn_obtener_texto(m, nodo->id, buf, sizeof buf) >= 0 && buf[0])
        jb_list_push(c->list, jb_new_texto(buf));
    return 0;
}

jb_var_t jb_obtener_todos_conceptos(void) {
    jb_var_t L = jb_new_list();
    JMNMemoria *m = jb_jmn_rt_mem();
    if (m) {
        JbRecolectarTodoCtx cx;
        cx.list = &L;
        (void)jmn_iterar_nodos(m, jb_cb_recolectar_concepto_texto, &cx);
    }
    jb_jmn_set_resultado(L);
    return L;
}

jb_var_t jb_obtener_relacionados(jb_var_t concepto) { return jb_buscar_asociados(concepto, jb_new_flotante_scalar(0)); }

jb_var_t jb_obtener_nombre_concepto(jb_var_t idv) { return jb_jmn_key_as_text(idv); }

void jb_imprimir_id(jb_var_t idv) {
    if (idv.type == JB_TYPE_ENTERO) printf("%lld\n", (long long)idv.u.i64);
    else
        jb_imprimir(idv);
}

jb_var_t jb_propiedad_concepto(jb_var_t concepto, jb_var_t prop) {
    (void)concepto;
    (void)prop;
    return jb_new_nulo();
}

jb_var_t jb_asociar_relacion(jb_var_t a, jb_var_t b, jb_var_t f) { return jb_asociar(a, b, f); }
jb_var_t jb_asociar_similitud(jb_var_t a, jb_var_t b, jb_var_t f) { return jb_asociar(a, b, f); }
jb_var_t jb_asociar_diferencia(jb_var_t a, jb_var_t b, jb_var_t f) { return jb_asociar(a, b, f); }

jb_var_t jb_comparar_patrones(jb_var_t a, jb_var_t b) {
    jb_var_t ta = jb_jmn_key_as_text(a);
    jb_var_t tb = jb_jmn_key_as_text(b);
    const char *sa = ta.type == JB_TYPE_TEXTO && ta.u.str ? ta.u.str : "";
    const char *sb = tb.type == JB_TYPE_TEXTO && tb.u.str ? tb.u.str : "";
    double score = strcmp(sa, sb) == 0 ? 1.0 : 0.0;
    jb_var_clear(&ta);
    jb_var_clear(&tb);
    return jb_new_flotante_scalar(score);
}

jb_var_t jb_decae_conexiones(jb_var_t factor) { return jb_decaer_conexiones(factor); }

jb_var_t jb_pensar(jb_var_t semilla) {
    jb_var_t r = jb_buscar(semilla);
    jb_rastro_push_id(jb_jmn_as_f64(r, 0), 1.0);
    return r;
}

jb_var_t jb_pensar_respuesta(jb_var_t entrada, jb_var_t creatividad, jb_var_t umbral) {
    (void)creatividad;
    (void)umbral;
    return jb_pensar(entrada);
}

jb_var_t jb_procesar_texto(jb_var_t texto) {
    jb_var_t parts = jb_dividir_texto(texto, jb_new_texto(" "));
    size_t i;
    if (parts.type == JB_TYPE_LIST && parts.u.lst) {
        for (i = 0; i + 1 < parts.u.lst->len; i++) {
            jb_asociar(parts.u.lst->items[i], parts.u.lst->items[i + 1], jb_new_flotante_scalar(0.15));
        }
    }
    jb_var_clear(&parts);
    jb_jmn_set_resultado(jb_new_bool(true));
    return jb_var_clone(jb_resultado_global);
}

static uint32_t jb_hash32(const char *s) {
    uint32_t h = 2166136261u;
    if (!s) return h;
    for (; *s; s++) h = (h ^ (uint32_t)(unsigned char)*s) * 16777619u;
    return h;
}

static uint32_t jb_vm_hash_texto_local(const char *texto) {
    uint32_t hash = 5381u;
    if (!texto) return 0;
    for (const unsigned char *p = (const unsigned char *)texto; *p; p++)
        hash = ((hash << 5) + hash) + *p;
    return hash;
}

static uint32_t jb_aot_concept_id_from_var(jb_var_t v) {
    jb_var_t kt = jb_jmn_key_as_text(v);
    const char *s = (kt.type == JB_TYPE_TEXTO && kt.u.str) ? kt.u.str : "";
    uint32_t id = jb_vm_hash_texto_local(s);
    if (id == 0u) id = 5381u;
    jb_var_clear(&kt);
    return id;
}

jb_var_t jb_elegir_por_peso(jb_var_t lista, jb_var_t ctx) { return jb_elegir_por_peso_segun(lista, ctx); }

jb_var_t jb_elegir_por_peso_segun(jb_var_t lista, jb_var_t ctx) {
    const char *seed = (ctx.type == JB_TYPE_TEXTO && ctx.u.str) ? ctx.u.str : "";
    uint32_t h = jb_hash32(seed);
    jb_list_t *L;
    size_t i, n;
    double sum = 0, *w = NULL, r, acc;
    JMNMemoria *m = jb_jmn_rt_mem();
    uint32_t id_ctx = jb_aot_concept_id_from_var(ctx);
    (void)h;
    if (lista.type != JB_TYPE_LIST || !lista.u.lst) return jb_new_entero(0);
    L = lista.u.lst;
    n = L->len;
    if (n == 0) return jb_new_entero(0);
    w = (double *)calloc(n, sizeof(double));
    if (!w) return jb_new_entero(0);
    for (i = 0; i < n; i++) {
        double wsum = 0;
        uint32_t id_item = jb_aot_concept_id_from_var(L->items[i]);
        if (m) {
            float s1 = jmn_obtener_fuerza_asociacion(m, id_ctx, id_item);
            float s2 = jmn_obtener_fuerza_asociacion(m, id_item, id_ctx);
            wsum = (double)s1 > (double)s2 ? (double)s1 : (double)s2;
        }
        w[i] = wsum <= 0 ? 1.0 : wsum;
        sum += w[i];
    }
    r = fmod((double)h * 0.0001 + 0.37, 1.0) * sum;
    acc = 0;
    for (i = 0; i < n; i++) {
        acc += w[i];
        if (r <= acc) {
            free(w);
            return jb_new_entero((int64_t)i);
        }
    }
    free(w);
    return jb_new_entero((int64_t)(n - 1));
}

jb_var_t jb_elegir_por_peso_id(jb_var_t lista, jb_var_t ctx) {
    jb_var_t idx = jb_elegir_por_peso_segun(lista, ctx);
    int64_t i = idx.u.i64;
    if (lista.type != JB_TYPE_LIST || !lista.u.lst || i < 0 || (size_t)i >= lista.u.lst->len) return jb_new_entero(0);
    return jb_var_clone(lista.u.lst->items[(size_t)i]);
}

jb_var_t jb_elegir_por_peso_semilla(jb_var_t lista, jb_var_t ctx, jb_var_t seed) {
    (void)seed;
    return jb_elegir_por_peso_segun(lista, ctx);
}

jb_var_t jb_elegir_por_peso_seed(jb_var_t lista, jb_var_t ctx, jb_var_t seed) {
    return jb_elegir_por_peso_semilla(lista, ctx, seed);
}
