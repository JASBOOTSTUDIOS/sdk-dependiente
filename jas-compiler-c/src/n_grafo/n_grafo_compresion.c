/**
 * Compresión LZ4/Zstd para bloques de triples.
 * Sin dependencias: compilación normal sin compresión (passthrough).
 * Con -DNGF_USE_ZSTD y -lzstd: compresión real.
 */
#include "n_grafo_compresion.h"
#include <string.h>

#ifdef NGF_USE_ZSTD
#include <zstd.h>
#endif

size_t n_grafo_comprimir(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_len, int metodo) {
    (void)metodo;
#ifdef NGF_USE_ZSTD
    if (metodo == NGF_COMPRESSION_ZSTD && dst_cap >= ZSTD_compressBound(src_len)) {
        size_t n = ZSTD_compress(dst, dst_cap, src, src_len, 3);
        return ZSTD_isError(n) ? 0 : n;
    }
#endif
    if (dst_cap >= src_len) {
        memcpy(dst, src, src_len);
        return src_len;
    }
    return 0;
}

size_t n_grafo_descomprimir(uint8_t* dst, size_t dst_cap, const uint8_t* src, size_t src_len, int metodo) {
    (void)metodo;
#ifdef NGF_USE_ZSTD
    if (metodo == NGF_COMPRESSION_ZSTD) {
        unsigned long long orig = ZSTD_getFrameContentSize(src, src_len);
        if (orig != ZSTD_CONTENTSIZE_ERROR && orig != ZSTD_CONTENTSIZE_UNKNOWN && orig <= dst_cap) {
            size_t n = ZSTD_decompress(dst, dst_cap, src, src_len);
            return ZSTD_isError(n) ? 0 : n;
        }
    }
#endif
    if (src_len <= dst_cap) {
        memcpy(dst, src, src_len);
        return src_len;
    }
    return 0;
}
