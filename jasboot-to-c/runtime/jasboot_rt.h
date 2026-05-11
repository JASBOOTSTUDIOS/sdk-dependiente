#ifndef JASBOOT_RT_H
#define JASBOOT_RT_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct jb_json_node;
typedef struct jb_json_node jb_json_node_t;

#define JB_JSON_NULL 0u
#define JB_JSON_BOOL 1u
#define JB_JSON_INT 2u
#define JB_JSON_FLOAT 3u
#define JB_JSON_STRING 4u
#define JB_JSON_ARRAY 5u
#define JB_JSON_OBJECT 6u

typedef struct jb_list jb_list_t;
typedef struct jb_map jb_map_t;

typedef enum {
    JB_TYPE_NULL = 0,
    JB_TYPE_ENTERO,
    JB_TYPE_FLOTANTE,
    JB_TYPE_TEXTO,
    JB_TYPE_BOOL,
    JB_TYPE_LIST,
    JB_TYPE_MAP,
    JB_TYPE_JSON,
    JB_TYPE_VEC2,
    JB_TYPE_VEC3,
    JB_TYPE_VEC4,
    JB_TYPE_MAT3,
    JB_TYPE_MAT4
} jb_type_t;

typedef struct {
    jb_type_t type;
    union {
        int64_t i64;
        double f64;
        bool b;
        char *str;
        jb_list_t *lst;
        jb_map_t *map;
        jb_json_node_t *json;
        struct {
            float data[16];
            int rows;
            int cols;
        } vmat;
    } u;
} jb_var_t;

struct jb_list {
    jb_var_t *items;
    size_t len;
    size_t cap;
};

struct jb_map {
    jb_var_t *keys;
    jb_var_t *vals;
    size_t len;
    size_t cap;
};

extern jb_var_t jb_resultado_global;
extern jb_var_t g_last_throw;
extern int jb_try_depth;
extern jmp_buf jb_try_stack[32];

void jb_init(void);
void jb_cleanup(void);
void jb_set_argv(int argc, char **argv);

jb_var_t jb_new_nulo(void);
jb_var_t jb_new_entero(int64_t v);
jb_var_t jb_new_flotante_scalar(double v);
jb_var_t jb_new_flotante_from_var(jb_var_t v);
/*
 * Compatibilidad: literales (int/double), jb_var_t (p. ej. jb_vec_longitud(...)), float, etc.
 * Un solo puntero + discriminador _Generic evita casts ilegales entre ramas.
 */
enum {
    JB_NF_JBVAR = 0,
    JB_NF_FLOAT,
    JB_NF_DOUBLE,
    JB_NF_LONGDOUBLE,
    JB_NF_LLONG,
    JB_NF_ULLONG,
    JB_NF_LONG,
    JB_NF_ULONG,
    JB_NF_INT,
    JB_NF_UINT,
    JB_NF_SHORT,
    JB_NF_USHORT,
    JB_NF_SCHAR,
    JB_NF_UCHAR,
    JB_NF_CHAR,
    JB_NF_BOOL
};
jb_var_t jb_new_flotante_dispatch(int kind, const void *addr);
#define jb_new_flotante(X)                                                                               \
    jb_new_flotante_dispatch(                                                                            \
        _Generic((X), jb_var_t                                                                            \
                 : JB_NF_JBVAR, float                                                                     \
                   : JB_NF_FLOAT, double                                                                  \
                     : JB_NF_DOUBLE, long double                                                          \
                       : JB_NF_LONGDOUBLE, long long                                                      \
                         : JB_NF_LLONG, unsigned long long                                                \
                           : JB_NF_ULLONG, long                                                           \
                             : JB_NF_LONG, unsigned long                                                  \
                               : JB_NF_ULONG, int                                                         \
                                 : JB_NF_INT, unsigned int                                                \
                                   : JB_NF_UINT, short                                                    \
                                     : JB_NF_SHORT, unsigned short                                        \
                                       : JB_NF_USHORT, signed char                                        \
                                         : JB_NF_SCHAR, unsigned char                                     \
                                           : JB_NF_UCHAR, char                                            \
                                             : JB_NF_CHAR, _Bool                                         \
                                               : JB_NF_BOOL, default                                      \
                                                 : JB_NF_INT),                                           \
        (const void *)&(__typeof__(X) const[]){(X)})
jb_var_t jb_new_texto(const char *v);
jb_var_t jb_new_bool(bool v);
jb_var_t jb_new_list(void);
jb_var_t jb_new_map(void);

void jb_var_clear(jb_var_t *v);
jb_var_t jb_var_clone(jb_var_t v);

void jb_throw_val(jb_var_t err);

