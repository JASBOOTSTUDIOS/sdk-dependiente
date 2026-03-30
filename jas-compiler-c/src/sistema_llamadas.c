#include "sistema_llamadas.h"
#include <string.h>

/* ~40+ llamadas de sistema - paradigma 100% español */
const char *const SISTEMA_LLAMADAS[] = {
    "abrir_archivo", "cerrar_archivo", "escribir_archivo", "leer_linea_archivo",
    "fin_archivo", "existe_archivo", "obtener_todos_conceptos", "obtener_relacionados",
    "lista_tamano", "lista_obtener", "obtener_timestamp", "aprender_peso", "procesar_texto",
    "str_minusculas", "str_copiar", "copiar_texto", "lista_agregar", "crear_lista", "obtener_nombre_concepto",
    "listar_archivos", "finalizar", "olvidar", "obtener_campo", "bit_shl", "bit_shr",
    "sistema_ejecutar", "mapa_crear", "mapa_poner", "mapa_obtener",
    "str_a_entero", "str_a_flotante", "convertir_entero", "convertir_flotante", "fs_abrir", "fs_cerrar", "fs_escribir", "fs_leer_linea",
    "fs_leer_byte", "fs_escribir_byte", "fs_leer_texto", "sys_argc", "sys_argv", "str_extraer_caracter", "str_desde_numero", "decimal", "codigo_caracter", "caracter_a_texto",
    "byte_a_caracter", "caracter_a_byte",
    "lista_crear", "mem_lista_crear", "mem_lista_agregar", "mem_lista_obtener", "mem_lista_tamano",
    "lista_limpiar", "mem_lista_limpiar",
    "lista_liberar", "mem_lista_liberar",
    "lista_mapear", "mem_lista_mapear", "lista_filtrar", "mem_lista_filtrar",
    "mem_crear", "mem_cerrar", "mem_asociar", "tiene_asociacion", "imprimir_flotante",
    "comparar_gt_flt", "mem_poner_u32_ind", "mem_obtener_u32_ind", "mem_aprender_peso_reg",
    "pensar", "buscar", "imprimir_sin_salto", "pensar_respuesta", "es_variable_sistema",
    "reforzar", "penalizar", "mem_obtener_fuerza",
    "asociar_pesos_conceptos", "asociar_secuencia", "registrar_patron", "pensar_siguiente",
    "pensar_anterior", "corregir_secuencia", "asociar_relacion", "comparar_patrones",
    "asociar_similitud", "asociar_diferencia", "buscar_asociados", "asociados_de",
    "buscar_asociados_lista", "asociados_lista_de", "decae_conexiones", "decaer_conexiones",
    "consolidar_memoria", "dormir", "consolidar", "olvidar_debiles",
    "ventana_percepcion", "flujo_temporal", "percepcion_limpiar", "percepcion_tamano",
    "percepcion_anterior", "percepcion_recientes", "percepcion",
    "ventana_rastro_activacion", "rastro_activacion_ventana", "rastro_activacion_limpiar",
    "rastro_activacion_tamano", "rastro_activacion_obtener", "rastro_activacion_peso",
    "rastro_activacion_lista", "rastro_activacion_recientes",
    "propagar_activacion", "propagar_activacion_de",
    "elegir_por_peso", "elegir_por_peso_segun", "elegir_por_peso_id", "elegir_por_peso_semilla", "elegir_por_peso_seed",
    "resolver_conflictos", "resolver_conflictos_de",
    "segmentar_palabras", "palabras_de", "dividir_texto", "minusculas", "extraer_subtexto",
    "imprimir_id", "longitud_texto", "propiedad_concepto", "reservar", "liberar", "ir_escribir",
    "concatenar", "longitud", "dividir", "buscar_en_texto", "contiene_texto", "termina_con",
    "leer_entrada", "percibir_teclado", "ingreso_inmediato", "entrada_flotante", "limpiar_consola", "ahora", "obtener_ahora", "diferencia_en_segundos",
    "n_abrir_grafo", "n_cerrar_grafo", "n_grafo_valido",
    "n_obtener_id", "n_obtener_texto", "n_existe_concepto",
    "n_recordar", "n_recordar_peso", "n_recordar_triple_texto",
    "n_buscar_objeto", "n_buscar_objetos", "n_buscar_objeto_texto",
    "n_buscar_sujeto", "n_buscar_sujetos", "n_buscar_predicados",
    "n_buscar_donde_aparece", "n_lista_triples", "n_tamano_grafo",
    "n_olvidar_triple", "n_configurar_cache_lru", "n_configurar_bloom",
    "n_heredar", "n_heredar_texto",
    "vec2_sumar", "vec2_restar", "vec3_sumar", "vec3_restar", "vec4_sumar", "vec4_restar",
    "vec2_longitud", "vec3_longitud", "vec4_longitud",
    "vec2_normalizar", "vec3_normalizar", "vec4_normalizar",
    "vec2_dot", "vec3_dot", "vec4_dot",
    "vec3_cross",
    "sin", "cos", "tan", "atan2", "arcotangente2",
    "exp", "log", "log10",
    "mat4_mul_vec4", "mat4_mul",
    "mat4_identidad", "mat4_transpuesta", "mat4_inversa",
    "mat3_mul_vec3", "mat3_mul",
    "ffi_cargar", "ffi_simbolo", "ffi_llamar"
};
const size_t SISTEMA_LLAMADAS_COUNT = sizeof(SISTEMA_LLAMADAS) / sizeof(SISTEMA_LLAMADAS[0]);

int is_sistema_llamada(const char *name, size_t len) {
    for (size_t i = 0; i < SISTEMA_LLAMADAS_COUNT; i++) {
        size_t k = strlen(SISTEMA_LLAMADAS[i]);
        if (k == len && memcmp(SISTEMA_LLAMADAS[i], name, len) == 0)
            return 1;
    }
    if (len >= 2 && name[0] == '_' && name[1] == '_')
        return 1;
    return 0;
}
