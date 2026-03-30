#ifndef N_GRAFO_PARTICIONADO_H
#define N_GRAFO_PARTICIONADO_H

#include "n_grafo.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_DOMINIO_GEO "geo"
#define NGF_DOMINIO_BIO "bio"
#define NGF_DOMINIO_ONT "ont"
#define NGF_DOMINIO_GEN "gen"
#define NGF_MAX_PARTICIONES 32

typedef struct NGrafoParticionado NGrafoParticionado;

NGrafoParticionado* n_abrir_grafo_particionado(const char** rutas, const char** dominios, size_t n);
void n_cerrar_grafo_particionado(NGrafoParticionado* gp);
int n_grafo_particionado_valido(const NGrafoParticionado* gp);
int n_recordar_en(NGrafoParticionado* gp, const char* dominio, const char* s, const char* p, const char* o);
uint32_t n_buscar_objeto_en(NGrafoParticionado* gp, const char* dominio, const char* sujeto, const char* predicado);
size_t n_buscar_objetos_en(NGrafoParticionado* gp, const char* dominio, const char* s, const char* p, uint32_t* ids, size_t max_count);
size_t n_tamano_grafo_particionado(const NGrafoParticionado* gp, size_t* conceptos_out);

/**
 * n_heredar_particionado - n_heredar sobre múltiples particiones; merge de resultados.
 * Si dominio != NULL, solo consulta esa partición; si NULL, consulta todas.
 * @return 1 si encontrado (buf con objeto), 0 si no
 */
int n_heredar_particionado(const NGrafoParticionado* gp, const char* dominio,
    const char* sujeto, const char* predicado, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
