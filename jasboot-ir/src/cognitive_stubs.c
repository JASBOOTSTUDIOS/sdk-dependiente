#include <stdint.h>
#include "memoria_neuronal.h"

// Stubs con las firmas EXACTAS de memoria_neuronal.h

int jmn_procesar_texto(JMNMemoria* mem, uint32_t id_origen) {
    (void)mem; (void)id_origen;
    return (int)id_origen;
}

uint32_t jmn_pensar_respuesta(JMNMemoria* mem, uint32_t id) {
    (void)mem; (void)id;
    return id;
}

uint32_t jmn_razonamiento_multipath(JMNMemoria* mem, uint32_t id, int profundidad) {
    (void)mem; (void)id; (void)profundidad;
    return id;
}

int jmn_asociar_secuencia(JMNMemoria* mem, uint32_t id_sec, uint32_t id_lista) {
    (void)mem; (void)id_sec; (void)id_lista;
    return 0;
}

uint32_t jmn_pensar_siguiente(JMNMemoria* mem, uint32_t id) {
    (void)mem; (void)id;
    return 0;
}

uint32_t jmn_pensar_anterior(JMNMemoria* mem, uint32_t id) {
    (void)mem; (void)id;
    return 0;
}

int jmn_marcar_estado(JMNMemoria* mem, uint32_t id_nombre, float valor) {
    (void)mem; (void)id_nombre; (void)valor;
    return 0;
}

int jmn_observar(JMNMemoria* mem, uint32_t id) {
    (void)mem; (void)id;
    return 0;
}

uint32_t jmn_registrar_patron(JMNMemoria* mem, uint32_t id_lista) {
    (void)mem; (void)id_lista;
    return 0;
}

int jmn_corregir_secuencia(JMNMemoria* mem, uint32_t id_lista) {
    (void)mem; (void)id_lista;
    return 0;
}

int jmn_asociar_relacion(JMNMemoria* mem, uint32_t id_a, uint32_t id_b, uint32_t tipo) {
    (void)mem; (void)id_a; (void)id_b; (void)tipo;
    return 0;
}

float jmn_comparar_patrones(JMNMemoria* mem, uint32_t id_a, uint32_t id_b) {
    (void)mem; (void)id_a; (void)id_b;
    return 0.0f;
}

uint32_t jmn_obtener_relacionados(JMNMemoria* mem, uint32_t id) {
    (void)mem; (void)id;
    return 0;
}

int jmn_extraer_caracter(JMNMemoria* mem, uint32_t id_frase, int32_t indice, uint32_t id_destino) {
    (void)mem; (void)id_frase; (void)indice; (void)id_destino;
    return 0;
}
