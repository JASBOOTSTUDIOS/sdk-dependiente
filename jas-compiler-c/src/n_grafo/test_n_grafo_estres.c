/**
 * Tests de rendimiento y estrés para n_grafo
 * Compilar: gcc -Wall -I. n_grafo_core.c test_n_grafo_estres.c -o test_n_grafo_estres
 */
#include "n_grafo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static int tests_run = 0;
static int tests_fail = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FALLO test %d: %s (en %s:%d)\n", tests_run, (msg), __FILE__, __LINE__); \
        tests_fail++; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)

/* Genera nombre único para concepto */
static void gen_concepto(char* buf, size_t size, const char* pref, size_t i) {
    snprintf(buf, size, "%s_%zu", pref, i);
}

int main(void) {
    const char* ruta = "test_n_grafo_estres.ngf";
    remove(ruta);
    char buf[64];
    clock_t t0, t1;

    printf("--- Test estrés: muchos conceptos y triples ---\n");
    NGrafo* g = n_abrir_grafo(ruta);
    ASSERT(g != NULL, "abrir grafo estrés");

#define N_CONCEPTOS 1000
#define N_TRIPLES  5000

    t0 = clock();
    for (size_t i = 0; i < N_CONCEPTOS; i++) {
        gen_concepto(buf, sizeof(buf), "c", i);
        uint32_t id = n_obtener_id(g, buf);
        ASSERT(id != NGF_ID_NULO, "crear concepto");
    }
    t1 = clock();
    printf("  %d conceptos en %.2f ms\n", N_CONCEPTOS, 1000.0 * (t1 - t0) / CLOCKS_PER_SEC);

    t0 = clock();
    for (size_t i = 0; i < N_TRIPLES; i++) {
        gen_concepto(buf, sizeof(buf), "s", i % N_CONCEPTOS);
        char p[64], o[64];
        gen_concepto(p, sizeof(p), "p", (i * 7) % N_CONCEPTOS);
        gen_concepto(o, sizeof(o), "o", (i * 11) % N_CONCEPTOS);
        ASSERT(n_recordar(g, buf, p, o), "recordar triple");
    }
    t1 = clock();
    printf("  %d triples en %.2f ms\n", N_TRIPLES, 1000.0 * (t1 - t0) / CLOCKS_PER_SEC);

    size_t nt = n_tamano_grafo(g, NULL);
    ASSERT(nt >= N_TRIPLES, "triples insertados");
    printf("  Grafo: %zu triples\n", nt);

    t0 = clock();
    for (size_t i = 0; i < 500; i++) {
        char s[64], p[64];
        gen_concepto(s, sizeof(s), "s", i % N_CONCEPTOS);
        gen_concepto(p, sizeof(p), "p", (i * 7) % N_CONCEPTOS);
        uint32_t o = n_buscar_objeto(g, s, p);
        (void)o;
    }
    t1 = clock();
    printf("  500 búsquedas en %.2f ms\n", 1000.0 * (t1 - t0) / CLOCKS_PER_SEC);

    /* Persistencia de grafo grande */
    n_cerrar_grafo(g);
    t0 = clock();
    g = n_abrir_grafo(ruta);
    t1 = clock();
    ASSERT(g != NULL, "reabrir grafo grande");
    printf("  Carga grafo grande: %.2f ms\n", 1000.0 * (t1 - t0) / CLOCKS_PER_SEC);

    ASSERT_EQ(n_tamano_grafo(g, NULL), nt, "persistencia triples");
    {
        char s[64], p[64];
        gen_concepto(s, sizeof(s), "s", 0);
        gen_concepto(p, sizeof(p), "p", 0);
        ASSERT(n_buscar_objeto(g, s, p) != NGF_ID_NULO || nt == 0, "búsqueda tras carga");
    }

    /* Cadena n_heredar larga (32 niveles) */
    for (size_t i = 0; i < 32; i++) {
        gen_concepto(buf, sizeof(buf), "h", i);
        char parent[64];
        gen_concepto(parent, sizeof(parent), "h", i + 1);
        n_recordar(g, buf, "n_es_un", parent);
    }
    n_recordar(g, "h_32", "hoja", "valor_final");
    ASSERT_EQ(n_heredar(g, "h_0", "hoja"), n_obtener_id(g, "valor_final"), "n_heredar 32 niveles");

    n_cerrar_grafo(g);
    remove(ruta);

    printf("--- Test estrés: %d tests, %d fallos ---\n", tests_run, tests_fail);
    return tests_fail;
}
