/**
 * Test exhaustivo para n_grafo (Fases 1-9)
 * Compilar: gcc -Wall -I. n_grafo_core.c test_n_grafo.c -o test_n_grafo
 * Ejecutar: ./test_n_grafo
 */

#include "n_grafo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a),(b)) == 0, msg)
#define ASSERT_NOT_NULL(p, msg) ASSERT((p) != NULL, msg)
#define ASSERT_NULL(p, msg) ASSERT((p) == NULL, msg)

int main(void) {
    const char* ruta = "test_n_grafo.ngf";
    remove(ruta);
    char buf[256];

    /* --- Tests con grafo NULL --- */
    ASSERT_EQ(n_obtener_id(NULL, "x"), NGF_ID_NULO, "n_obtener_id(NULL) debe devolver NGF_ID_NULO");
    ASSERT_EQ(n_obtener_texto(NULL, 1, buf, sizeof(buf)), 0, "n_obtener_texto(NULL) debe devolver 0");
    ASSERT_EQ(n_existe_concepto(NULL, "x"), 0, "n_existe_concepto(NULL) debe devolver 0");
    ASSERT_EQ(n_recordar(NULL, "a", "b", "c"), 0, "n_recordar(NULL) debe devolver 0");
    ASSERT_EQ(n_buscar_objeto(NULL, "a", "b"), NGF_ID_NULO, "n_buscar_objeto(NULL) debe devolver NGF_ID_NULO");
    ASSERT_EQ(n_buscar_objetos(NULL, "a", "b", NULL, 0), 0, "n_buscar_objetos(NULL) debe devolver 0");
    ASSERT_EQ(n_buscar_sujeto(NULL, "b", "c"), NGF_ID_NULO, "n_buscar_sujeto(NULL) debe devolver NGF_ID_NULO");
    ASSERT_EQ(n_buscar_sujetos(NULL, "b", "c", NULL, 0), 0, "n_buscar_sujetos(NULL) debe devolver 0");
    ASSERT_EQ(n_buscar_donde_aparece(NULL, "c", NULL, NULL, 0), 0, "n_buscar_donde_aparece(NULL) debe devolver 0");
    ASSERT_EQ(n_buscar_predicados(NULL, "a", NULL, 0), 0, "n_buscar_predicados(NULL) debe devolver 0");
    ASSERT_EQ(n_olvidar_triple(NULL, "a", "b", "c"), 0, "n_olvidar_triple(NULL) debe devolver 0");
    ASSERT_EQ(n_buscar_objeto_texto(NULL, "a", "b", buf, sizeof(buf)), 0, "n_buscar_objeto_texto(NULL) debe devolver 0");
    ASSERT_EQ(n_lista_triples(NULL, "a", NULL, NULL, 0), 0, "n_lista_triples(NULL) debe devolver 0");
    ASSERT_EQ(n_tamano_grafo(NULL, NULL), 0, "n_tamano_grafo(NULL) debe devolver 0");
    ASSERT_EQ(n_heredar(NULL, "a", "b"), NGF_ID_NULO, "n_heredar(NULL)");
    n_cerrar_grafo(NULL); /* no debe crashear */
    n_configurar_cache_lru(NULL, 64);   /* no debe crashear */
    n_configurar_bloom(NULL, 1024);     /* no debe crashear */

    /* --- Abrir/crear grafo --- */
    NGrafo* g = n_abrir_grafo(ruta);
    ASSERT_NOT_NULL(g, "n_abrir_grafo debe devolver grafo válido");
    n_configurar_cache_lru(g, 64);   /* Fase 7.1: caché LRU */
    n_configurar_bloom(g, 1024);     /* Fase 7.2: Bloom filter */
    ASSERT_EQ(n_grafo_valido(g), 1, "n_grafo_valido debe ser 1");

    /* --- Vocabulario --- */
    uint32_t id_francia = n_obtener_id(g, "francia");
    uint32_t id_capital = n_obtener_id(g, "capital");
    uint32_t id_paris = n_obtener_id(g, "Paris");
    ASSERT(id_francia != NGF_ID_NULO && id_capital != NGF_ID_NULO && id_paris != NGF_ID_NULO,
           "n_obtener_id debe crear conceptos");
    ASSERT_EQ(n_obtener_id(g, "Francia"), id_francia, "normalización: Francia == francia");

    ASSERT_EQ(n_obtener_texto(g, id_francia, buf, sizeof(buf)), 1, "n_obtener_texto encontrado");
    ASSERT_STR_EQ(buf, "francia", "texto debe ser 'francia'");
    ASSERT_EQ(n_obtener_texto(g, 99999, buf, sizeof(buf)), 0, "n_obtener_texto ID inexistente");
    ASSERT_EQ(buf[0], '\0', "buf vacío para ID inexistente");

    ASSERT_EQ(n_existe_concepto(g, "capital"), 1, "n_existe_concepto existe");
    ASSERT_EQ(n_existe_concepto(g, "xyz123"), 0, "n_existe_concepto no existe");

    /* --- Triples --- */
    ASSERT_EQ(n_recordar(g, "francia", "capital", "paris"), 1, "n_recordar ok");
    ASSERT_EQ(n_recordar(g, "espana", "capital", "madrid"), 1, "n_recordar ok");
    ASSERT_EQ(n_recordar(g, "italia", "capital", "roma"), 1, "n_recordar italia ok");
    ASSERT_EQ(n_recordar(g, "francia", "poblacion", "67M"), 1, "n_recordar ok");
    ASSERT_EQ(n_recordar_peso(g, "francia", "area", "643801", 200), 1, "n_recordar_peso ok");

    ASSERT_EQ(n_buscar_objeto(g, "francia", "capital"), id_paris, "n_buscar_objeto paris");
    ASSERT_EQ(n_buscar_objeto(g, "inexistente", "capital"), NGF_ID_NULO, "n_buscar_objeto sujeto inexistente");
    ASSERT_EQ(n_buscar_objeto(g, "francia", "inexistente"), NGF_ID_NULO, "n_buscar_objeto predicado inexistente");

    uint32_t objs[8];
    size_t n_objs = n_buscar_objetos(g, "francia", "capital", objs, 8);
    ASSERT_EQ(n_objs, 1, "n_buscar_objetos count");
    ASSERT_EQ(objs[0], id_paris, "n_buscar_objetos valor");
    n_objs = n_buscar_objetos(g, "francia", "capital", objs, 8);
    ASSERT_EQ(n_objs, 1, "n_buscar_objetos count tras verificación previa");

    ASSERT_EQ(n_buscar_sujeto(g, "capital", "madrid"), n_obtener_id(g, "espana"), "n_buscar_sujeto espana");
    ASSERT_EQ(n_buscar_sujeto(g, "capital", "inexistente"), NGF_ID_NULO, "n_buscar_sujeto objeto inexistente");

    uint32_t subjs[8];
    size_t n_subjs = n_buscar_sujetos(g, "capital", "madrid", subjs, 8);
    ASSERT_EQ(n_subjs, 1, "n_buscar_sujetos count");
    ASSERT_EQ(subjs[0], n_obtener_id(g, "espana"), "n_buscar_sujetos valor");

    uint32_t preds[8];
    size_t np = n_buscar_predicados(g, "francia", preds, 8);
    ASSERT(np >= 3, "francia debe tener al menos 3 predicados"); /* capital, poblacion, area */

    /* --- n_buscar_donde_aparece --- */
    uint32_t subs[8], preds_da[8];
    size_t nd = n_buscar_donde_aparece(g, "paris", subs, preds_da, 8);
    ASSERT_EQ(nd, 1, "n_buscar_donde_aparece paris count");
    ASSERT_EQ(subs[0], id_francia, "paris aparece como objeto de francia");
    ASSERT_EQ(preds_da[0], id_capital, "paris con predicado capital");

    nd = n_buscar_donde_aparece(g, "ninguno", NULL, NULL, 0);
    ASSERT_EQ(nd, 0, "n_buscar_donde_aparece inexistente");

    /* --- n_olvidar_triple --- */
    ASSERT_EQ(n_olvidar_triple(g, "francia", "poblacion", "67M"), 1, "n_olvidar_triple ok");
    ASSERT_EQ(n_olvidar_triple(g, "francia", "poblacion", "67M"), 0, "n_olvidar_triple ya no existe");
    ASSERT_EQ(n_buscar_objeto(g, "francia", "poblacion"), NGF_ID_NULO, "tras olvidar, objeto no existe");

    np = n_buscar_predicados(g, "francia", preds, 8);
    ASSERT(np == 2, "francia debe tener 2 predicados tras olvidar"); /* capital, area */

    /* --- Fase 5: API alto nivel (italia ya añadido antes) --- */
    ASSERT_EQ(n_recordar_triple_texto(g, "portugal", "capital", "lisboa"), 1, "n_recordar_triple_texto ok");

    buf[0] = 'X';
    ASSERT_EQ(n_buscar_objeto_texto(g, "italia", "capital", buf, sizeof(buf)), 1, "n_buscar_objeto_texto italia ok");
    ASSERT_STR_EQ(buf, "roma", "n_buscar_objeto_texto contenido roma");
    ASSERT_EQ(n_buscar_objeto_texto(g, "portugal", "capital", buf, sizeof(buf)), 1, "n_buscar_objeto_texto portugal ok");
    ASSERT_STR_EQ(buf, "lisboa", "n_buscar_objeto_texto contenido lisboa");
    ASSERT_EQ(n_buscar_objeto_texto(g, "x", "y", buf, sizeof(buf)), 0, "n_buscar_objeto_texto no encontrado");
    ASSERT_EQ(buf[0], '\0', "buf vacío cuando no encontrado");
    ASSERT_EQ(n_buscar_objeto_texto(g, "a", "b", NULL, 10), 0, "buf NULL debe devolver 0");
    ASSERT_EQ(n_buscar_objeto_texto(g, "a", "b", buf, 0), 0, "buf_size 0 debe devolver 0");

    uint32_t plist[8], olist[8];
    size_t nt = n_lista_triples(g, "francia", plist, olist, 8);
    ASSERT(nt >= 2, "francia debe tener al menos 2 triples (capital, area)");
    nt = n_lista_triples(g, "inexistente", plist, olist, 8);
    ASSERT_EQ(nt, 0, "n_lista_triples sujeto inexistente");
    nt = n_lista_triples(g, "francia", plist, olist, 8);
    ASSERT(nt >= 2, "n_lista_triples francia tiene al menos 2 (capital, area)");

    size_t num_triples = n_tamano_grafo(g, NULL);
    size_t num_conc;
    num_triples = n_tamano_grafo(g, &num_conc);
    ASSERT(num_triples >= 5, "debe haber al menos 5 triples (francia x3, espana, italia)");
    ASSERT(num_conc >= 8, "debe haber al menos 8 conceptos");

    /* --- Persistencia: cerrar y reabrir --- */
    size_t triples_antes = n_tamano_grafo(g, NULL);
    n_cerrar_grafo(g);
    g = n_abrir_grafo(ruta);
    ASSERT_NOT_NULL(g, "reabrir grafo");
    size_t triples_despues = n_tamano_grafo(g, NULL);
    ASSERT_EQ(triples_despues, triples_antes, "persistencia: mismo número de triples");
    ASSERT_EQ(n_buscar_objeto(g, "francia", "capital"), id_paris, "persistencia: francia capital paris");
    /* Verificar que el vocabulario y al menos un triple persisten */
    ASSERT_EQ(n_existe_concepto(g, "francia"), 1, "persistencia: concepto francia existe");
    ASSERT_EQ(n_existe_concepto(g, "capital"), 1, "persistencia: concepto capital existe");
    ASSERT_EQ(n_buscar_objeto(g, "francia", "poblacion"), NGF_ID_NULO, "persistencia: olvidado no debe existir");
    n_obtener_texto(g, n_obtener_id(g, "francia"), buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "francia", "persistencia: vocabulario");

    /* --- Múltiples objetos para mismo (S,P) --- */
    n_recordar(g, "francia", "idioma", "frances");
    n_recordar(g, "francia", "idioma", "occitano");
    n_objs = n_buscar_objetos(g, "francia", "idioma", objs, 8);
    ASSERT_EQ(n_objs, 2, "múltiples objetos para (francia, idioma)");
    nt = n_lista_triples(g, "francia", plist, olist, 8);
    ASSERT(nt >= 3, "francia con varios triples (capital, area, idioma x2)");

    /* --- n_buscar_donde_aparece con múltiples referencias --- */
    n_recordar(g, "belgica", "idioma", "frances");
    nd = n_buscar_donde_aparece(g, "frances", subs, preds_da, 8);
    ASSERT_EQ(nd, 2, "frances aparece en francia y belgica");

    /* --- Fase 9: n_heredar (herencia vía n_es_un) --- */
    n_recordar(g, "perro", "n_es_un", "animal");
    n_recordar(g, "animal", "patas", "cuatro");
    ASSERT_EQ(n_heredar(g, "perro", "patas"), n_obtener_id(g, "cuatro"), "n_heredar perro->animal->patas");
    ASSERT_EQ(n_heredar(g, "gato", "patas"), NGF_ID_NULO, "gato sin n_es_un no hereda");
    n_recordar(g, "gato", "color", "negro");
    ASSERT_EQ(n_heredar(g, "gato", "color"), n_obtener_id(g, "negro"), "n_heredar directo gato color");
    n_recordar(g, "labrador", "n_es_un", "perro");
    ASSERT_EQ(n_heredar(g, "labrador", "patas"), n_obtener_id(g, "cuatro"), "n_heredar transitivo labrador->perro->animal->patas");
    ASSERT_EQ(n_heredar_texto(g, "perro", "patas", buf, sizeof(buf)), 1, "n_heredar_texto ok");
    ASSERT_STR_EQ(buf, "cuatro", "n_heredar_texto contenido cuatro");
    ASSERT_EQ(n_heredar(g, "x", "y"), NGF_ID_NULO, "n_heredar sujeto inexistente");
    ASSERT_EQ(n_heredar(g, "perro", "inexistente"), NGF_ID_NULO, "n_heredar predicado inexistente");

    /* n_heredar_texto edge cases */
    ASSERT_EQ(n_heredar_texto(NULL, "perro", "patas", buf, sizeof(buf)), 0, "n_heredar_texto(NULL)");
    ASSERT_EQ(n_heredar_texto(g, "perro", "patas", NULL, 10), 0, "n_heredar_texto buf NULL");
    ASSERT_EQ(n_heredar_texto(g, "perro", "patas", buf, 0), 0, "n_heredar_texto buf_size 0");
    ASSERT_EQ(n_heredar_texto(g, "x", "y", buf, sizeof(buf)), 0, "n_heredar_texto no encontrado");
    ASSERT_EQ(buf[0], '\0', "n_heredar_texto buf vacío cuando no encontrado");

    /* Ciclo n_es_un: A->B->A no debe colgar. ciclo_b hereda de ciclo_a. */
    n_recordar(g, "ciclo_a", "n_es_un", "ciclo_b");
    n_recordar(g, "ciclo_b", "n_es_un", "ciclo_a");
    n_recordar(g, "ciclo_a", "valor", "va");
    ASSERT_EQ(n_heredar(g, "ciclo_a", "valor"), n_obtener_id(g, "va"), "n_heredar con ciclo directo ok");
    ASSERT_EQ(n_heredar(g, "ciclo_b", "valor"), n_obtener_id(g, "va"), "ciclo: ciclo_b hereda valor vía ciclo_a");
    n_recordar(g, "solo_x", "n_es_un", "solo_y");
    n_recordar(g, "solo_y", "n_es_un", "solo_x");
    ASSERT_EQ(n_heredar(g, "solo_x", "inexistente"), NGF_ID_NULO, "ciclo sin salida no cuelga");

    /* Fase 7: configurar y desactivar LRU/Bloom */
    n_configurar_cache_lru(g, 0);
    n_configurar_bloom(g, 0);
    ASSERT_EQ(n_buscar_objeto(g, "francia", "capital"), id_paris, "tras desactivar cache, búsqueda ok");
    n_configurar_cache_lru(g, 32);
    n_configurar_bloom(g, 512);

    /* n_recordar con parámetros NULL/edge */
    ASSERT_EQ(n_recordar(g, NULL, "p", "o"), 0, "n_recordar sujeto NULL");
    ASSERT_EQ(n_recordar(g, "s", NULL, "o"), 0, "n_recordar predicado NULL");
    ASSERT_EQ(n_recordar(g, "s", "p", NULL), 0, "n_recordar objeto NULL");

    /* Robustez: n_recordar_peso NULL, pesos límite 0 y 255 */
    ASSERT_EQ(n_recordar_peso(g, NULL, "p", "o", 100), 0, "n_recordar_peso sujeto NULL");
    ASSERT_EQ(n_recordar_peso(g, "peso0", "p", "cero", 0), 1, "n_recordar_peso peso 0 ok");
    ASSERT_EQ(n_recordar_peso(g, "peso255", "p", "max", 255), 1, "n_recordar_peso peso 255 ok");

    /* Robustez: n_obtener_id texto vacío, n_obtener_texto ID 0 */
    ASSERT_EQ(n_obtener_id(g, ""), NGF_ID_NULO, "n_obtener_id texto vacío");
    ASSERT_EQ(n_obtener_texto(g, NGF_ID_NULO, buf, sizeof(buf)), 0, "n_obtener_texto ID 0");

    /* Robustez: buffers pequeños, truncado */
    n_recordar(g, "corto", "v", "ab");
    ASSERT_EQ(n_obtener_texto(g, n_obtener_id(g, "corto"), buf, 1), 1, "n_obtener_texto buf_size 1");
    ASSERT_EQ(buf[0], '\0', "buf 1 byte debe ser vacío (truncado seguro)");
    ASSERT_EQ(n_obtener_texto(g, n_obtener_id(g, "corto"), buf, 2), 1, "n_obtener_texto buf_size 2");
    ASSERT_EQ(buf[0], 'c', "buf 2 bytes trunca a 1 char");
    ASSERT_EQ(buf[1], '\0', "buf null-terminated");

    /* Robustez: n_buscar_objetos con max_count menor que resultados (truncado) */
    n_recordar(g, "multi", "n", "v1");
    n_recordar(g, "multi", "n", "v2");
    n_recordar(g, "multi", "n", "v3");
    n_objs = n_buscar_objetos(g, "multi", "n", objs, 2);
    ASSERT_EQ(n_objs, 2, "n_buscar_objetos truncado a max_count");
    n_objs = n_buscar_objetos(g, "multi", "n", NULL, 8);
    ASSERT_EQ(n_objs, 3, "n_buscar_objetos ids NULL con max_count devuelve count");
    n_objs = n_buscar_objetos(g, "multi", "n", objs, 8);
    ASSERT_EQ(n_objs, 3, "n_buscar_objetos 3 resultados");

    /* Robustez: n_lista_triples con predicados/objetos NULL (solo count) */
    nt = n_lista_triples(g, "multi", NULL, NULL, 8);
    ASSERT_EQ(nt, 3, "n_lista_triples solo count con NULL arrays");

    /* Robustez: n_donde_aparece con arrays NULL */
    nd = n_buscar_donde_aparece(g, "v1", NULL, NULL, 8);
    ASSERT(nd >= 1, "n_buscar_donde_aparece count con NULL arrays");

    /* Duplicado: n_recordar mismo triple dos veces (comportamiento: permite duplicados) */
    size_t tr_ant = n_tamano_grafo(g, NULL);
    n_recordar(g, "dup_s", "dup_p", "dup_o");
    n_recordar(g, "dup_s", "dup_p", "dup_o");
    size_t tr_desp = n_tamano_grafo(g, NULL);
    ASSERT(tr_desp >= tr_ant + 2, "n_recordar duplicado añade triples (permite múltiples)");
    n_objs = n_buscar_objetos(g, "dup_s", "dup_p", objs, 8);
    ASSERT(n_objs >= 2, "múltiples triples (S,P,O) idénticos");

    /* n_abrir_grafo ruta vacía/NULL */
    ASSERT_NULL(n_abrir_grafo(""), "n_abrir_grafo ruta vacía");
    ASSERT_NULL(n_abrir_grafo(NULL), "n_abrir_grafo ruta NULL");

    n_cerrar_grafo(g);
    remove(ruta);

    /* --- Resumen --- */
    printf("Tests ejecutados: %d\n", tests_run);
    if (tests_fail > 0) {
        printf("Tests fallidos: %d\n", tests_fail);
        return 1;
    }
    printf("Todos los tests pasaron correctamente.\n");
    return 0;
}
