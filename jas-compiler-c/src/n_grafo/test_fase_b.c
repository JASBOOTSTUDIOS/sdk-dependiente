/**
 * Test Fase B: particionado, índice texto, compresión, caché
 */
#include "n_grafo.h"
#include "n_grafo_particionado.h"
#include "n_grafo_indice_texto.h"
#include "n_grafo_compresion.h"
#include "n_grafo_cache.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int ok = 0, fail = 0;
#define T(c, m) do { if (c) { ok++; } else { fail++; printf("FAIL: %s\n", m); } } while(0)

int main(void) {
    const char* geo_ruta = "test_geo.ngf";
    const char* bio_ruta = "test_bio.ngf";
    remove(geo_ruta);
    remove(bio_ruta);

    /* Crear grafos base para particiones */
    NGrafo* g_geo = n_abrir_grafo(geo_ruta);
    NGrafo* g_bio = n_abrir_grafo(bio_ruta);
    T(g_geo && g_bio, "crear grafos base");
    n_recordar(g_geo, "francia", "capital", "paris");
    n_recordar(g_geo, "espana", "capital", "madrid");
    n_recordar(g_bio, "canis", "especie", "lupus");
    n_recordar(g_bio, "homo", "especie", "sapiens");
    n_cerrar_grafo(g_geo);
    n_cerrar_grafo(g_bio);

    /* Test particionado */
    const char* rutas[] = { geo_ruta, bio_ruta };
    const char* doms[] = { "geo", "bio" };
    NGrafoParticionado* gp = n_abrir_grafo_particionado(rutas, doms, 2);
    T(gp != NULL, "abrir particionado");
    T(n_grafo_particionado_valido(gp), "particionado valido");
    uint32_t o1 = n_buscar_objeto_en(gp, "geo", "francia", "capital");
    T(o1 != NGF_ID_NULO, "buscar en geo");
    uint32_t o2 = n_buscar_objeto_en(gp, "bio", "canis", "especie");
    T(o2 != NGF_ID_NULO, "buscar en bio");
    size_t sz = n_tamano_grafo_particionado(gp, NULL);
    T(sz >= 4, "tamano particionado");
    n_cerrar_grafo_particionado(gp);

    /* Test índice texto */
    NGrafo* g = n_abrir_grafo(geo_ruta);
    NGrafoIndiceTexto* it = n_indice_texto_crear(g);
    T(it != NULL, "crear indice texto");
    uint32_t ids[16];
    size_t n = n_indice_texto_buscar(it, "paris", ids, 16);
    T(n > 0, "buscar paris en indice");
    n_indice_texto_cerrar(it);
    n_cerrar_grafo(g);

    /* Test compresión (passthrough sin zstd) */
    uint8_t orig[] = "Hello world";
    uint8_t comp[128], decomp[128];
    size_t clen = n_grafo_comprimir(comp, sizeof(comp), orig, 11, NGF_COMPRESSION_NONE);
    T(clen == 11, "comprimir none");
    size_t dlen = n_grafo_descomprimir(decomp, sizeof(decomp), comp, clen, NGF_COMPRESSION_NONE);
    T(dlen == 11 && memcmp(orig, decomp, 11) == 0, "descomprimir");

    /* Test caché */
    NGrafoCache* cache = n_grafo_cache_crear(NGF_CACHE_LOCAL, NULL);
    T(cache != NULL, "crear cache");
    uint32_t v = 42;
    T(n_grafo_cache_put(cache, 123, &v, sizeof(v)) == 0, "cache put");
    uint32_t v2 = 0;
    size_t len = sizeof(v2);
    T(n_grafo_cache_get(cache, 123, &v2, &len) == 0 && v2 == 42, "cache get");
    n_grafo_cache_cerrar(cache);

    remove(geo_ruta);
    remove(bio_ruta);
    printf("Fase B: %d ok, %d fail\n", ok, fail);
    return fail > 0 ? 1 : 0;
}