void jb_imprimir(jb_var_t v);
void jb_imprimir_sin_salto(jb_var_t v);
void jb_imprimir_flotante(jb_var_t v);
void jb_warn_aot(const char *msg);
/** Para expresiones AOT donde hace falta un jb_var_t tras un aviso. */
jb_var_t jb_warn_aot_expr(const char *msg);

int jb_truthy(jb_var_t v);

jb_var_t jb_add(jb_var_t a, jb_var_t b);
jb_var_t jb_sub(jb_var_t a, jb_var_t b);
jb_var_t jb_mul(jb_var_t a, jb_var_t b);
jb_var_t jb_mod(jb_var_t a, jb_var_t b);
jb_var_t jb_div(jb_var_t a, jb_var_t b);

jb_var_t jb_eq(jb_var_t a, jb_var_t b);
jb_var_t jb_ne(jb_var_t a, jb_var_t b);
jb_var_t jb_lt(jb_var_t a, jb_var_t b);
jb_var_t jb_le(jb_var_t a, jb_var_t b);
jb_var_t jb_gt(jb_var_t a, jb_var_t b);
jb_var_t jb_ge(jb_var_t a, jb_var_t b);
jb_var_t jb_land(jb_var_t a, jb_var_t b);
jb_var_t jb_lor(jb_var_t a, jb_var_t b);
jb_var_t jb_not(jb_var_t a);

jb_var_t jb_bit_shl(jb_var_t a, jb_var_t b);
jb_var_t jb_bit_shr(jb_var_t a, jb_var_t b);

jb_var_t jb_concat(jb_var_t a, jb_var_t b);
jb_var_t jb_texto_desde_numero(jb_var_t v);
jb_var_t jb_entero_a_texto(jb_var_t v);
jb_var_t jb_str_a_entero(jb_var_t v);
jb_var_t jb_str_a_flotante(jb_var_t v);
jb_var_t jb_texto_len(jb_var_t v);
jb_var_t jb_extraer_subtexto(jb_var_t s, jb_var_t a, jb_var_t b);
jb_var_t jb_contiene_texto(jb_var_t s, jb_var_t sub);
jb_var_t jb_termina_con(jb_var_t s, jb_var_t suf);
jb_var_t jb_minusculas(jb_var_t s);
jb_var_t jb_reemplazar(jb_var_t hay, jb_var_t patron, jb_var_t reemplazo);
jb_var_t jb_dividir_texto(jb_var_t s, jb_var_t sep);
jb_var_t jb_codigo_caracter(jb_var_t s);
jb_var_t jb_caracter_a_texto(jb_var_t code);
jb_var_t jb_str_extraer_caracter(jb_var_t s, jb_var_t idx);
jb_var_t jb_copiar_texto(jb_var_t s);
jb_var_t jb_decimal(jb_var_t v, jb_var_t prec);
jb_var_t jb_sin(jb_var_t v);
jb_var_t jb_cos(jb_var_t v);
jb_var_t jb_exp(jb_var_t v);
jb_var_t jb_log10(jb_var_t v);

jb_var_t jb_formatear_timestamp(jb_var_t ts, jb_var_t fmt);

void jb_limpiar_consola(void);
void jb_pausa_milisegundos(jb_var_t ms);

jb_var_t jb_leer_entrada(void);
void jb_ingresar_texto(jb_var_t *dst);

void jb_assign(jb_var_t *dst, jb_var_t src);
void jb_index_set(jb_var_t *container, jb_var_t idx, jb_var_t val);

void jb_list_push(jb_var_t *list_var, jb_var_t item);
jb_var_t jb_list_get(jb_var_t list, jb_var_t idx);
jb_var_t jb_list_len(jb_var_t list);
void jb_list_clear(jb_var_t *list_var);
void jb_list_release_in_place(jb_var_t *list_var);
void jb_list_set(jb_var_t *list_var, jb_var_t idx, jb_var_t val);

jb_var_t jb_map_get(jb_var_t map, jb_var_t key);
void jb_map_put(jb_var_t *map_var, jb_var_t key, jb_var_t val);
jb_var_t jb_map_len(jb_var_t map);
jb_var_t jb_map_has(jb_var_t map, jb_var_t key);
/* Lectura/escritura de miembro: mapa (clave texto) o componente vec/mat (x,y,z,w,...). */
jb_var_t jb_member_get(jb_var_t v, const char *member);
void jb_put_member_leaf(jb_var_t *obj, const char *leaf, jb_var_t val);
void jb_map_remove(jb_var_t *map_var, jb_var_t key);
jb_var_t jb_map_val_at(jb_var_t map, jb_var_t idx);
jb_var_t jb_map_key_at(jb_var_t map, jb_var_t idx);

