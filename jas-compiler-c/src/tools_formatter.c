/* 10.2 Formatter - Formateo de codigo .jasb */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDENT "    "

static int block_increase(const char *word) {
    return strcmp(word, "principal") == 0 || strcmp(word, "funcion") == 0 ||
           strcmp(word, "cuando") == 0 || strcmp(word, "si") == 0 ||
           strcmp(word, "mientras") == 0 || strcmp(word, "sino") == 0 ||
           strcmp(word, "registro") == 0 || strcmp(word, "hacer") == 0;
}

static int block_decrease(const char *word) {
    return strcmp(word, "fin_principal") == 0 || strcmp(word, "fin_funcion") == 0 ||
           strcmp(word, "fin_cuando") == 0 || strcmp(word, "fin_si") == 0 ||
           strcmp(word, "fin_mientras") == 0 || strcmp(word, "fin_registro") == 0;
}

static void strip_comment(const char *line, char *content, char *comment) {
    int in_s = 0;
    int j = 0, k = 0;
    for (int i = 0; line[i]; i++) {
        if (in_s) {
            if (line[i] == '\\') { if (content) content[j++] = line[i]; i++; if (line[i]) { if (content) content[j++] = line[i]; } continue; }
            if (line[i] == in_s) { in_s = 0; if (content) content[j++] = line[i]; continue; }
            if (content) content[j++] = line[i];
            continue;
        }
        if (line[i] == '"' || line[i] == '\'') { in_s = line[i]; if (content) content[j++] = line[i]; continue; }
        if (line[i] == '#' && line[i-1] != '\\') {
            while (line[i] && k < 255) { if (comment) comment[k++] = line[i]; i++; }
            if (comment) comment[k] = '\0';
            break;
        }
        if (content) content[j++] = line[i];
    }
    if (content) content[j] = '\0';
}

static void first_word(const char *s, char *out, size_t out_sz) {
    while (*s && (*s == ' ' || *s == '\t')) s++;
    size_t i = 0;
    while (*s && *s != ' ' && *s != '\t' && i < out_sz - 1) { out[i++] = *s++; }
    out[i] = '\0';
}

char *format_source(const char *source) {
    size_t cap = strlen(source) + 4096;
    char *result = malloc(cap);
    if (!result) return NULL;
    result[0] = '\0';
    size_t len = 0;

    int stack[64];
    int sp = 0;
    stack[0] = 0;

    const char *p = source;
    char line[2048];
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        size_t ln = (size_t)(p - start);
        if (ln >= sizeof(line) - 1) ln = sizeof(line) - 2;
        memcpy(line, start, ln);
        line[ln] = '\0';
        if (*p == '\n') p++;

        char content[2048], comment[256];
        strip_comment(line, content, comment);
        size_t c_len = strlen(content);
        while (c_len && (content[c_len-1] == ' ' || content[c_len-1] == '\t')) content[--c_len] = '\0';

        char fw[64];
        first_word(content, fw, sizeof(fw));
        if (!fw[0]) {
            if (len + 2 < cap) { strcpy(result + len, "\n"); len++; }
            continue;
        }

        if (block_decrease(fw) && sp > 0) sp--;

        int level = sp - 1;
        if (level < 0) level = 0;

        for (int i = 0; i < level && len + 5 < cap; i++) { strcpy(result + len, INDENT); len += 4; }
        if (len + strlen(content) + 64 < cap) {
            strcpy(result + len, content);
            len += strlen(content);
            if (comment[0]) {
                strcat(result + len, "  ");
                len += 2;
                strcat(result + len, comment);
                len += strlen(comment);
            }
            strcat(result + len, "\n");
            len += 1;
        }

        if (block_increase(fw) && strcmp(fw, "sino") != 0 && sp < 63) sp++;
    }

    if (source[0] && source[strlen(source)-1] != '\n' && len > 0 && result[len-1] == '\n') { }
    return result;
}

int do_format(const char *path, int write_file, int quiet) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (!quiet) fprintf(stderr, "Error: no se puede abrir '%s'\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 1; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    char *out = format_source(buf);
    free(buf);
    if (!out) return 1;

    if (write_file) {
        FILE *wf = fopen(path, "wb");
        if (!wf) { free(out); return 1; }
        fputs(out, wf);
        fclose(wf);
        if (!quiet) printf("[jbc] Formateado: %s\n", path);
    } else {
        printf("%s", out);
    }
    free(out);
    return 0;
}
