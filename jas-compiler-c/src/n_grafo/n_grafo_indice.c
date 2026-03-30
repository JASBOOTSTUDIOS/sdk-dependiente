/**
 * n_grafo_indice.c - Implementación del índice maestro dominio → ruta .ngf
 */
#include "n_grafo_indice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int n_indice_escribir(const char* ruta_indice, const char** dominios, const char** rutas, size_t n) {
    if (!ruta_indice || !dominios || !rutas || n == 0 || n > NGF_INDICE_MAX_PARTICIONES)
        return 0;

    FILE* f = fopen(ruta_indice, "w");
    if (!f) return 0;

    for (size_t i = 0; i < n; i++) {
        const char* d = dominios[i] ? dominios[i] : NGF_DOMINIO_GEN;
        const char* r = rutas[i];
        if (!r) r = "";
        fprintf(f, "%s\t%s\n", d, r);
    }
    fclose(f);
    return 1;
}

NGrafoParticionado* n_abrir_grafo_particionado_desde_indice(const char* ruta_indice) {
    if (!ruta_indice) return NULL;

    FILE* f = fopen(ruta_indice, "r");
    if (!f) return NULL;

    char* dominios[NGF_INDICE_MAX_PARTICIONES];
    char* rutas[NGF_INDICE_MAX_PARTICIONES];
    size_t n = 0;

    char line[NGF_INDICE_MAX_LINE];
    while (n < NGF_INDICE_MAX_PARTICIONES && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char* dom = line;
        char* ruta = tab + 1;
        while (*ruta == ' ') ruta++;
        if (!*ruta) continue;

        dominios[n] = strdup(dom);
        rutas[n] = strdup(ruta);
        if (!dominios[n] || !rutas[n]) {
            for (size_t j = 0; j < n; j++) {
                free(dominios[j]);
                free(rutas[j]);
            }
            fclose(f);
            return NULL;
        }
        n++;
    }
    fclose(f);

    if (n == 0) return NULL;

    NGrafoParticionado* gp = n_abrir_grafo_particionado((const char**)rutas, (const char**)dominios, n);

    for (size_t i = 0; i < n; i++) {
        free(dominios[i]);
        free(rutas[i]);
    }

    return gp;
}
