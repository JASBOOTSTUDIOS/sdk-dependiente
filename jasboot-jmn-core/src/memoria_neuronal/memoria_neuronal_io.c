/**
 * JMN I/O: persistencia .jmn
 * Formato: Ver docs/FORMATO_JMN.md
 * magic(4) version(4) num_nodos(4) num_conex(4) num_textos(4)
 * nodos | conexiones | textos | checksum(4)
 */
#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define JMN_MAGIC 0x4A4D4E31  /* "JMN1" en big-endian */
#define JMN_VERSION_V1 1
#define JMN_VERSION_V2 2
#define JMN_LISTA_MAPA_CAP 10000u

static int jmn_io_mkdir_one(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32) || defined(_WIN64)
    /* Ignorar directorios de unidad como "C:" */
    if (strlen(path) == 2 && path[1] == ':') return 0;
    if (_mkdir(path) == 0 || errno == EEXIST) return 0;
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST) return 0;
#endif
    return -1;
}

static int jmn_io_asegurar_directorios_padre(const char* ruta) {
    char dir[512];
    size_t len;
    size_t i;

    if (!ruta || !ruta[0]) return 0;
    len = strlen(ruta);
    if (len >= sizeof(dir)) len = sizeof(dir) - 1;
    memcpy(dir, ruta, len);
    dir[len] = '\0';

    for (i = len; i > 0; i--) {
        if (dir[i - 1] == '/' || dir[i - 1] == '\\') {
            dir[i - 1] = '\0';
            break;
        }
    }
    if (i == 0 || !dir[0]) return 0;

    for (i = 0; dir[i]; i++) {
        if (dir[i] == '/' || dir[i] == '\\') {
            char saved = dir[i];
            if (i == 0) continue;
            if (i == 2 && dir[1] == ':') continue;
            dir[i] = '\0';
            if (dir[0] && jmn_io_mkdir_one(dir) != 0) return -1;
            dir[i] = saved;
        }
    }

    return jmn_io_mkdir_one(dir);
}

/* CRC32 simple para integridad (polinomio estándar) */
static uint32_t jmn_crc32_update(uint32_t crc, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    static uint32_t table[256];
    static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = 1;
    }
    while (len--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc;
}

static uint32_t jmn_io_contar_listas_guardables(const JMNMemoria* mem) {
    uint32_t n = 0;
    if (!mem || !mem->listas) return 0;
    for (uint32_t i = 0; i < JMN_LISTA_MAPA_CAP; i++) {
        if (mem->listas[i].id != 0) n++;
    }
    return n;
}

static uint32_t jmn_io_contar_mapas_guardables(const JMNMemoria* mem) {
    uint32_t n = 0;
    if (!mem || !mem->mapas) return 0;
    for (uint32_t i = 0; i < JMN_LISTA_MAPA_CAP; i++) {
        if (mem->mapas[i].keys != NULL) n++;
    }
    return n;
}

