/**
 * Replicacion y consistencia eventual (Fase D)
 */
#include "n_grafo_replica.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

struct NGrafoReplica {
    NGrafo* primario;
    char* ruta_primaria;
    char* rutas_replica[NGF_REPLICA_MAX];
    size_t n_replicas;
};

static int copy_file(const char* src, const char* dst) {
    FILE* fin = fopen(src, "rb");
    if (!fin) return -1;
    FILE* fout = fopen(dst, "wb");
    if (!fout) { fclose(fin); return -1; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (fwrite(buf, 1, n, fout) != n) {
            fclose(fin);
            fclose(fout);
            return -1;
        }
    }
    fclose(fin);
    fclose(fout);
    return 0;
}

NGrafoReplica* n_grafo_replica_crear(const char* ruta_primaria, const char** rutas_replica, size_t n_replicas) {
    if (!ruta_primaria || n_replicas > NGF_REPLICA_MAX) return NULL;
    NGrafoReplica* gr = (NGrafoReplica*)calloc(1, sizeof(NGrafoReplica));
    if (!gr) return NULL;
    gr->ruta_primaria = strdup(ruta_primaria);
    gr->primario = n_abrir_grafo(ruta_primaria);
    if (!gr->primario) {
        free(gr->ruta_primaria);
        free(gr);
        return NULL;
    }
    gr->n_replicas = n_replicas;
    for (size_t i = 0; i < n_replicas && rutas_replica && rutas_replica[i]; i++) {
        gr->rutas_replica[i] = strdup(rutas_replica[i]);
    }
    return gr;
}

void n_grafo_replica_cerrar(NGrafoReplica* gr) {
    if (!gr) return;
    n_grafo_replica_sincronizar(gr);
    n_cerrar_grafo(gr->primario);
    for (size_t i = 0; i < gr->n_replicas; i++) free(gr->rutas_replica[i]);
    free(gr->ruta_primaria);
    free(gr);
}

int n_grafo_replica_sincronizar(NGrafoReplica* gr) {
    if (!gr || !gr->primario) return -1;
    n_cerrar_grafo(gr->primario);
    gr->primario = NULL;
    for (size_t i = 0; i < gr->n_replicas && gr->rutas_replica[i]; i++) {
        copy_file(gr->ruta_primaria, gr->rutas_replica[i]);
    }
    gr->primario = n_abrir_grafo(gr->ruta_primaria);
    return gr->primario ? 0 : -1;
}

NGrafo* n_grafo_replica_primario(NGrafoReplica* gr) {
    return gr ? gr->primario : NULL;
}