jb_var_t jb_fs_abrir(jb_var_t path, jb_var_t mode);
jb_var_t jb_fs_cerrar(jb_var_t h);
jb_var_t jb_fs_leer_linea(jb_var_t h);
jb_var_t jb_fs_escribir(jb_var_t data, jb_var_t h);
jb_var_t jb_fs_escribir_byte(jb_var_t byte_val, jb_var_t h);
jb_var_t jb_fs_leer_byte(jb_var_t h);
jb_var_t jb_fs_fin_archivo(jb_var_t h);
jb_var_t jb_existe_archivo(jb_var_t path);
jb_var_t jb_fs_leer_texto(jb_var_t path);
jb_var_t jb_fs_borrar(jb_var_t path);
jb_var_t jb_fs_copiar(jb_var_t src, jb_var_t dst);
jb_var_t jb_fs_mover(jb_var_t src, jb_var_t dst);
jb_var_t jb_fs_tamano(jb_var_t path);
jb_var_t jb_fs_listar(jb_var_t pattern);

jb_var_t jb_sistema_ejecutar(jb_var_t cmd);

void jb_json_free_tree(jb_json_node_t *n);
jb_json_node_t *jb_json_clone_subtree(const jb_json_node_t *src);
void jb_json_print_compact(jb_var_t v);
jb_var_t jb_json_parse(jb_var_t text);
jb_var_t jb_json_stringify(jb_var_t root, jb_var_t indent);
jb_var_t jb_json_objeto_obtener(jb_var_t root, jb_var_t key);
jb_var_t jb_json_lista_obtener(jb_var_t root, jb_var_t idx);
jb_var_t jb_json_lista_tamano(jb_var_t root);
jb_var_t jb_json_a_texto(jb_var_t root);
jb_var_t jb_json_a_entero(jb_var_t root);
jb_var_t jb_json_a_flotante(jb_var_t root);
jb_var_t jb_json_a_bool(jb_var_t root);
jb_var_t jb_json_tipo(jb_var_t root);

/* --- Vectores / matrices (AOT) --- */
jb_var_t jb_new_vec2_from_jbvals(jb_var_t x, jb_var_t y);
jb_var_t jb_new_vec3_from_jbvals(jb_var_t x, jb_var_t y, jb_var_t z);
jb_var_t jb_new_vec4_from_jbvals(jb_var_t x, jb_var_t y, jb_var_t z, jb_var_t w);
jb_var_t jb_vec_longitud(jb_var_t v);
void jb_vec3_normalizar(jb_var_t *out, jb_var_t *axis);
jb_var_t jb_new_mat4_zero(void);
void jb_mat4_identidad(jb_var_t *m);
void jb_mat4_mul_vec4(jb_var_t *out, jb_var_t *m, jb_var_t *v);
jb_var_t jb_vec_mat_component(jb_var_t v, const char *name);
void jb_put_vec_mat_member(jb_var_t *v, const char *name, jb_var_t val);

/* --- JMN (jasboot-jmn-core, paridad con VM) --- */
jb_var_t jb_jmn_key_as_text(jb_var_t key);
double jb_jmn_as_f64(jb_var_t v, double def);
double jb_jmn_clamp01(double x);
void jb_jmn_set_resultado(jb_var_t v);

jb_var_t jb_crear_memoria(jb_var_t path);
jb_var_t jb_abrir_memoria(jb_var_t path);
jb_var_t jb_recordar(jb_var_t key, jb_var_t val);
jb_var_t jb_buscar(jb_var_t key);
jb_var_t jb_asociar(jb_var_t origen, jb_var_t destino, jb_var_t fuerza);
jb_var_t jb_reforzar(jb_var_t origen, jb_var_t destino, jb_var_t delta);
jb_var_t jb_penalizar(jb_var_t origen, jb_var_t destino, jb_var_t delta);
jb_var_t jb_buscar_asociados(jb_var_t origen, jb_var_t min_peso);
jb_var_t jb_buscar_asociados_lista(jb_var_t origen, jb_var_t k, jb_var_t tipo_rel_opt);
jb_var_t jb_buscar_asociados_rango(jb_var_t lista_conceptos, jb_var_t min_p, jb_var_t max_p);
jb_var_t jb_propagar_activacion(jb_var_t origen, jb_var_t decaimiento);
jb_var_t jb_resolver_conflictos(jb_var_t origen);
jb_var_t jb_buscar_peso(jb_var_t concepto);
jb_var_t jb_define_concepto(jb_var_t concepto, jb_var_t descripcion);
void jb_consolidar_memoria(void);
/** consolidar_memoria / consolidar / dormir en contexto de expresión AOT. */
jb_var_t jb_consolidar_memoria_expr(void);
void jb_cerrar_memoria(void);
jb_var_t jb_aprender_concepto(jb_var_t concepto, jb_var_t peso);

