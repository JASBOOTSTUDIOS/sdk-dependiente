/**
 * n_grafo - Grafo de triples optimizado (S, P, O)
 * Formato .ngf - No modifica funcionalidades existentes (recordar, buscar, etc.)
 * Fase 1: Modelo de datos e infraestructura base
 */

#ifndef N_GRAFO_H
#define N_GRAFO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Handle opaco del grafo */
typedef struct NGrafo NGrafo;

/* Magic y versión del formato .ngf */
#define NGF_MAGIC_0 0x4E
#define NGF_MAGIC_1 0x47
#define NGF_MAGIC_2 0x46
#define NGF_MAGIC_3 0x31
#define NGF_VERSION_1 1
#define NGF_HEADER_SIZE 28
#define NGF_TRIPLE_SIZE 13
#define NGF_FLAG_COMPRESSED 0x01  /* bit 0: triples comprimidos (LZ4/Zstd) - futuro */

/* IDs reservados: 0 = inválido/no existe */
#define NGF_ID_NULO 0

/**
 * n_abrir_grafo - Abre o crea archivo de grafo.
 * No interfiere con crear_memoria.
 * @param ruta: ruta al archivo .ngf
 * @return handle del grafo, o NULL si error
 */
NGrafo* n_abrir_grafo(const char* ruta);

/**
 * n_cerrar_grafo - Cierra grafo y persiste cambios.
 * No interfiere con cerrar_memoria.
 * @param g: handle del grafo (puede ser NULL)
 */
void n_cerrar_grafo(NGrafo* g);

/**
 * n_grafo_valido - Comprueba si el handle es válido.
 */
int n_grafo_valido(const NGrafo* g);

/**
 * n_obtener_id - Resuelve texto → ID. Si no existe, lo crea.
 * @param g: handle del grafo
 * @param texto: texto a buscar (se normaliza internamente)
 * @return ID del concepto, o NGF_ID_NULO si error
 */
uint32_t n_obtener_id(NGrafo* g, const char* texto);

/**
 * n_obtener_texto - Resuelve ID → texto.
 * @param g: handle del grafo
 * @param id: ID del concepto
 * @param buf: buffer de salida
 * @param buf_size: tamaño del buffer
 * @return 1 si encontrado, 0 si no existe (buf[0]='\0')
 */
int n_obtener_texto(const NGrafo* g, uint32_t id, char* buf, size_t buf_size);

/**
 * n_existe_concepto - Comprueba si el concepto existe. No crea.
 * @param g: handle del grafo
 * @param texto: texto a buscar
 * @return 1 si existe, 0 si no
 */
int n_existe_concepto(const NGrafo* g, const char* texto);

/**
 * n_recordar - Inserta triple (S, P, O). Texto se resuelve a ID; si no existe, se crea.
 * @return 1 si ok, 0 si error
 */
int n_recordar(NGrafo* g, const char* sujeto, const char* predicado, const char* objeto);

/**
 * n_recordar_peso - Inserta triple con peso (0-255).
 */
int n_recordar_peso(NGrafo* g, const char* sujeto, const char* predicado, const char* objeto, uint8_t peso);

/**
 * n_buscar_objeto - Busca triples (S, P, ?). Devuelve el primer objeto encontrado.
 * @return ID del objeto, o NGF_ID_NULO si no existe
 */
uint32_t n_buscar_objeto(const NGrafo* g, const char* sujeto, const char* predicado);

/**
 * n_buscar_objetos - Busca triples (S, P, ?). Llena ids[] con los objetos.
 * @param ids: array de salida (puede ser NULL si max_count=0)
 * @param max_count: tamaño máximo del array
 * @return número de objetos encontrados
 */
size_t n_buscar_objetos(const NGrafo* g, const char* sujeto, const char* predicado, uint32_t* ids, size_t max_count);

/**
 * n_buscar_sujeto - Busca triples (?, P, O). Devuelve el primer sujeto encontrado.
 */
uint32_t n_buscar_sujeto(const NGrafo* g, const char* predicado, const char* objeto);

/**
 * n_buscar_sujetos - Busca triples (?, P, O). Llena ids[] con los sujetos.
 */
size_t n_buscar_sujetos(const NGrafo* g, const char* predicado, const char* objeto, uint32_t* ids, size_t max_count);

