#ifndef N_GRAFO_COMPRESION_H
#define N_GRAFO_COMPRESION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NGF_COMPRESSION_BLOCK_SIZE 65536
#define NGF_COMPRESSION_NONE 0
#define NGF_COMPRESSION_ZSTD 1

/* Comprime bloque. Requiere NGF_USE_ZSTD en compilación. */
size_t n_grafo_comprimir(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_len, int metodo);

/* Descomprime. src_len = tamaño comprimido, orig_len = tamaño original. */
size_t n_grafo_descomprimir(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_len, int metodo);

#ifdef __cplusplus
}
#endif

#endif