jb_var_t jb_mem_obtener_fuerza(jb_var_t origen, jb_var_t destino);
jb_var_t jb_reforzar_concepto(jb_var_t concepto, jb_var_t magnitud);
jb_var_t jb_penalizar_concepto(jb_var_t concepto, jb_var_t magnitud);
jb_var_t jb_olvidar_debiles(jb_var_t umbral);

jb_var_t jb_asociar_secuencia_solo(jb_var_t lista);
jb_var_t jb_asociar_secuencia(jb_var_t a, jb_var_t b);
jb_var_t jb_obtener_secuencia(jb_var_t contexto);
jb_var_t jb_pensar_siguiente(jb_var_t paso, jb_var_t contexto_opt);
jb_var_t jb_pensar_anterior(jb_var_t paso, jb_var_t contexto_opt);
jb_var_t jb_corregir_secuencia(jb_var_t anterior, jb_var_t incorrecto, jb_var_t correcto);

jb_var_t jb_recordar_stub(jb_var_t key, jb_var_t val);
jb_var_t jb_buscar_stub(jb_var_t key);
jb_var_t jb_crear_memoria_stub(jb_var_t path);

jb_var_t jb_nativo_mlp_entrenar(jb_var_t pesos_capas, jb_var_t sesgos_capas, jb_var_t X, jb_var_t lista_y,
                                jb_var_t capas_ocultas, jb_var_t learning_rate, jb_var_t epochs);

/* --- Cognición / introspección (AOT, modelo en memoria; no es VM binaria) --- */
jb_var_t jb_pensar(jb_var_t semilla);
jb_var_t jb_pensar_respuesta(jb_var_t entrada, jb_var_t creatividad, jb_var_t umbral);
jb_var_t jb_procesar_texto(jb_var_t texto);
jb_var_t jb_obtener_todos_conceptos(void);
jb_var_t jb_obtener_relacionados(jb_var_t concepto);
jb_var_t jb_obtener_nombre_concepto(jb_var_t idv);
void jb_imprimir_id(jb_var_t idv);
jb_var_t jb_propiedad_concepto(jb_var_t concepto, jb_var_t prop);
jb_var_t jb_asociar_relacion(jb_var_t a, jb_var_t b, jb_var_t fuerza);
jb_var_t jb_asociar_relacion_4(jb_var_t a, jb_var_t b, jb_var_t tipo_rel, jb_var_t peso);
jb_var_t jb_asociar_similitud(jb_var_t a, jb_var_t b, jb_var_t fuerza);
jb_var_t jb_asociar_diferencia(jb_var_t a, jb_var_t b, jb_var_t fuerza);
jb_var_t jb_comparar_patrones(jb_var_t a, jb_var_t b);
jb_var_t jb_buscar_en_memoria(jb_var_t termino);
jb_var_t jb_buscar_en_memoria_lista(jb_var_t termino, jb_var_t maxn);
jb_var_t jb_buscar_en_memoria_cs(jb_var_t termino, jb_var_t case_sens);
jb_var_t jb_buscar_en_memoria_detallada(jb_var_t termino, jb_var_t maxn, jb_var_t case_sens);
jb_var_t jb_decae_conexiones(jb_var_t factor);
jb_var_t jb_decaer_conexiones(jb_var_t factor);
jb_var_t jb_ventana_percepcion(jb_var_t tam);
jb_var_t jb_flujo_temporal(jb_var_t tam);
void jb_percepcion_limpiar(void);
jb_var_t jb_percepcion_tamano(void);
jb_var_t jb_percepcion_anterior(jb_var_t idx);
jb_var_t jb_percepcion_recientes(void);
jb_var_t jb_percepcion(jb_var_t idv);
jb_var_t jb_ventana_rastro_activacion(jb_var_t tam);
jb_var_t jb_rastro_activacion_ventana(jb_var_t tam);
void jb_rastro_activacion_limpiar(void);
jb_var_t jb_rastro_activacion_tamano(void);
jb_var_t jb_rastro_activacion_obtener(jb_var_t idx);
jb_var_t jb_rastro_activacion_peso(jb_var_t idx);
jb_var_t jb_rastro_activacion_lista(void);
jb_var_t jb_rastro_activacion_recientes(void);
jb_var_t jb_elegir_por_peso(jb_var_t lista, jb_var_t ctx);
jb_var_t jb_elegir_por_peso_segun(jb_var_t lista, jb_var_t ctx);
jb_var_t jb_elegir_por_peso_id(jb_var_t lista, jb_var_t ctx);
jb_var_t jb_elegir_por_peso_semilla(jb_var_t lista, jb_var_t ctx, jb_var_t seed);
jb_var_t jb_elegir_por_peso_seed(jb_var_t lista, jb_var_t ctx, jb_var_t seed);

#endif
