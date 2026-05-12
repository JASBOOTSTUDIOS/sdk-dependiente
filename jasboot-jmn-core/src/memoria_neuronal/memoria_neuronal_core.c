/**
 * JMN Core: apertura, cierre, persistencia
 */
#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern int jmn_io_guardar(JMNMemoria* mem, const char* ruta);
extern int jmn_io_cargar(JMNMemoria* mem, const char* ruta);

uint32_t jmn_hash_u32(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x);
    return x;
}

uint32_t jmn_hash_str(const char* s) {
    uint32_t h = 5381;
    if (!s) return 0;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return h;
}

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static void* jmn_huge_alloc(JMNMemoria* m, size_t size, const char* name) {
#if defined(_WIN32) || defined(_WIN64)
    if (m && !m->es_ram) { 
        char temp_path[MAX_PATH];
        GetTempPathA(MAX_PATH, temp_path);
        char file_path[MAX_PATH];
        snprintf(file_path, MAX_PATH, "%s\\jmn_%s_%p.swap", temp_path, name ? name : "anon", m);
        
        HANDLE hFile = CreateFileA(file_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_RANDOM_ACCESS, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, (DWORD)(size >> 32), (DWORD)(size & 0xFFFFFFFF), NULL);
            if (hMap) {
                void* ptr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
                CloseHandle(hMap);
                CloseHandle(hFile);
                if (ptr) {
                    m->es_mapeado = 1;
                    fprintf(stderr, "[JMN] Huge alloc %zu bytes mapped to %s\n", size, file_path);
                    return ptr;
                }
            } else {
                fprintf(stderr, "[JMN ERROR] CreateFileMappingA failed for %s (size %zu), error %lu\n", file_path, size, GetLastError());
            }
            CloseHandle(hFile);
        } else {
            fprintf(stderr, "[JMN ERROR] CreateFileA failed for %s, error %lu\n", file_path, GetLastError());
        }
    }
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    int fd = -1;
    if (m && !m->es_ram) {
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "/tmp/jmn_%s_%p.swap", name ? name : "anon", m);
        fd = open(file_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd != -1) {
            unlink(file_path);
            ftruncate(fd, size);
            flags = MAP_SHARED;
            m->es_mapeado = 1;
        }
    }
    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (fd != -1) close(fd);
    return ptr == MAP_FAILED ? NULL : ptr;
#endif
}

static void jmn_huge_free(void* ptr, size_t size) {
    if (!ptr) return;
#if defined(_WIN32) || defined(_WIN64)
    if (!UnmapViewOfFile(ptr)) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
#else
    munmap(ptr, size);
#endif
}

static JMNMemoria* jmn_alloc(uint32_t cap_nodos, uint32_t cap_conex, const char* ruta) {
    JMNMemoria* m = (JMNMemoria*)calloc(1, sizeof(JMNMemoria));
    if (!m) return NULL;
    m->cap_nodos = cap_nodos ? cap_nodos : JMN_DEFAULT_NODOS;
    m->cap_conexiones = cap_conex ? cap_conex : JMN_DEFAULT_CONEX;
    m->cap_textos = m->cap_nodos * 2; 
    if (m->cap_textos < 50000) m->cap_textos = 50000;
    m->num_listas = 0;
    m->num_mapas = 0;
    if (ruta) strncpy(m->ruta_archivo, ruta, sizeof(m->ruta_archivo)-1);

    size_t sz_nodos = (size_t)m->cap_nodos * sizeof(JMNEntradaNodo);
    size_t sz_conex = (size_t)m->cap_conexiones * sizeof(JMNEntradaConexion);
    size_t sz_textos = (size_t)m->cap_textos * sizeof(JMNEntradaTexto);
    size_t sz_hash = (size_t)JMN_HASH_SIZE * sizeof(uint32_t);
    size_t sz_cabeza = (size_t)(m->cap_nodos + 1) * sizeof(uint32_t);

    m->nodos = (JMNEntradaNodo*)jmn_huge_alloc(m, sz_nodos, "nodos");
    m->hash_nodos = (uint32_t*)jmn_huge_alloc(m, sz_hash, "hash_nodos");
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_nodos[i] = 0xFFFFFFFF;

    m->conexiones = (JMNEntradaConexion*)jmn_huge_alloc(m, sz_conex, "conex");
    m->hash_conexiones = (uint32_t*)jmn_huge_alloc(m, sz_hash, "hash_conex");
    m->cabeza_origen = (uint32_t*)jmn_huge_alloc(m, sz_cabeza, "cabeza");
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_conexiones[i] = 0xFFFFFFFF;
    for (uint32_t i = 0; i <= m->cap_nodos; i++) m->cabeza_origen[i] = 0xFFFFFFFF;

    m->textos = (JMNEntradaTexto*)jmn_huge_alloc(m, sz_textos, "textos");
    m->hash_textos = (uint32_t*)jmn_huge_alloc(m, sz_hash, "hash_textos");
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_textos[i] = 0xFFFFFFFF;

    m->listas = (JMNLista*)calloc(10000, sizeof(JMNLista));
    for (uint32_t i = 0; i < 10000; i++) m->listas[i].next_hash = 0xFFFFFFFF;
    m->hash_listas = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_listas[i] = 0xFFFFFFFF;

    m->mapas = (JMNMapa*)calloc(10000, sizeof(JMNMapa));
    m->hash_mapas = (uint32_t*)calloc(JMN_HASH_SIZE, sizeof(uint32_t));
    for (uint32_t i = 0; i < JMN_HASH_SIZE; i++) m->hash_mapas[i] = 0xFFFFFFFF;

    if (!m->nodos || !m->hash_nodos || !m->conexiones || !m->hash_conexiones ||
        !m->cabeza_origen || !m->textos || !m->hash_textos ||
        !m->listas || !m->hash_listas || !m->mapas || !m->hash_mapas) {
        jmn_cerrar(m);
        return NULL;
    }
    return m;
}