int jmn_io_guardar(JMNMemoria* mem, const char* ruta) {
    if (!mem || !ruta) return -1;
    if (jmn_io_asegurar_directorios_padre(ruta) != 0) return -1;
    FILE* f = fopen(ruta, "wb");
    if (!f) return -1;

    uint32_t n_lista_p = jmn_io_contar_listas_guardables(mem);
    uint32_t n_map_p = jmn_io_contar_mapas_guardables(mem);
    int usar_v2 = (n_lista_p > 0 || n_map_p > 0);

    uint32_t magic = JMN_MAGIC;
    uint32_t version = usar_v2 ? JMN_VERSION_V2 : JMN_VERSION_V1;
    uint32_t crc = 0xFFFFFFFF;

    fwrite(&magic, 4, 1, f);
    crc = jmn_crc32_update(crc, &magic, 4);
    fwrite(&version, 4, 1, f);
    crc = jmn_crc32_update(crc, &version, 4);
    fwrite(&mem->num_nodos, 4, 1, f);
    crc = jmn_crc32_update(crc, &mem->num_nodos, 4);
    fwrite(&mem->num_conexiones, 4, 1, f);
    crc = jmn_crc32_update(crc, &mem->num_conexiones, 4);
    fwrite(&mem->num_textos, 4, 1, f);
    crc = jmn_crc32_update(crc, &mem->num_textos, 4);

    for (uint32_t i = 0; i < mem->cap_nodos; i++) {
        if (mem->nodos[i].used) {
            fwrite(&mem->nodos[i].id, 4, 1, f);
            fwrite(&mem->nodos[i].peso.u, 4, 1, f);
            crc = jmn_crc32_update(crc, &mem->nodos[i].id, 4);
            crc = jmn_crc32_update(crc, &mem->nodos[i].peso.u, 4);
        }
    }

    for (uint32_t i = 0; i < mem->cap_conexiones; i++) {
        if (mem->conexiones[i].used) {
            uint32_t buf[4] = {
                mem->conexiones[i].origen_id,
                mem->conexiones[i].destino_id,
                mem->conexiones[i].key_id,
                mem->conexiones[i].fuerza.u
            };
            fwrite(&buf[0], 4, 1, f);
            fwrite(&buf[1], 4, 1, f);
            fwrite(&buf[2], 4, 1, f);
            fwrite(&buf[3], 4, 1, f);
            crc = jmn_crc32_update(crc, buf, 16);
        }
    }

    for (uint32_t i = 0; i < mem->cap_textos; i++) {
        if (mem->textos[i].used) {
            size_t len = strnlen(mem->textos[i].texto, 255);
            uint16_t slen = (uint16_t)len;
            fwrite(&mem->textos[i].id, 4, 1, f);
            fwrite(&slen, 2, 1, f);
            fwrite(mem->textos[i].texto, 1, slen + 1, f);
            crc = jmn_crc32_update(crc, &mem->textos[i].id, 4);
            crc = jmn_crc32_update(crc, &slen, 2);
            crc = jmn_crc32_update(crc, mem->textos[i].texto, slen + 1);
        }
    }

    if (usar_v2) {
        fwrite(&n_lista_p, 4, 1, f);
        crc = jmn_crc32_update(crc, &n_lista_p, 4);
        for (uint32_t i = 0; i < JMN_LISTA_MAPA_CAP; i++) {
            if (mem->listas[i].id == 0) continue;
            uint32_t lid = mem->listas[i].id;
            uint32_t cnt = mem->listas[i].items ? mem->listas[i].count : 0;
            fwrite(&lid, 4, 1, f);
            fwrite(&cnt, 4, 1, f);
            crc = jmn_crc32_update(crc, &lid, 4);
            crc = jmn_crc32_update(crc, &cnt, 4);
            for (uint32_t k = 0; k < cnt; k++) {
                JMNValor val = mem->listas[i].items[k];
                fwrite(&val.u, 4, 1, f);
                crc = jmn_crc32_update(crc, &val.u, 4);
            }
        }
        fwrite(&n_map_p, 4, 1, f);
        crc = jmn_crc32_update(crc, &n_map_p, 4);
        for (uint32_t i = 0; i < JMN_LISTA_MAPA_CAP; i++) {
            if (!mem->mapas[i].keys) continue;
            uint32_t slot = i;
            uint32_t mc = mem->mapas[i].count;
            fwrite(&slot, 4, 1, f);
            fwrite(&mc, 4, 1, f);
            crc = jmn_crc32_update(crc, &slot, 4);
            crc = jmn_crc32_update(crc, &mc, 4);
            for (uint32_t k = 0; k < mc; k++) {
                uint32_t key = mem->mapas[i].keys[k];
                uint32_t vu = mem->mapas[i].vals[k].u;
                fwrite(&key, 4, 1, f);
                fwrite(&vu, 4, 1, f);
                crc = jmn_crc32_update(crc, &key, 4);
                crc = jmn_crc32_update(crc, &vu, 4);
            }
        }
    }

    uint32_t final_crc = ~crc;
    fwrite(&final_crc, 4, 1, f);
    fclose(f);
    return 0;
}

