#ifndef N_GRAFO_INDICE_TEXTO_H
#define N_GRAFO_INDICE_TEXTO_H

#include "n_grafo.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NGrafoIndiceTexto NGrafoIndiceTexto;

/* Crea índice invertido sobre vocabulario del grafo. Términos -> concept_ids */
NGrafoIndiceTexto* n_indice_texto_crear(const NGrafo* g);
void n_indice_texto_cerrar(NGrafoIndiceTexto* it);

/* Busca conceptos cuyo texto contiene el término (subcadena simple) */
size_t n_indice_texto_buscar(const NGrafoIndiceTexto* it, const char* termino, uint32_t* ids_out, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif
