/**
 * Pruebas unitarias JMN: decaimiento, olvido de débiles, consolidación (Fase B IA).
 * Compilar con los mismos objetos JMN que test_jmn (ver build_test_jmn.bat).
 */
#include "memoria_neuronal.h"
#include <stdio.h>
#include <stdlib.h>

static int fail(const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void) {
    JMNMemoria* mem = jmn_crear_memoria_ram(256, 1024);
    if (!mem) return fail("crear RAM");

    uint32_t a = 100, b = 200, c = 300;
    JMNValor v1 = { .f = 1.0f };
    jmn_agregar_nodo(mem, a, v1);
    jmn_agregar_nodo(mem, b, v1);
    jmn_agregar_nodo(mem, c, v1);

    JMNValor debil = { .f = 0.04f };
    JMNValor fuerte = { .f = 0.9f };
    jmn_agregar_conexion(mem, a, b, debil, 0);
    jmn_agregar_conexion(mem, a, c, fuerte, 0);

    jmn_decaer_conexiones_global(mem, 0.5f, 1, 0.05f);
    float fa = jmn_obtener_fuerza_asociacion(mem, a, b);
    float fb = jmn_obtener_fuerza_asociacion(mem, a, c);
    if (fa > 0.001f) return fail("arista debil deberia anularse tras decaer (0.04*0.5 < 0.05)");
    if (fb < 0.35f || fb > 0.55f) return fail("arista fuerte deberia ~0.45 tras un decaimiento 50%");

    uint32_t n = jmn_olvidar_conexiones_debiles(mem, 0.05f);
    if (n < 1) return fail("deberia eliminarse al menos la arista debil");
    if (jmn_obtener_fuerza_asociacion(mem, a, b) > 0.f) return fail("slot a-b debe liberarse");

    jmn_consolidar_conexiones_supervivientes(mem, 0.1f);
    float fc = jmn_obtener_fuerza_asociacion(mem, a, c);
    if (fc < fb * 1.05f) return fail("consolidacion debe multiplicar supervivientes");

    jmn_agregar_conexion(mem, a, b, debil, 0);
    jmn_consolidar_memoria_sueno(mem, 0.2f, 2, 0.03f, 0.05f);
    if (jmn_obtener_fuerza_asociacion(mem, a, b) > 0.001f)
        return fail("sueno completo deberia eliminar la arista debil");
    if (jmn_obtener_fuerza_asociacion(mem, a, c) < 0.2f)
        return fail("sueno completo deberia conservar arista fuerte");

    jmn_cerrar(mem);
    printf("OK test_jmn_sueno\n");
    return 0;
}
