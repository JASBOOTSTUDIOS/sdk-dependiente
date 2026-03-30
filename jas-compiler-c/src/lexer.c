/* Lexer para compilador Jasboot en C - Paridad con lexer.py */

#include "lexer.h"  /* desde include/ via -Iinclude */
#include "keywords.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define BUF_INIT 64
#define ERROR_MAX 256

static void set_error_at(Lexer *lex, int el, int ec, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (lex->last_error) free(lex->last_error);
    lex->last_error = malloc(ERROR_MAX);
    if (lex->last_error)
        vsnprintf(lex->last_error, ERROR_MAX, fmt, ap);
    va_end(ap);
    lex->err_line = el;
    lex->err_column = ec;
}

static void set_error(Lexer *lex, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (lex->last_error) free(lex->last_error);
    lex->last_error = malloc(ERROR_MAX);
    if (lex->last_error)
        vsnprintf(lex->last_error, ERROR_MAX, fmt, ap);
    va_end(ap);
    lex->err_line = lex->line;
    lex->err_column = lex->column;
}

void lexer_init(Lexer *lex, const char *source) {
    lex->source = source;
    lex->pos = 0;
    lex->length = source ? strlen(source) : 0;
    lex->line = 1;
    lex->column = 1;
    lex->last_error = NULL;
    lex->err_line = -1;
    lex->err_column = -1;
}

void lexer_free(Lexer *lex) {
    if (lex->last_error) {
        free(lex->last_error);
        lex->last_error = NULL;
    }
}

void token_free(Token *tok) {
    token_free_value(tok);
}

void token_free_value(Token *tok) {
    if (tok->type == TOK_STRING || tok->type == TOK_CONCEPT ||
        tok->type == TOK_IDENTIFIER || tok->type == TOK_KEYWORD) {
        if (tok->value.str) {
            free(tok->value.str);
            tok->value.str = NULL;
        }
    }
}

static char peek(const Lexer *lex) {
    if (lex->pos < lex->length)
        return lex->source[lex->pos];
    return '\0';
}

static char advance(Lexer *lex) {
    if (lex->pos >= lex->length) return '\0';
    char ch = lex->source[lex->pos++];
    if (ch == '\n') {
        lex->line++;
        lex->column = 1;
    } else {
        lex->column++;
    }
    return ch;
}

static void skip_whitespace(Lexer *lex) {
    while (lex->pos < lex->length) {
        char ch = lex->source[lex->pos];
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            advance(lex);
        } else if (ch == '/' && lex->pos + 1 < lex->length && lex->source[lex->pos + 1] == '*') {
            /* Comentario multilinea */
            advance(lex);
            advance(lex);
            while (lex->pos + 1 < lex->length) {
                if (lex->source[lex->pos] == '*' && lex->source[lex->pos + 1] == '/') {
                    advance(lex);
                    advance(lex);
                    break;
                }
                advance(lex);
            }
        } else if (ch == '#') {
            while (lex->pos < lex->length && lex->source[lex->pos] != '\n')
                advance(lex);
        } else if (ch == '/' && lex->pos + 1 < lex->length && lex->source[lex->pos + 1] == '/') {
            advance(lex);
            advance(lex);
            while (lex->pos < lex->length && lex->source[lex->pos] != '\n')
                advance(lex);
        } else {
            break;
        }
    }
}

/* Colores: 4 letras o nombre completo. Nombres largos primero para "normal" antes de "norm". */
static const struct { const char *nombre; const char *ansi; } LEXER_COLORES[] = {
    {"normal",  "\033[0m"}, {"negrita", "\033[1m"}, {"amarillo","\033[33m"},
    {"magenta", "\033[35m"}, {"blanco",  "\033[37m"}, {"verde",   "\033[32m"},
    {"reset",   "\033[0m"},
    {"rojo", "\033[31m"}, {"verd", "\033[32m"}, {"amar", "\033[33m"},
    {"azul", "\033[34m"}, {"mage", "\033[35m"}, {"cian", "\033[36m"},
    {"blan", "\033[37m"}, {"negr", "\033[1m"},  {"norm", "\033[0m"},
    {"rese", "\033[0m"},  {NULL, NULL}
};

