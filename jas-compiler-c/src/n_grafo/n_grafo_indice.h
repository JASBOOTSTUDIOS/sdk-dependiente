/**
 * n_grafo_indice.h - Índice maestro: dominio → ruta .ngf
 * Formato particiones.ngfi: una línea por partición, "dominio\truta"
 */
#ifndef N_GRAFO_INDICE_H
#define N_GRAFO_INDICE_H

#include "n_grafo_particionado.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_INDICE_MAX_LINE 512
#define NGF_INDICE_MAX_PARTICIONES 32

/**
 * n_indice_escribir - Crea archivo de índice desde arrays.
 * @param ruta_indice: ruta al archivo .ngfi (ej. particiones.ngfi)
 * @param dominios: array de nombres de dominio
 * @param rutas: array de rutas a archivos .ngf
 * @param n: número de particiones
 * @return 1 si ok, 0 si error
 */
int n_indice_escribir(const char* ruta_indice, const char** dominios, const char** rutas, size_t n);

/**
 * n_abrir_grafo_particionado_desde_indice - Abre particiones desde archivo índice.
 * @param ruta_indice: ruta al archivo .ngfi
 * @return handle NGrafoParticionado, o NULL si error
 */
NGrafoParticionado* n_abrir_grafo_particionado_desde_indice(const char* ruta_indice);

#ifdef __cplusplus
}
#endif

#endif /* N_GRAFO_INDICE_H */
