#ifndef JMN_INTERNO_H
#define JMN_INTERNO_H

#include "memoria_neuronal.h"
#include <stddef.h>

#define JMN_HASH_SIZE 131071
#define JMN_DEFAULT_NODOS  200000
#define JMN_DEFAULT_CONEX  10000000
#define JMN_MAX_TEXTO_LEN  4096
#define JMN_JMN_MAGIC 0x4A4D4E31  /* "JMN1" */

typedef struct JMNEntradaNodo {
    uint32_t id;
    JMNValor peso;
    int used;
    uint32_t next_hash;
} JMNEntradaNodo;

typedef struct JMNEntradaConexion {
    uint32_t origen_id;
    uint32_t destino_id;
    uint32_t key_id;
    JMNValor fuerza;
    int used;
    uint32_t next_hash;
    uint32_t next_origen;
} JMNEntradaConexion;

typedef struct JMNEntradaTexto {
    uint32_t id;
    char texto[256];
    int used;
    uint32_t next_hash;
} JMNEntradaTexto;

typedef struct JMNLista {
    uint32_t id;           /* id lógico de la lista; 0 = hueco libre en el pool */
    uint32_t next_hash;    /* siguiente en cadena hash_listas (0xFFFFFFFF = fin) */
    JMNValor* items;
    uint32_t count;
    uint32_t cap;
} JMNLista;

typedef struct JMNMapa {
    uint32_t* keys;
    JMNValor* vals;
    uint32_t count;
    uint32_t cap;
} JMNMapa;

struct JMNMemoria {
    int es_ram;
    char ruta_archivo[512];
    int dirty;

    JMNEntradaNodo* nodos;
    uint32_t cap_nodos;
    uint32_t num_nodos;
    uint32_t* hash_nodos;

    JMNEntradaConexion* conexiones;
    uint32_t cap_conexiones;
    uint32_t num_conexiones;
    uint32_t* hash_conexiones;
    uint32_t* cabeza_origen;

    JMNEntradaTexto* textos;
    uint32_t cap_textos;
    uint32_t num_textos;
    uint32_t* hash_textos;

    JMNLista* listas;
    uint32_t* hash_listas;
    uint32_t num_listas;

    JMNMapa* mapas;
    uint32_t* hash_mapas;
    uint32_t num_mapas;
};

uint32_t jmn_hash_u32(uint32_t x);
uint32_t jmn_hash_str(const char* s);

#endif
