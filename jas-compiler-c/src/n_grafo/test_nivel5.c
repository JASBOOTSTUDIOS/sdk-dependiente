/**
 * Test Nivel 5: Escala y particionado
 * - Índice maestro (particiones.ngfi)
 * - n_heredar_particionado
 * - n_grafo_shard_heredar
 */
#include "n_grafo.h"
#include "n_grafo_particionado.h"
#include "n_grafo_indice.h"
#include "n_grafo_shard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while(0)
#define CLEANUP(f) do { if (f) remove(f); } while(0)

int main(void) {
    const char* geo_ngf = "test_n5_geo.ngf";
    const char* ont_ngf = "test_n5_ont.ngf";
    const char* indice_ngfi = "test_n5_particiones.ngfi";
    const char* base_shard = "test_n5_shard";
    int failed = 0;

    /* --- 5.3 Índice maestro --- */
    {
        NGrafo* g_geo = n_abrir_grafo(geo_ngf);
        NGrafo* g_ont = n_abrir_grafo(ont_ngf);
        ASSERT(g_geo && g_ont, "abrir grafos");
        n_recordar(g_geo, "Madrid", "capital", "España");
        n_recordar(g_geo, "España", "poblacion", "47000000");
        n_recordar(g_ont, "perro", "n_es_un", "animal");
        n_recordar(g_ont, "animal", "patas", "cuatro");
        n_cerrar_grafo(g_geo);
        n_cerrar_grafo(g_ont);

        const char* doms[] = { NGF_DOMINIO_GEO, NGF_DOMINIO_ONT };
        const char* ruts[] = { geo_ngf, ont_ngf };
        ASSERT(n_indice_escribir(indice_ngfi, doms, ruts, 2), "escribir indice");

        NGrafoParticionado* gp = n_abrir_grafo_particionado_desde_indice(indice_ngfi);
        ASSERT(gp && n_grafo_particionado_valido(gp), "abrir desde indice");

        char buf[128];
        buf[0] = '\0';
        uint32_t o = n_buscar_objeto_en(gp, NGF_DOMINIO_GEO, "Madrid", "capital");
        ASSERT(o != NGF_ID_NULO, "buscar Madrid capital en geo");
        uint32_t o2 = n_buscar_objeto_en(gp, NULL, "perro", "n_es_un");
        ASSERT(o2 != NGF_ID_NULO, "buscar perro n_es_un en todas");

        /* 5.5 n_heredar_particionado */
        ASSERT(n_heredar_particionado(gp, NGF_DOMINIO_ONT, "perro", "patas", buf, sizeof(buf)), "heredar perro patas");
        ASSERT(strcmp(buf, "cuatro") == 0, "heredar resultado cuatro");

        n_cerrar_grafo_particionado(gp);
    }

    /* --- 5.5 n_grafo_shard_heredar --- */
    {
        NGrafoShard* gs = n_grafo_shard_crear(base_shard, 4);
        ASSERT(gs, "crear shards");
        n_grafo_shard_recordar(gs, "perro", "n_es_un", "animal");
        n_grafo_shard_recordar(gs, "animal", "patas", "cuatro");
        n_grafo_shard_recordar(gs, "labrador", "n_es_un", "perro");
        n_grafo_shard_cerrar(gs);

        gs = n_grafo_shard_crear(base_shard, 4);
        char buf[128];
        ASSERT(n_grafo_shard_heredar(gs, "perro", "patas", buf, sizeof(buf)), "shard heredar perro");
        ASSERT(strcmp(buf, "cuatro") == 0, "shard heredar resultado");
        ASSERT(n_grafo_shard_heredar(gs, "labrador", "patas", buf, sizeof(buf)), "shard heredar labrador transitivo");
        ASSERT(strcmp(buf, "cuatro") == 0, "shard heredar labrador resultado");
        n_grafo_shard_cerrar(gs);
    }

    CLEANUP(geo_ngf);
    CLEANUP(ont_ngf);
    CLEANUP(indice_ngfi);
    for (int i = 0; i < 4; i++) {
        char fn[64];
        snprintf(fn, sizeof(fn), "%s_%d.ngf", base_shard, i);
        CLEANUP(fn);
    }

    printf("Nivel 5 tests OK\n");
    return 0;
}
