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
#define JMN_VERSION 1

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

int jmn_io_guardar(JMNMemoria* mem, const char* ruta) {
    if (!mem || !ruta) return -1;
    if (jmn_io_asegurar_directorios_padre(ruta) != 0) return -1;
    FILE* f = fopen(ruta, "wb");
    if (!f) return -1;

    uint32_t magic = JMN_MAGIC;
    uint32_t version = JMN_VERSION;
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
    if (version != JMN_VERSION) {
        /* Migración: versión futura podría manejarse aquí */
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
        if (toread > 255) toread = 255;
        if (fread(mem->textos[slot].texto, 1, toread, f) != toread) break;
        crc = jmn_crc32_update(crc, mem->textos[slot].texto, toread);
        mem->textos[slot].texto[255] = '\0';
        mem->textos[slot].used = 1;

        /* REPARACIÓN: Actualizar hash_textos para que find_texto_slot funcione tras cargar */
        uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
        mem->textos[slot].next_hash = mem->hash_textos[h];
        mem->hash_textos[h] = slot;

        mem->num_textos++;
    }

    /* Checksum: si existe (formato nuevo), verificar; si no (formato viejo), aceptar */
    uint32_t stored_crc;
    if (fread(&stored_crc, 4, 1, f) == 1 && stored_crc != (~crc)) {
        fclose(f);
        return -1;  /* Checksum inválido: archivo corrupto */
    }
    fclose(f);
    return 0;
}