static void jmn_free_data(JMNMemoria* m) {
    if (!m) return;
    jmn_huge_free(m->nodos, (size_t)m->cap_nodos * sizeof(JMNEntradaNodo));
    jmn_huge_free(m->hash_nodos, (size_t)JMN_HASH_SIZE * sizeof(uint32_t));
    jmn_huge_free(m->conexiones, (size_t)m->cap_conexiones * sizeof(JMNEntradaConexion));
    jmn_huge_free(m->hash_conexiones, (size_t)JMN_HASH_SIZE * sizeof(uint32_t));
    jmn_huge_free(m->cabeza_origen, (size_t)(m->cap_nodos + 1) * sizeof(uint32_t));
    jmn_huge_free(m->textos, (size_t)m->cap_textos * sizeof(JMNEntradaTexto));
    jmn_huge_free(m->hash_textos, (size_t)JMN_HASH_SIZE * sizeof(uint32_t));
    if (m->listas) {
        for (uint32_t i = 0; i < 10000; i++) {
            if (m->listas[i].items) free(m->listas[i].items);
        }
        free(m->listas);
    }
    free(m->hash_listas);
    if (m->mapas) {
        for (uint32_t i = 0; i < 10000; i++) {
            if (m->mapas[i].keys) free(m->mapas[i].keys);
            if (m->mapas[i].vals) free(m->mapas[i].vals);
        }
        free(m->mapas);
    }
    free(m->hash_mapas);
}

JMNMemoria* jmn_crear(const char* ruta) {
    JMNMemoria* m = jmn_alloc(JMN_DEFAULT_NODOS, JMN_DEFAULT_CONEX, ruta);
    if (!m) return NULL;
    m->es_ram = 0;
    m->dirty = 0;
    return m;
}

JMNMemoria* jmn_abrir_escritura_cap(const char* ruta, uint32_t cap_nodos, uint32_t cap_conexiones) {
    if (!ruta || !ruta[0]) return NULL;
    uint32_t cn = cap_nodos ? cap_nodos : JMN_DEFAULT_NODOS;
    uint32_t cc = cap_conexiones ? cap_conexiones : JMN_DEFAULT_CONEX;
    JMNMemoria* m = jmn_alloc(cn, cc, ruta);
    if (!m) return NULL;
    m->es_ram = 0;
    m->dirty = 0;
    if (jmn_io_cargar(m, ruta) == 0) {
        m->dirty = 0;
    }
    jmn_journal_log_size_if_any(m);
    return m;
}

JMNMemoria* jmn_abrir_escritura(const char* ruta) {
    return jmn_abrir_escritura_cap(ruta, JMN_DEFAULT_NODOS, JMN_DEFAULT_CONEX);
}

JMNMemoria* jmn_abrir_lectura(const char* ruta) {
    JMNMemoria* m = jmn_abrir_escritura(ruta);
    return m;
}

void jmn_finalizar_escritura(JMNMemoria* mem) {
    if (!mem) return;
    if (!mem->es_ram && mem->dirty && mem->ruta_archivo[0]) {
        if (jmn_io_guardar(mem, mem->ruta_archivo) == 0) {
            mem->dirty = 0;
            jmn_journal_commit(mem);
        }
    }
}

void jmn_cerrar(JMNMemoria* mem) {
    if (!mem) return;
    jmn_finalizar_escritura(mem);
    jmn_free_data(mem);
    free(mem);
}

void jmn_sincronizar_disco(JMNMemoria* mem) {
    if (!mem || mem->es_ram || !mem->es_mapeado) return;
#if defined(_WIN32) || defined(_WIN64)
    FlushViewOfFile(mem->nodos, 0);
    FlushViewOfFile(mem->conexiones, 0);
    FlushViewOfFile(mem->textos, 0);
    FlushViewOfFile(mem->hash_nodos, 0);
    FlushViewOfFile(mem->hash_conexiones, 0);
    FlushViewOfFile(mem->hash_textos, 0);
    FlushViewOfFile(mem->cabeza_origen, 0);
#else
    msync(mem->nodos, (size_t)mem->cap_nodos * sizeof(JMNEntradaNodo), MS_ASYNC);
    msync(mem->conexiones, (size_t)mem->cap_conexiones * sizeof(JMNEntradaConexion), MS_ASYNC);
    msync(mem->textos, (size_t)mem->cap_textos * sizeof(JMNEntradaTexto), MS_ASYNC);
    msync(mem->hash_nodos, (size_t)JMN_HASH_SIZE * sizeof(uint32_t), MS_ASYNC);
    msync(mem->hash_conexiones, (size_t)JMN_HASH_SIZE * sizeof(uint32_t), MS_ASYNC);
    msync(mem->hash_textos, (size_t)JMN_HASH_SIZE * sizeof(uint32_t), MS_ASYNC);
    msync(mem->cabeza_origen, (size_t)(mem->cap_nodos + 1) * sizeof(uint32_t), MS_ASYNC);
#endif
}

JMNMemoria* jmn_crear_memoria_ram(uint32_t cap_nodos, uint32_t cap_conex) {
    JMNMemoria* m = jmn_alloc(cap_nodos, cap_conex, NULL);
    if (!m) return NULL;
    m->es_ram = 1;
    m->ruta_archivo[0] = '\0';
    return m;
}
