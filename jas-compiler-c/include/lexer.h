/* Lexer para compilador Jasboot en C - Paridad con lexer.py */

#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdint.h>

/* 1.1 Tipos de token */
typedef enum {
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    TOK_STRING,
    TOK_CONCEPT,
    TOK_NUMBER,
    TOK_OPERATOR,
    TOK_EOF,
    TOK_NEWLINE
} TokenType;

/* Token con metadatos (1.8 línea y columna) */
typedef struct {
    TokenType type;
    union {
        char *str;      /* STRING, CONCEPT, IDENTIFIER, KEYWORD */
        int64_t i;      /* NUMBER entero */
        double f;       /* NUMBER flotante */
    } value;
    int is_float;       /* 1 si value.f es válido */
    int line;
    int column;
} Token;

/* Estado del lexer */
typedef struct {
    const char *source;
    size_t pos;
    size_t length;
    int line;
    int column;
    char *last_error;
    int err_line;   /* Para diagnosticos con fragmento de fuente; -1 si no aplica */
    int err_column;
} Lexer;

/* Crear/destruir */
void lexer_init(Lexer *lex, const char *source);
void lexer_free(Lexer *lex);
void token_free(Token *tok);

/* Obtener siguiente token. Retorna 0 en éxito, -1 en error (last_error puesto) */
int lexer_next(Lexer *lex, Token *out);

/* Liberar string interno de un token (cuando value.str fue asignado) */
void token_free_value(Token *tok);

#endif /* LEXER_H */
