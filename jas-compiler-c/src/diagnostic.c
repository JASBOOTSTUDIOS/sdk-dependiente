#include "diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIAG_MAX_LINE_SHOW 2048

char *diag_attach_snippet(const char *source, int line1, int col1, const char *headline) {
    if (!headline) return NULL;
    if (!source || line1 < 1 || col1 < 1)
        return strdup(headline);

    const char *p = source;
    int line = 1;
    while (*p && line < line1) {
        if (*p == '\n')
            line++;
        p++;
    }
    if (!*p && line < line1) {
        size_t h = strlen(headline);
        char *r = malloc(h + 96);
        if (!r) return strdup(headline);
        snprintf(r, h + 96, "%s\n (linea %d: fin de archivo antes de esa linea)\n", headline, line1);
        return r;
    }

    const char *sol = p;
    const char *eol = sol;
    while (*eol && *eol != '\n' && *eol != '\r')
        eol++;
    size_t raw_len = (size_t)(eol - sol);
    size_t show = raw_len;
    int truncated = 0;
    if (show > DIAG_MAX_LINE_SHOW) {
        show = DIAG_MAX_LINE_SHOW;
        truncated = 1;
    }

    int caret = col1 - 1;
    if (caret < 0) caret = 0;
    if (caret > (int)show) caret = (int)show;

    size_t hl = strlen(headline);
    size_t cap = hl + show + (size_t)caret + 128;
    char *out = malloc(cap);
    if (!out) return strdup(headline);

    int n = snprintf(out, cap, "%s\n%.*s", headline, (int)show, sol);
    if (n < 0) n = 0;
    if (truncated && n + 48 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, " ... (%zu caracteres en total)", raw_len);
    if (n + 8 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, "\n");
    for (int i = 0; i < caret && n + 2 < (int)cap; i++)
        out[n++] = ' ';
    if (n + 3 < (int)cap) {
        out[n++] = '^';
        out[n++] = '\n';
        out[n] = '\0';
    }
    return out;
}

static const char *diag_line_start(const char *source, int target_line) {
    const char *p = source;
    int line = 1;
    while (*p && line < target_line) {
        if (*p == '\n')
            line++;
        p++;
    }
    return p;
}

static int diag_line_slice(const char *source, int target_line, const char **sol_out, size_t *len_out) {
    const char *sol = diag_line_start(source, target_line);
    if (!sol || target_line < 1) {
        *sol_out = NULL;
        *len_out = 0;
        return 0;
    }
    const char *eol = sol;
    while (*eol && *eol != '\n' && *eol != '\r')
        eol++;
    *sol_out = sol;
    *len_out = (size_t)(eol - sol);
    return 1;
}

char *diag_attach_snippet_two(const char *source, int line1, int col1, int line2, int col2, const char *headline) {
    if (!headline)
        return NULL;
    if (!source || line1 < 1 || col1 < 1 || line2 < 1 || col2 < 1)
        return strdup(headline);

    size_t hl = strlen(headline);

    if (line1 == line2) {
        const char *sol;
        size_t raw_len;
        if (!diag_line_slice(source, line1, &sol, &raw_len))
            return strdup(headline);

        size_t show = raw_len;
        int truncated = 0;
        if (show > DIAG_MAX_LINE_SHOW) {
            show = DIAG_MAX_LINE_SHOW;
            truncated = 1;
        }

        size_t i1 = (size_t)(col1 - 1);
        size_t i2 = (size_t)(col2 - 1);
        if (i1 > show)
            i1 = show;
        if (i2 > show)
            i2 = show;

        size_t mark_width = show;
        if (i1 >= mark_width)
            mark_width = i1 + 1;
        if (i2 >= mark_width)
            mark_width = i2 + 1;

        char *marks = (char *)calloc(mark_width + 2, 1);
        if (!marks)
            return strdup(headline);
        memset(marks, ' ', mark_width);
        if (i1 == i2)
            marks[i1] = '^';
        else {
            marks[i1] = '^';
            marks[i2] = '^';
        }

        size_t cap = hl + show + mark_width + 160;
        char *out = malloc(cap);
        if (!out) {
            free(marks);
            return strdup(headline);
        }
        int n = snprintf(out, cap, "%s\n%.*s", headline, (int)show, sol);
        if (n < 0)
            n = 0;
        if (truncated && n + 48 < (int)cap)
            n += snprintf(out + n, cap - (size_t)n, " ... (%zu caracteres en total)", raw_len);
        if (n + 2 < (int)cap)
            n += snprintf(out + n, cap - (size_t)n, "\n%.*s\n", (int)mark_width, marks);
        free(marks);
        return out;
    }

    const char *sol1, *sol2;
    size_t len1, len2;
    if (!diag_line_slice(source, line1, &sol1, &len1) || !diag_line_slice(source, line2, &sol2, &len2))
        return strdup(headline);

    size_t show1 = len1 > DIAG_MAX_LINE_SHOW ? DIAG_MAX_LINE_SHOW : len1;
    size_t show2 = len2 > DIAG_MAX_LINE_SHOW ? DIAG_MAX_LINE_SHOW : len2;
    int t1 = len1 > DIAG_MAX_LINE_SHOW;
    int t2 = len2 > DIAG_MAX_LINE_SHOW;

    size_t i1 = (size_t)(col1 - 1);
    size_t i2 = (size_t)(col2 - 1);
    if (i1 > show1)
        i1 = show1;
    if (i2 > show2)
        i2 = show2;
    size_t mw1 = show1;
    if (i1 >= mw1)
        mw1 = i1 + 1;
    size_t mw2 = show2;
    if (i2 >= mw2)
        mw2 = i2 + 1;

    char *m1 = (char *)calloc(mw1 + 2, 1);
    char *m2 = (char *)calloc(mw2 + 2, 1);
    if (!m1 || !m2) {
        free(m1);
        free(m2);
        return strdup(headline);
    }
    memset(m1, ' ', mw1);
    memset(m2, ' ', mw2);
    m1[i1] = '^';
    m2[i2] = '^';

    size_t cap = hl + show1 + show2 + mw1 + mw2 + 256;
    char *out = malloc(cap);
    if (!out) {
        free(m1);
        free(m2);
        return strdup(headline);
    }
    int n = snprintf(out, cap, "%s\n%.*s", headline, (int)show1, sol1);
    if (n < 0)
        n = 0;
    if (t1 && n + 48 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, " ... (%zu caracteres en total)", len1);
    if (n + 2 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, "\n%.*s\n", (int)mw1, m1);
    if (n + 2 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, "%.*s", (int)show2, sol2);
    if (t2 && n + 48 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, " ... (%zu caracteres en total)", len2);
    if (n + 2 < (int)cap)
        n += snprintf(out + n, cap - (size_t)n, "\n%.*s\n", (int)mw2, m2);
    free(m1);
    free(m2);
    return out;
}
