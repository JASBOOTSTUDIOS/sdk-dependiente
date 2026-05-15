#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t find_texto_slot(JMNMemoria* mem, uint32_t id) {
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    uint32_t slot = mem->hash_textos[h];
    while (slot != 0xFFFFFFFF) {
        if (mem->textos[slot].id == id) return slot;
        slot = mem->textos[slot].next_hash;
    }
    return 0xFFFFFFFF;
}

static uint32_t alloc_texto_slot(JMNMemoria* mem, uint32_t id) {
    if (mem->num_textos >= mem->cap_textos) {
        // En un sistema real aquí redimensionaríamos, por ahora evitamos el bucle infinito
        fprintf(stderr, "[JMN ERROR] Capacidad de textos agotada (%u/%u)\n", mem->num_textos, mem->cap_textos);
        return 0; // Slot 0 suele ser reservado o ignorado en fallos
    }
    uint32_t slot = id % mem->cap_textos;
    uint32_t start_slot = slot;
    while (mem->textos[slot].used) {
        slot = (slot + 1) % mem->cap_textos;
        if (slot == start_slot) break; // No debería pasar por el check inicial
    }
    uint32_t h = jmn_hash_u32(id) % JMN_HASH_SIZE;
    mem->textos[slot].id = id;
    mem->textos[slot].texto[0] = '\0';
    mem->textos[slot].used = 1;
    mem->textos[slot].next_hash = mem->hash_textos[h];
    mem->hash_textos[h] = slot;
    mem->num_textos++;
    return slot;
}

int jmn_guardar_texto(JMNMemoria* mem, uint32_t id, const char* texto) {
    if (!mem || id == 0) return -1;
    uint32_t slot = find_texto_slot(mem, id);
    if (slot == 0xFFFFFFFF) slot = alloc_texto_slot(mem, id);
    strncpy(mem->textos[slot].texto, texto ? texto : "", 254);
    mem->textos[slot].texto[255] = '\0';
    if (!mem->es_ram) mem->dirty = 1;
    if (!mem->es_ram) jmn_journal_op_texto(mem, id, mem->textos[slot].texto);
    return 0;
}

int jmn_obtener_texto(JMNMemoria* mem, uint32_t id, char* buffer, size_t max_len) {
    if (!mem || !buffer || max_len == 0) return -1;
    buffer[0] = '\0';
    uint32_t slot = find_texto_slot(mem, id);
    if (slot == 0xFFFFFFFF) return -1;
    strncpy(buffer, mem->textos[slot].texto, max_len - 1);
    buffer[max_len - 1] = '\0';
    return (int)strlen(buffer);
}

int jmn_existe_texto(JMNMemoria* mem, uint32_t id) {
    if (!mem || id == 0) return 0;
    return find_texto_slot(mem, id) != 0xFFFFFFFF;
}

int jmn_listar_nodos_por_texto_exacto(JMNMemoria* mem, const char* literal, uint32_t* out, int max_out) {
    if (!mem || !literal || !literal[0] || !out || max_out <= 0) return 0;
    int n = 0;
    for (uint32_t i = 0; i < mem->cap_textos && n < max_out; i++) {
        if (!mem->textos[i].used) continue;
        if (strcmp(mem->textos[i].texto, literal) != 0) continue;
        if (!jmn_obtener_nodo(mem, mem->textos[i].id)) continue;
        out[n++] = mem->textos[i].id;
    }
    return n;
}

uint32_t jmn_buscar_id_por_texto_exacto(JMNMemoria* mem, const char* literal) {
    uint32_t buf[8];
    int n = jmn_listar_nodos_por_texto_exacto(mem, literal, buf, 8);
    if (n <= 0) return 0;
    uint32_t best = buf[0];
    for (int k = 1; k < n; k++) {
        uint32_t id = buf[k];
        int cand_rt = (id >= 0x80000000u);
        int best_rt = (best >= 0x80000000u);
        if (cand_rt && !best_rt)
            best = id;
        else if (cand_rt == best_rt && id < best)
            best = id;
    }
    return best;
}

int jmn_contiene_texto(JMNMemoria* mem, uint32_t id_frase, uint32_t id_patron) {
    char frase[512], patron[256];
    if (jmn_obtener_texto(mem, id_frase, frase, sizeof(frase)) < 0) return 0;
    if (jmn_obtener_texto(mem, id_patron, patron, sizeof(patron)) < 0) return 0;
    return strstr(frase, patron) != NULL ? 1 : 0;
}

int jmn_termina_con(JMNMemoria* mem, uint32_t id_frase, uint32_t id_sufijo) {
    char frase[512], suf[256];
    if (jmn_obtener_texto(mem, id_frase, frase, sizeof(frase)) < 0) return 0;
    if (jmn_obtener_texto(mem, id_sufijo, suf, sizeof(suf)) < 0) return 0;
    size_t lf = strlen(frase), ls = strlen(suf);
    if (ls > lf) return 0;
    return strcmp(frase + lf - ls, suf) == 0 ? 1 : 0;
}

