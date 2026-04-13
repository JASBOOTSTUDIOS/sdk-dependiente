/* Keywords y operadores - definiciones */

#include "keywords.h"
#include <string.h>

/* 1.9 FORBIDDEN_ENGLISH */
const char *const FORBIDDEN_ENGLISH[] = {
    "if", "else", "for", "while", "function", "return", "endif", "endwhile", "endfor",
    "true", "false", "null", "var", "let", "const", "class", "import", "export"
};
const size_t FORBIDDEN_ENGLISH_COUNT = sizeof(FORBIDDEN_ENGLISH) / sizeof(FORBIDDEN_ENGLISH[0]);

/* 1.10 KEYWORDS - todas las de lexer.py */
const char *const KEYWORDS[] = {
    "principal", "fin_principal", "funcion", "fin_funcion",
    "asincrono", "esperar", "tarea",
    "mientras", "fin_mientras", "cuando", "fin_cuando",
    "si", "sino", "fin_si",
    "imprimir", "imprimir_sin_salto", "imprimir_texto", "ingresar_texto", "ingreso_inmediato", "limpiar_consola", "pausa", "retornar", "romper", "continuar",
    "entero", "texto", "flotante", "caracter", "constante", "u32", "u64", "u8", "byte",
    "bytes", "socket", "tls", "http_solicitud", "http_respuesta", "http_servidor",
    "vec2", "vec3", "vec4", "mat4", "mat3",
    "activar_modulo", "biblioteca", "recordar", "responder", "aprender", "reforzar", "penalizar", "buscar", "buscar_peso", "asociar",
    "con", "valor", "peso", "igual", "es", "entrada", "entonces", "retorna",
    "extraer_antes_de", "extraer_despues_de", "contiene_texto", "termina_con", "ultima_palabra",
    "copiar_texto", "asociar_pesos_conceptos", "imprimir_auditoria", "como", "o", "y", "no",
    "define_concepto", "crear_memoria", "abrir_memoria", "cerrar_memoria", "mayor", "menor", "distinto", "que", "de", "a",
    "concepto", "fin_concepto", "crear_concepto", "propiedad_concepto",
    "abrir_archivo", "cerrar_archivo", "escribir_archivo", "leer_linea_archivo",
    "fin_archivo", "existe_archivo", "obtener_todos_conceptos", "obtener_relacionados",
    "lista_tamano", "lista_obtener", "obtener_timestamp",
    "aprender_peso", "procesar_texto", "str_minusculas", "str_copiar",
    "lista_agregar", "crear_lista", "lista_crear", "lista_limpiar", "obtener_nombre_concepto", "listar_archivos", "fs_listar", "finalizar",
    "olvidar", "pensar", "pensar_respuesta", "obtener_campo", "es_variable_sistema",
    "bit_shl", "bit_shr", "sistema_ejecutar",
    "fs_escribir_byte", "mapa_crear", "mapa_poner", "mapa_obtener",
    "str_a_entero", "str_a_flotante", "entrada_flotante",
    "registro", "fin_registro", "clase", "fin_clase", "extiende", "lista", "mapa", "bool", "hacer", "fin_hacer", "usar", "enviar", "todo", "todas", "privado",
    "seleccionar", "caso", "defecto", "fin_seleccionar",
    "intentar", "atrapar", "final", "fin_intentar", "lanzar", "macro", "llamar",
    "fs_abrir", "fs_cerrar", "fs_escribir", "fs_leer_linea",
    "fs_leer_byte", "fs_escribir_byte", "sys_argc", "sys_argv", "str_extraer_caracter", "str_desde_numero", "decimal", "codigo_caracter", "caracter_a_texto",
    "byte_a_caracter", "caracter_a_byte",
    "mem_lista_crear", "mem_lista_agregar", "mem_lista_obtener", "mem_lista_tamano", "mem_lista_limpiar",
    "mem_lista_liberar", "lista_liberar",
    "mem_lista_mapear", "mem_lista_filtrar", "lista_mapear", "lista_filtrar",
    "json", "objeto", "json_parse", "json_stringify", "json_objeto_obtener", "json_lista_obtener", "json_lista_tamano",
    "json_a_texto", "json_a_entero", "json_a_flotante", "json_a_bool", "json_tipo",
    "bytes_crear", "bytes_tamano", "bytes_obtener", "bytes_poner", "bytes_anexar", "bytes_subbytes",
    "bytes_desde_texto", "bytes_a_texto", "dns_resolver",
    "tcp_conectar", "tcp_escuchar", "tcp_aceptar", "tcp_enviar", "tcp_recibir", "tcp_cerrar",
    "tls_cliente", "tls_servidor", "tls_enviar", "tls_recibir", "tls_cerrar",
    "pausa_milisegundos", "esperar_milisegundos",
    "para_cada", "fin_para_cada", "sobre",
    "mem_crear", "mem_cerrar", "mem_asociar", "tiene_asociacion", "imprimir_flotante",
    "comparar_gt_flt", "mem_poner_u32_ind", "mem_obtener_u32_ind", "mem_aprender_peso_reg",
    "registrar_patron", "asociar_secuencia", "pensar_siguiente", "pensar_anterior", "corregir_secuencia", "asociar_relacion", "comparar_patrones",
    "asociar_similitud", "asociar_diferencia", "buscar_asociados", "asociados_de", "buscar_asociados_lista", "asociados_lista_de", "decae_conexiones", "decaer_conexiones", "consolidar_memoria", "dormir", "consolidar", "olvidar_debiles", "ventana_percepcion", "flujo_temporal", "percepcion_limpiar", "percepcion_tamano", "percepcion_anterior", "percepcion_recientes", "percepcion", "ventana_rastro_activacion", "rastro_activacion_ventana", "rastro_activacion_limpiar", "rastro_activacion_tamano", "rastro_activacion_obtener", "rastro_activacion_peso", "rastro_activacion_lista", "rastro_activacion_recientes", "propagar_activacion", "propagar_activacion_de", "elegir_por_peso", "elegir_por_peso_segun", "elegir_por_peso_id", "elegir_por_peso_semilla", "elegir_por_peso_seed", "resolver_conflictos", "resolver_conflictos_de",
    "segmentar_palabras", "palabras_de", "dividir_texto", "minusculas", "extraer_subtexto", "imprimir_id", "longitud_texto",
    "reservar", "liberar", "ir_escribir"
};
const size_t KEYWORDS_COUNT = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