int jmn_io_cargar(JMNMemoria* mem, const char* ruta) {
    if (!mem || !ruta) return -1;
    FILE* f = fopen(ruta, "rb");
    if (!f) return -1;

    uint32_t crc = 0xFFFFFFFF;
    uint32_t magic, version;
    if (fread(&magic, 4, 1, f) != 1 || magic != JMN_MAGIC) {
        fclose(f);
        return -1;
    }
    crc = jmn_crc32_update(crc, &magic, 4);
    if (fread(&version, 4, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    if (version != JMN_VERSION_V1 && version != JMN_VERSION_V2) {
        fclose(f);
        return -1;
    }
    crc = jmn_crc32_update(crc, &version, 4);

    uint32_t num_nodos, num_conex, num_textos;
    if (fread(&num_nodos, 4, 1, f) != 1 ||
        fread(&num_conex, 4, 1, f) != 1 ||
        fread(&num_textos, 4, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    crc = jmn_crc32_update(crc, &num_nodos, 4);
    crc = jmn_crc32_update(crc, &num_conex, 4);
    crc = jmn_crc32_update(crc, &num_textos, 4);

    for (uint32_t i = 0; i < num_nodos; i++) {
        uint32_t id, pu;
        if (fread(&id, 4, 1, f) != 1 || fread(&pu, 4, 1, f) != 1) break;
        crc = jmn_crc32_update(crc, &id, 4);
        crc = jmn_crc32_update(crc, &pu, 4);
        uint32_t slot = id % mem->cap_nodos;
        uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
        while (mem->nodos[slot].used) slot = (slot + 1) % mem->cap_nodos;
        mem->nodos[slot].id = id;
        mem->nodos[slot].peso.u = pu;
        mem->nodos[slot].used = 1;
        mem->nodos[slot].next_hash = mem->hash_nodos[h];
        mem->hash_nodos[h] = slot;
        mem->num_nodos++;
    }

    for (uint32_t i = 0; i < num_conex; i++) {
        uint32_t ori, dest, key;
        union { float f; uint32_t u; } fu;
        if (fread(&ori, 4, 1, f) != 1 || fread(&dest, 4, 1, f) != 1 ||
            fread(&key, 4, 1, f) != 1 || fread(&fu.f, 4, 1, f) != 1) break;
        uint32_t buf[4] = {ori, dest, key, fu.u};
        crc = jmn_crc32_update(crc, buf, 16);
        uint32_t slot = (ori * 31 + dest) % mem->cap_conexiones;
        while (mem->conexiones[slot].used) slot = (slot + 1) % mem->cap_conexiones;
        mem->conexiones[slot].origen_id = ori;
        mem->conexiones[slot].destino_id = dest;
        mem->conexiones[slot].key_id = key;
        mem->conexiones[slot].fuerza.f = fu.f;
        mem->conexiones[slot].used = 1;
        mem->conexiones[slot].next_origen = mem->cabeza_origen[ori % (mem->cap_nodos + 1)];
        mem->cabeza_origen[ori % (mem->cap_nodos + 1)] = slot;
        mem->num_conexiones++;
    }

    for (uint32_t i = 0; i < num_textos; i++) {
        uint32_t id;
        uint16_t slen;
        if (fread(&id, 4, 1, f) != 1 || fread(&slen, 2, 1, f) != 1) break;
        crc = jmn_crc32_update(crc, &id, 4);
        crc = jmn_crc32_update(crc, &slen, 2);
        uint32_t slot = id % mem->cap_textos;
        while (mem->textos[slot].used) slot = (slot + 1) % mem->cap_textos;
        mem->textos[slot].id = id;
        size_t toread = slen + 1;
        if (toread > 256) toread = 256; /* Máximo incluyendo el nulo */
        
        char dummy[256];
        void* target = (toread <= 256) ? mem->textos[slot].texto : dummy;
        
        /* Si slen+1 es > 256, leemos el excedente en dummy para no desalinear el archivo */
        if (fread(mem->textos[slot].texto, 1, (toread > 256 ? 256 : toread), f) != (toread > 256 ? 256 : toread)) break;
        crc = jmn_crc32_update(crc, mem->textos[slot].texto, (toread > 256 ? 256 : toread));
        
        if (slen + 1 > 256) {
            size_t extra = (slen + 1) - 256;
            while (extra > 0) {
                size_t chunk = extra > sizeof(dummy) ? sizeof(dummy) : extra;
                if (fread(dummy, 1, chunk, f) != chunk) break;
                crc = jmn_crc32_update(crc, dummy, chunk);
                extra -= chunk;
            }
        }
        
        mem->textos[slot].texto[255] = '\0';
        mem->textos[slot].used = 1;

        /* REPARACIÓN: Actualizar hash_textos para que find_texto_slot funcione tras cargar */
        uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
        mem->textos[slot].next_hash = mem->hash_textos[h];
        mem->hash_textos[h] = slot;

        mem->num_textos++;
    }

    if (getenv("JASBOOT_DEBUG")) {
        fprintf(stderr, "[JMN IO] Cargados %u/%u nodos, %u/%u conex, %u/%u textos\n", 
                mem->num_nodos, num_nodos, mem->num_conexiones, num_conex, mem->num_textos, num_textos);
    }

    if (version == JMN_VERSION_V2) {
        uint32_t n_lista = 0;
        if (fread(&n_lista, 4, 1, f) != 1) {
            fclose(f);
            return -1;
        }
        crc = jmn_crc32_update(crc, &n_lista, 4);
        for (uint32_t li = 0; li < n_lista; li++) {
            uint32_t lid, cnt;
            if (fread(&lid, 4, 1, f) != 1 || fread(&cnt, 4, 1, f) != 1) {
                fclose(f);
                return -1;
            }
            crc = jmn_crc32_update(crc, &lid, 4);
            crc = jmn_crc32_update(crc, &cnt, 4);
            jmn_crear_lista(mem, lid);
            for (uint32_t k = 0; k < cnt; k++) {
                JMNValor val = {0};
                if (fread(&val.u, 4, 1, f) != 1) {
                    fclose(f);
                    return -1;
                }
                crc = jmn_crc32_update(crc, &val.u, 4);
                jmn_lista_agregar(mem, lid, val);
            }
        }
        uint32_t n_map = 0;
        if (fread(&n_map, 4, 1, f) != 1) {
            fclose(f);
            return -1;
        }
        crc = jmn_crc32_update(crc, &n_map, 4);
        for (uint32_t mi = 0; mi < n_map; mi++) {
            uint32_t slot, mc;
            if (fread(&slot, 4, 1, f) != 1 || fread(&mc, 4, 1, f) != 1) {
                fclose(f);
                return -1;
            }
            crc = jmn_crc32_update(crc, &slot, 4);
            crc = jmn_crc32_update(crc, &mc, 4);
            if (slot >= JMN_LISTA_MAPA_CAP) {
                fclose(f);
                return -1;
            }
            jmn_crear_mapa(mem, slot);
            for (uint32_t k = 0; k < mc; k++) {
                uint32_t key, vu;
                if (fread(&key, 4, 1, f) != 1 || fread(&vu, 4, 1, f) != 1) {
                    fclose(f);
                    return -1;
                }
                crc = jmn_crc32_update(crc, &key, 4);
                crc = jmn_crc32_update(crc, &vu, 4);
                JMNValor val = { .u = vu };
                jmn_mapa_insertar(mem, slot, key, val);
            }
        }
    }

    /* Checksum: si existe (formato nuevo), verificar; si no (formato viejo), aceptar */
    uint32_t stored_crc;
    if (fread(&stored_crc, 4, 1, f) == 1 && stored_crc != (~crc)) {
        if (getenv("JASBOOT_DEBUG")) {
            fprintf(stderr, "[JMN IO] ERROR: Checksum invalido en '%s' (leido: %08X, calculado: %08X)\n", 
                    ruta, (unsigned)stored_crc, (unsigned)(~crc));
        }
        fclose(f);
        return -1;  /* Checksum inválido: archivo corrupto */
    }
    mem->dirty = 0;
    fclose(f);
    return 0;
}
