/**
 * Journal append-only (.jwl) para trazabilidad mínima antes del plan de journaling completo.
 */
#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static void jmn_journal_make_path(const JMNMemoria* mem, char* out, size_t outsz) {
    if (!mem || !mem->ruta_archivo[0]) {
        if (outsz) out[0] = '\0';
        return;
    }
    snprintf(out, outsz, "%s.jwl", mem->ruta_archivo);
}

void jmn_journal_op_nodo(JMNMemoria* mem, uint32_t id, uint32_t peso_u) {
    if (!mem || mem->es_ram || !mem->ruta_archivo[0] || id == 0) return;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    FILE* f = fopen(p, "ab");
    if (!f) return;
    unsigned char op = 1;
    uint32_t t = (uint32_t)time(NULL);
    fwrite(&op, 1, 1, f);
    fwrite(&id, 4, 1, f);
    fwrite(&peso_u, 4, 1, f);
    fwrite(&t, 4, 1, f);
    fclose(f);
}

void jmn_journal_op_conex(JMNMemoria* mem, uint32_t ori, uint32_t dest, uint32_t tipo, uint32_t fuerza_u) {
    if (!mem || mem->es_ram || !mem->ruta_archivo[0] || ori == 0 || dest == 0) return;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    FILE* f = fopen(p, "ab");
    if (!f) return;
    unsigned char op = 2;
    uint32_t t = (uint32_t)time(NULL);
    fwrite(&op, 1, 1, f);
    fwrite(&ori, 4, 1, f);
    fwrite(&dest, 4, 1, f);
    fwrite(&tipo, 4, 1, f);
    fwrite(&fuerza_u, 4, 1, f);
    fwrite(&t, 4, 1, f);
    fclose(f);
}

void jmn_journal_commit(JMNMemoria* mem) {
    if (!mem || mem->es_ram || !mem->ruta_archivo[0]) return;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    FILE* f = fopen(p, "ab");
    if (!f) return;
    unsigned char op = 0xFF;
    uint32_t mark = mem->num_nodos ^ (mem->num_conexiones * 1315423911u) ^ (mem->num_textos * 2654435761u);
    uint32_t t = (uint32_t)time(NULL);
    fwrite(&op, 1, 1, f);
    fwrite(&mark, 4, 1, f);
    fwrite(&t, 4, 1, f);
    fclose(f);
}

void jmn_journal_log_size_if_any(const JMNMemoria* mem) {
    if (!getenv("JASBOOT_JWL_STAT") || !mem || !mem->ruta_archivo[0]) return;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    struct stat st;
    if (stat(p, &st) != 0 || st.st_size <= 0) return;
    fprintf(stderr, "[JMN JWL] journal existente %s (%lld bytes)\n", p, (long long)st.st_size);
}
