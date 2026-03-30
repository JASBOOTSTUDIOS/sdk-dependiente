/* 10.1 Linter - Analisis estatico para .jasb */
#include "lexer.h"
#include "token_vec.h"
#include "keywords.h"
#include "parser.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FORBIDDEN[] = {
    "if", "else", "for", "while", "function", "return", "endif", "endwhile", "endfor",
    "true", "false", "null", "var", "let", "const", "class", "import", "export",
    "print", "def", "int", "string", "float", "bool", "list", "dict", "break", "continue"
};
static const size_t FORBIDDEN_N = sizeof(FORBIDDEN) / sizeof(FORBIDDEN[0]);

static int linter_is_forbidden(const char *word) {
    for (size_t i = 0; i < FORBIDDEN_N; i++) {
        if (strcmp(word, FORBIDDEN[i]) == 0) return 1;
    }
    return 0;
}

typedef struct { int line; int col; char msg[256]; } LintErr;

static int lint_english(const char *src, LintErr *out, int max_out, int *n_out) {
    Lexer lex;
    lexer_init(&lex, src);
    TokenVec vec;
    token_vec_init(&vec);
    Token t;
    *n_out = 0;
    while (lexer_next(&lex, &t) && *n_out < max_out) {
        if (t.type == TOK_IDENTIFIER && t.value.str) {
            if (linter_is_forbidden(t.value.str)) {
                snprintf(out[*n_out].msg, sizeof(out[*n_out].msg),
                    "Palabra en ingles no permitida: '%s'. Use equivalente en espanol.", t.value.str);
                out[*n_out].line = t.line;
                out[*n_out].col = t.column;
                (*n_out)++;
            }
        }
    }
    token_vec_free(&vec);
    return *n_out;
}

extern int do_compile(const char *in_path, const char *out_path, char **err_msg);

int do_lint(const char *path, int quiet) {
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

    LintErr errs[64];
    int n = 0;
    int has_err = 0;

    lint_english(buf, errs, 64, &n);
    for (int i = 0; i < n; i++) {
        if (!quiet) fprintf(stderr, "%s:%d:%d: %s\n", path, errs[i].line, errs[i].col, errs[i].msg);
        has_err = 1;
    }

    char jbo_tmp[512];
    snprintf(jbo_tmp, sizeof(jbo_tmp), "%s.jbo.lint", path);
    if (do_compile(path, jbo_tmp, NULL) != 0) {
        if (!quiet) fprintf(stderr, "%s: Error de compilacion (sintaxis)\n", path);
        has_err = 1;
    }
    remove(jbo_tmp);

    free(buf);
    if (!quiet && !has_err) printf("[jbc] OK Revisado: %s (sin errores)\n", path);
    return has_err ? 1 : 0;
}
