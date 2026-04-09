#include "memoria_neuronal.h"
#include <stdio.h>

int main() {
    printf("Testeando JMN...\n");
    JMNMemoria* mem = jmn_abrir_escritura("test_cog.jmn");
    if (!mem) {
        printf("Error: No se pudo abrir memoria\n");
        return 1;
    }
    
    uint32_t id_sol = jmn_estructura_id_texto("Sol");
    uint32_t id_calor = jmn_estructura_id_texto("Calor");
    
    JMNValor v1 = { .f = 1.0f };
    JMNValor v08 = { .f = 0.8f };
    jmn_agregar_nodo(mem, id_sol, v1);
    jmn_agregar_nodo(mem, id_calor, v1);
    jmn_agregar_conexion(mem, id_sol, id_calor, v08, 0);
    
    jmn_finalizar_escritura(mem);
    jmn_cerrar(mem);
    
    // Reabrir y verificar
    mem = jmn_abrir_lectura("test_cog.jmn");
    JMNNodo* nodo = jmn_obtener_nodo(mem, id_sol);
    if (!nodo) {
        printf("Error: Nodo Sol no encontrado\n");
        return 1;
    }
    
    uint32_t count = 0;
    JMNConexion* conns = jmn_obtener_conexiones(mem, nodo, &count);
    printf("Conexiones de Sol: %u\n", count);
    if (count > 0 && conns) {
        printf("  -> Destino: %u, Fuerza: %.2f\n", conns[0].destino_id, conns[0].fuerza.f);
    }
    
    jmn_cerrar(mem);
    return 0;
}