/* 1.6 Operadores de un carácter (incluye < > para comparaciones) */
static const char ops_single[] = "=+-*/%(),.[]{}:?!<>";
const char *const OPERATORS_SINGLE = ops_single;

/* Operadores de dos caracteres (ordenados para matching: más largos primero) */
static const struct { const char s[3]; } ops_double[] = {
    {"=="}, {"!="}, {"<="}, {">="}, {"=>"}, {"++"}, {"--"}, {"+="}, {"-="}, {"*="}, {"/="}, {"<<"}, {">>"}
};

int is_keyword(const char *str, size_t len) {
    for (size_t i = 0; i < KEYWORDS_COUNT; i++) {
        size_t klen = strlen(KEYWORDS[i]);
        if (klen == len && memcmp(KEYWORDS[i], str, len) == 0)
            return 1;
    }
    return 0;
}

int is_forbidden(const char *str, size_t len) {
    for (size_t i = 0; i < FORBIDDEN_ENGLISH_COUNT; i++) {
        size_t klen = strlen(FORBIDDEN_ENGLISH[i]);
        if (klen == len && memcmp(FORBIDDEN_ENGLISH[i], str, len) == 0)
            return 1;
    }
    return 0;
}

int is_operator_single(char c) {
    for (size_t i = 0; OPERATORS_SINGLE[i]; i++)
        if (OPERATORS_SINGLE[i] == c) return 1;
    return 0;
}

/* Retorna 1 si hay operador de 2 chars, y escribe en out_two (debe tener 3 bytes) */
int is_operator_double(const char *s, size_t len, char *out_two) {
    if (len < 2) return 0;
    for (size_t i = 0; i < sizeof(ops_double)/sizeof(ops_double[0]); i++) {
        if (s[0] == ops_double[i].s[0] && s[1] == ops_double[i].s[1]) {
            out_two[0] = ops_double[i].s[0];
            out_two[1] = ops_double[i].s[1];
            out_two[2] = '\0';
            return 1;
        }
    }
    return 0;
}
