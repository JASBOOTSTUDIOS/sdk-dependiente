/**
 * Journal append-only (.jwl) para trazabilidad mínima antes del plan de journaling completo.
 */
#include "memoria_neuronal.h"
#include "jmn_interno.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

/** >0 mientras se aplica replay: no re-append al .jwl desde jmn_agregar_* */
static int jmn_journal_replay_depth;

static void jmn_journal_make_path(const JMNMemoria* mem, char* out, size_t outsz) {
    if (!mem || !mem->ruta_archivo[0]) {
        if (outsz) out[0] = '\0';
        return;
    }
    snprintf(out, outsz, "%s.jwl", mem->ruta_archivo);
}

void jmn_journal_op_nodo(JMNMemoria* mem, uint32_t id, uint32_t peso_u) {
    if (jmn_journal_replay_depth) return;
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

void jmn_journal_op_texto(JMNMemoria* mem, uint32_t id, const char* texto) {
    if (jmn_journal_replay_depth) return;
    if (!mem || mem->es_ram || !mem->ruta_archivo[0] || id == 0) return;
    const char* t = texto ? texto : "";
    size_t L = strlen(t);
    if (L > 255u) L = 255u;
    uint16_t len = (uint16_t)L;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    FILE* f = fopen(p, "ab");
    if (!f) return;
    unsigned char op = 3;
    uint32_t ts = (uint32_t)time(NULL);
    fwrite(&op, 1, 1, f);
    fwrite(&id, 4, 1, f);
    fwrite(&len, 2, 1, f);
    if (len) fwrite(t, 1, len, f);
    fwrite(&ts, 4, 1, f);
    fclose(f);
}

void jmn_journal_op_conex(JMNMemoria* mem, uint32_t ori, uint32_t dest, uint32_t tipo, uint32_t fuerza_u) {
    if (jmn_journal_replay_depth) return;
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
    if (jmn_journal_replay_depth) return;
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

void jmn_journal_truncate_desde_checkpoint(JMNMemoria* mem) {
    if (jmn_journal_replay_depth) return;
    if (!mem || mem->es_ram || !mem->ruta_archivo[0]) return;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    FILE* f = fopen(p, "wb");
    if (f) fclose(f);
}

void jmn_journal_log_size_if_any(const JMNMemoria* mem) {
    if (!getenv("JASBOOT_JWL_STAT") || !mem || !mem->ruta_archivo[0]) return;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    struct stat st;
    if (stat(p, &st) != 0 || st.st_size <= 0) return;
    fprintf(stderr, "[JMN JWL] journal existente %s (%lld bytes)\n", p, (long long)st.st_size);
}

/**
 * Reproduce operaciones del .jwl sobre mem (vacía o parcial).
 * No escribe al journal durante la reproducción.
 * @return 0 si el archivo no existe, está vacío o se leyó completo; -1 si hay registro truncado u op desconocida.
 */
int jmn_journal_replay(JMNMemoria* mem) {
    if (!mem || mem->es_ram || !mem->ruta_archivo[0]) return -1;
    char p[512];
    jmn_journal_make_path(mem, p, sizeof p);
    FILE* f = fopen(p, "rb");
    if (!f) {
        const char* dbg = getenv("JASBOOT_JWL_REPLAY_DBG");
        if (dbg && dbg[0] && strcmp(dbg, "0") != 0)
            fprintf(stderr, "[JMN JWL] replay: fopen fallo %s\n", p);
        return 0;
    }

    jmn_journal_replay_depth++;
    unsigned char op = 0;
    while (fread(&op, 1, 1, f) == 1) {
        if (op == 1) {
            uint32_t id, peso_u, t;
            if (fread(&id, 4, 1, f) != 1 || fread(&peso_u, 4, 1, f) != 1 || fread(&t, 4, 1, f) != 1)
                goto bad;
            JMNValor pv;
            pv.u = peso_u;
            jmn_agregar_nodo(mem, id, pv);
        } else if (op == 2) {
            uint32_t ori, dest, tipo, fuerza_u, t;
            if (fread(&ori, 4, 1, f) != 1 || fread(&dest, 4, 1, f) != 1 ||
                fread(&tipo, 4, 1, f) != 1 || fread(&fuerza_u, 4, 1, f) != 1 || fread(&t, 4, 1, f) != 1)
                goto bad;
            JMNValor fv;
            fv.u = fuerza_u;
            jmn_agregar_conexion(mem, ori, dest, fv, tipo);
        } else if (op == 3) {
            uint32_t id;
            uint16_t len;
            uint32_t t;
            if (fread(&id, 4, 1, f) != 1 || fread(&len, 2, 1, f) != 1) goto bad;
            if (len > 255) goto bad;
            char buf[256];
            if (len > 0) {
                if (fread(buf, 1, len, f) != len) goto bad;
            }
            buf[len] = '\0';
            if (fread(&t, 4, 1, f) != 1) goto bad;
            jmn_guardar_texto(mem, id, buf);
        } else if (op == 0xFFu) {
            uint32_t mark, t;
            if (fread(&mark, 4, 1, f) != 1 || fread(&t, 4, 1, f) != 1) goto bad;
        } else {
            goto bad;
        }
    }
    if (ferror(f)) goto bad;
    jmn_journal_replay_depth--;
    fclose(f);
    {
        const char* dbg = getenv("JASBOOT_JWL_REPLAY_DBG");
        if (dbg && dbg[0] && strcmp(dbg, "0") != 0) {
            fprintf(stderr, "[JMN JWL] replay fin nodos=%u conex=%u textos=%u\n",
                    (unsigned)mem->num_nodos, (unsigned)mem->num_conexiones,
                    (unsigned)mem->num_textos);
        }
    }
    return 0;
bad:
    {
        const char* dbg = getenv("JASBOOT_JWL_REPLAY_DBG");
        if (dbg && dbg[0] && strcmp(dbg, "0") != 0) {
            long pos = ftell(f);
            fprintf(stderr, "[JMN JWL] replay error op=%u pos=%ld errno=%d\n",
                    (unsigned)op, pos, errno);
        }
    }
    jmn_journal_replay_depth--;
    fclose(f);
    return -1;
}