/**
 * n_buscar_donde_aparece - Consulta inversa: ¿dónde aparece este concepto como objeto?
 * Llena sujetos[] y predicados[] con los (S,P) que referencian al objeto.
 * @param sujetos: array de salida (o NULL)
 * @param predicados: array de salida (o NULL)
 * @param max_count: tamaño máximo
 * @return número de (S,P) encontrados
 */
size_t n_buscar_donde_aparece(const NGrafo* g, const char* objeto, uint32_t* sujetos, uint32_t* predicados, size_t max_count);

/**
 * n_buscar_predicados - Lista todos los predicados de un sujeto.
 * @param preds: array de salida (IDs de predicados)
 * @param max_count: tamaño máximo
 * @return número de predicados
 */
size_t n_buscar_predicados(const NGrafo* g, const char* sujeto, uint32_t* preds, size_t max_count);

/**
 * n_olvidar_triple - Elimina un triple específico.
 * @return 1 si se eliminó, 0 si no existía
 */
int n_olvidar_triple(NGrafo* g, const char* sujeto, const char* predicado, const char* objeto);

/* --- Fase 5: API de alto nivel (comodidades) --- */

/**
 * n_recordar_triple_texto - Variante que acepta tres textos. Traduce a IDs y llama a n_recordar.
 */
int n_recordar_triple_texto(NGrafo* g, const char* s_texto, const char* p_texto, const char* o_texto);

/**
 * n_buscar_objeto_texto - Busca (S,P,?) y devuelve el primer objeto como texto.
 * @param buf: buffer de salida
 * @param buf_size: tamaño del buffer
 * @return 1 si encontrado, 0 si no (buf[0]='\0')
 */
int n_buscar_objeto_texto(const NGrafo* g, const char* sujeto, const char* predicado, char* buf, size_t buf_size);

/**
 * n_lista_triples - Devuelve todos los (P, O) para un sujeto.
 * @param predicados: array de salida (IDs)
 * @param objetos: array de salida (IDs)
 * @param max_count: tamaño máximo
 * @return número de (P,O) encontrados
 */
size_t n_lista_triples(const NGrafo* g, const char* sujeto, uint32_t* predicados, uint32_t* objetos, size_t max_count);

/**
 * n_para_cada_concepto - Itera sobre vocabulario (id, texto). Para índices invertidos.
 * @param cb: callback(id, texto, user). Retornar !=0 para parar.
 */
int n_para_cada_concepto(const NGrafo* g, int (*cb)(uint32_t id, const char* texto, void* user), void* user);

/**
 * n_tamano_grafo - Devuelve número de triples.
 * @param conceptos_out: si no NULL, se escribe el número de conceptos (vocabulario)
 * @return número de triples
 */
size_t n_tamano_grafo(const NGrafo* g, size_t* conceptos_out);

/* --- Fase 7: Caché y optimización --- */

/**
 * n_configurar_cache_lru - Tamaño del caché LRU para vocabulario (conceptos más usados).
 * @param tamano: número de entradas (0 = desactivar). Por defecto 0.
 */
void n_configurar_cache_lru(NGrafo* g, size_t tamano);

/**
 * n_configurar_bloom - Bits del Bloom filter para n_existe_concepto (0 = desactivar).
 * @param num_bits: aprox. 8-10 bits por concepto para ~1% falsos positivos.
 */
void n_configurar_bloom(NGrafo* g, size_t num_bits);

/* --- Fase 9: Herencia y razonamiento --- */

/**
 * n_heredar - Búsqueda transitiva vía n_es_un.
 * Si (S, P, O) existe, devuelve O.
 * Si (S, n_es_un, T) y (T, P, O), devuelve O.
 * Recorre la cadena n_es_un transitivamente.
 * @return ID del objeto, o NGF_ID_NULO si no existe
 */
uint32_t n_heredar(const NGrafo* g, const char* sujeto, const char* predicado);

/**
 * n_heredar_texto - Igual que n_heredar pero devuelve el objeto como texto.
 * @return 1 si encontrado, 0 si no (buf[0]='\0')
 */
int n_heredar_texto(const NGrafo* g, const char* sujeto, const char* predicado, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* N_GRAFO_H */