/* Intenta leer nombre de color tras \; retorna 1 si encontró, 0 si no. No avanza si no coincide. */
static int try_consumir_color(Lexer *lex, char **buf, size_t *len, size_t *cap) {
    for (int i = 0; LEXER_COLORES[i].nombre; i++) {
        const char *nom = LEXER_COLORES[i].nombre;
        size_t j = 0;
        while (nom[j] && lex->pos + j < lex->length && lex->source[lex->pos + j] == nom[j]) j++;
        if (nom[j] == '\0') {
            for (size_t k = 0; k < j; k++) advance(lex);
            const char *a = LEXER_COLORES[i].ansi;
            while (*a) {
                if (*len >= *cap) { *cap *= 2; *buf = realloc(*buf, *cap); }
                (*buf)[(*len)++] = *a++;
            }
            return 1;
        }
    }
    return 0;
}

/* 1.7 Strings con escape \n \t \" \\ \rojo \verd \amar \azul etc. */
static int read_string(Lexer *lex, Token *out) {
    int start_col = lex->column;
    advance(lex); /* consumir " */
    size_t cap = BUF_INIT;
    char *buf = malloc(cap);
    if (!buf) { set_error_at(lex, -1, -1, "Sin memoria"); return -1; }
    size_t len = 0;

    while (lex->pos < lex->length) {
        char ch = peek(lex);
        if (ch == '"') break;

        if (ch == '$' && lex->pos + 1 < lex->length && lex->source[lex->pos + 1] == '{') {
            /* Interpolación ${...} - copiar literal por ahora */
            while (lex->pos < lex->length) {
                char c = advance(lex);
                if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = c;
                if (c == '}' && len >= 2 && buf[len-2] != '\\') break;
            }
            continue;
        }

        advance(lex);
        if (ch == '\\') {
            if (try_consumir_color(lex, &buf, &len, &cap)) continue;
            char next = peek(lex);
            if (lex->pos < lex->length) advance(lex);
            if (next == 'n') ch = '\n';
            else if (next == 't') ch = '\t';
            else if (next == 'r') ch = '\r';
            else if (next == '"') ch = '"';
            else if (next == '\\') ch = '\\';
            else if (next == 'e' || next == 'E') ch = '\033';
            else { if (len >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = '\\'; ch = next; }
        }
        if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = ch;
    }

    if (lex->pos >= lex->length) {
        set_error_at(lex, lex->line, start_col, "Error lexico: cadena de texto no cerrada con '\"' antes del fin de archivo");
        free(buf);
        return -1;
    }
    advance(lex); /* consumir " */

    buf = realloc(buf, len + 1);
    buf[len] = '\0';
    out->type = TOK_STRING;
    out->value.str = buf;
    out->is_float = 0;
    out->line = lex->line;
    out->column = start_col;
    return 0;
}

/* Conceptos (comillas simples) */
static int read_concept(Lexer *lex, Token *out) {
    int start_col = lex->column;
    advance(lex);
    size_t cap = BUF_INIT;
    char *buf = malloc(cap);
    if (!buf) { set_error_at(lex, -1, -1, "Sin memoria"); return -1; }
    size_t len = 0;

    while (lex->pos < lex->length && peek(lex) != '\'') {
        if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }
    if (lex->pos >= lex->length) {
        set_error_at(lex, lex->line, start_col, "Error lexico: cadena de concepto no cerrada con '\\'' antes del fin de archivo");
        free(buf);
        return -1;
    }
    advance(lex);

    buf = realloc(buf, len + 1);
    buf[len] = '\0';
    out->type = TOK_CONCEPT;
    out->value.str = buf;
    out->is_float = 0;
    out->line = lex->line;
    out->column = start_col;
    return 0;
}

/* 1.4, 1.5, 1.9, 1.10 Identificadores y keywords */
static int read_identifier(Lexer *lex, Token *out) {
    int start_col = lex->column;
    size_t cap = BUF_INIT;
    char *buf = malloc(cap);
    if (!buf) { set_error_at(lex, -1, -1, "Sin memoria"); return -1; }
    size_t len = 0;

    while (lex->pos < lex->length && (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')) {
        if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = advance(lex);
    }

    buf = realloc(buf, len + 1);
    buf[len] = '\0';

    if (is_forbidden(buf, len)) {
        set_error_at(lex, lex->line, start_col,
                  "'%s' no permitido (paradigma 100%% espanol). Use: cuando/si, mientras, funcion, retornar, verdadero/falso.",
                  buf);
        free(buf);
        return -1;
    }

    if (strcmp(buf, "verdadero") == 0) {
        free(buf);
        out->type = TOK_NUMBER;
        out->value.i = 1;
        out->is_float = 0;
        out->line = lex->line;
        out->column = start_col;
        return 0;
    }
    if (strcmp(buf, "falso") == 0) {
        free(buf);
        out->type = TOK_NUMBER;
        out->value.i = 0;
        out->is_float = 0;
        out->line = lex->line;
        out->column = start_col;
        return 0;
    }

    if (is_keyword(buf, len)) {
        out->type = TOK_KEYWORD;
        out->value.str = buf;
    } else {
        out->type = TOK_IDENTIFIER;
        out->value.str = buf;
    }
    out->is_float = 0;
    out->line = lex->line;
    out->column = start_col;
    return 0;
}

/* Números enteros y flotantes (incl. hex 0x...) */
static int read_number(Lexer *lex, Token *out) {
    int start_col = lex->column;
    size_t cap = 32;
    char *buf = malloc(cap);
    if (!buf) { set_error_at(lex, -1, -1, "Sin memoria"); return -1; }
    size_t len = 0;
    int is_float = 0;
    int is_hex = 0;

    if (peek(lex) == '0' && lex->pos + 1 < lex->length) {
        char nx = lex->source[lex->pos + 1];
        if (nx == 'x' || nx == 'X') {
            is_hex = 1;
            advance(lex);
            advance(lex);
            while (lex->pos < lex->length) {
                char ch = peek(lex);
                if (isxdigit((unsigned char)ch)) {
                    if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
                    buf[len++] = advance(lex);
                } else
                    break;
            }
        }
    }

    if (!is_hex) {
        while (lex->pos < lex->length) {
            char ch = peek(lex);
            if (isdigit((unsigned char)ch)) {
                if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = advance(lex);
            } else if (ch == '.' && !is_float) {
                is_float = 1;
                if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = advance(lex);
            } else
                break;
        }
    }

    buf = realloc(buf, len + 1);
    buf[len] = '\0';

    out->line = lex->line;
    out->column = start_col;
    out->type = TOK_NUMBER;
    out->value.str = NULL;

    if (is_float && !is_hex) {
        out->is_float = 1;
        out->value.f = strtod(buf, NULL);
    } else if (is_hex) {
        out->is_float = 0;
        out->value.i = (int64_t)(uint64_t)strtoull(buf, NULL, 16);
    } else {
        out->is_float = 0;
        out->value.i = (int64_t)strtoll(buf, NULL, 10);
    }
    free(buf);
    return 0;
}

int lexer_next(Lexer *lex, Token *out) {
    memset(out, 0, sizeof(*out));

    while (1) {
        skip_whitespace(lex);

        if (lex->pos >= lex->length) {
            out->type = TOK_EOF;
            out->line = lex->line;
            out->column = lex->column;
            return 0;
        }

        char ch = peek(lex);

        if (ch == '\n') {
            advance(lex);
            continue;
        }

        if (ch == '"') return read_string(lex, out);
        if (ch == '\'') return read_concept(lex, out);

        if (isalpha((unsigned char)ch) || ch == '_')
            return read_identifier(lex, out);

        if (isdigit((unsigned char)ch))
            return read_number(lex, out);

        /* 1.6 Operadores */
        if (lex->pos + 1 < lex->length) {
            char two[3] = {0};
            if (is_operator_double(lex->source + lex->pos, 2, two)) {
                int col = lex->column;
                advance(lex);
                advance(lex);
                out->type = TOK_OPERATOR;
                out->value.str = strdup(two);
                out->is_float = 0;
                out->line = lex->line;
                out->column = col;
                return 0;
            }
        }
        if (is_operator_single(ch)) {
            int col = lex->column;
            advance(lex);
            out->type = TOK_OPERATOR;
            out->value.str = malloc(2);
            if (out->value.str) { out->value.str[0] = ch; out->value.str[1] = '\0'; }
            out->is_float = 0;
            out->line = lex->line;
            out->column = col;
            return 0;
        }

        set_error(lex, "Caracter no reconocido '%.1s'", &ch);
        return -1;
    }
}