void jmn_copiar_texto(JMNMemoria* mem, uint32_t id_o, uint32_t id_d) {
    char buf[512];
    if (jmn_obtener_texto(mem, id_o, buf, sizeof(buf)) >= 0)
        jmn_guardar_texto(mem, id_d, buf);
}

uint32_t jmn_ultima_palabra(JMNMemoria* mem, uint32_t id_frase, uint32_t id_destino) {
    char buf[512], *p, *last = NULL;
    if (jmn_obtener_texto(mem, id_frase, buf, sizeof(buf)) < 0) return 0;
    for (p = buf; *p; p++) {
        if (*p == ' ' || *p == '\t') last = p + 1;
    }
    if (last && *last) {
        jmn_guardar_texto(mem, id_destino, last);
        return jmn_hash_str(last);
    }
    jmn_guardar_texto(mem, id_destino, buf);
    return id_frase;
}

uint32_t jmn_ultima_silaba(JMNMemoria* mem, uint32_t id_frase, uint32_t id_destino) {
    (void)id_destino;
    return jmn_ultima_palabra(mem, id_frase, id_destino);
}

void jmn_extraer_antes_de(JMNMemoria* mem, uint32_t id_f, uint32_t id_p, uint32_t id_d) {
    char frase[512], patron[256], out[512] = "";
    if (jmn_obtener_texto(mem, id_f, frase, sizeof(frase)) < 0) return;
    if (jmn_obtener_texto(mem, id_p, patron, sizeof(patron)) < 0) return;
    char* pos = strstr(frase, patron);
    if (pos) {
        size_t n = (size_t)(pos - frase);
        if (n >= sizeof(out)) n = sizeof(out) - 1;
        memcpy(out, frase, n);
        out[n] = '\0';
    }
    jmn_guardar_texto(mem, id_d, out);
}

void jmn_extraer_despues_de(JMNMemoria* mem, uint32_t id_f, uint32_t id_p, uint32_t id_d) {
    char frase[512], patron[256];
    if (jmn_obtener_texto(mem, id_f, frase, sizeof(frase)) < 0) return;
    if (jmn_obtener_texto(mem, id_p, patron, sizeof(patron)) < 0) return;
    char* pos = strstr(frase, patron);
    if (pos) {
        pos += strlen(patron);
        jmn_guardar_texto(mem, id_d, pos);
    } else {
        jmn_guardar_texto(mem, id_d, "");
    }
}

void jmn_concatenar_texto(JMNMemoria* mem, uint32_t id_izq, uint32_t id_der, uint32_t id_dest) {
    char a[512], b[512];
    if (jmn_obtener_texto(mem, id_izq, a, sizeof(a)) < 0) a[0] = '\0';
    if (jmn_obtener_texto(mem, id_der, b, sizeof(b)) < 0) b[0] = '\0';
    strncat(a, b, sizeof(a) - strlen(a) - 1);
    jmn_guardar_texto(mem, id_dest, a);
}

uint32_t jmn_concatenar_dinamico(JMNMemoria* mem, uint32_t id_izq, uint32_t id_der) {
    char a[512], b[512];
    if (jmn_obtener_texto(mem, id_izq, a, sizeof(a)) < 0) a[0] = '\0';
    if (jmn_obtener_texto(mem, id_der, b, sizeof(b)) < 0) b[0] = '\0';
    strncat(a, b, sizeof(a) - strlen(a) - 1);
    uint32_t id = jmn_hash_str(a);
    jmn_guardar_texto(mem, id, a);
    return id;
}

uint32_t jmn_registrar_texto_dinamico(JMNMemoria* mem, const char* texto) {
    uint32_t id = jmn_hash_str(texto);
    jmn_guardar_texto(mem, id, texto);
    return id;
}

int jmn_imprimir_texto(JMNMemoria* mem, uint32_t id) {
    char buf[1024];
    if (jmn_obtener_texto(mem, id, buf, sizeof(buf)) >= 0) {
        printf("%s", buf);
        return 1;
    }
    return 0;
}

int jmn_leer_archivo(JMNMemoria* mem, const char* ruta, uint32_t id_destino) {
    FILE* f = fopen(ruta, "r");
    if (!f) return -1;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    jmn_guardar_texto(mem, id_destino, buf);
    return 0;
}

int jmn_escribir_archivo(JMNMemoria* mem, const char* ruta, uint32_t id_origen) {
    char buf[4096];
    if (jmn_obtener_texto(mem, id_origen, buf, sizeof(buf)) < 0) return -1;
    FILE* f = fopen(ruta, "w");
    if (!f) return -1;
    fputs(buf, f);
    fclose(f);
    return 0;
}
