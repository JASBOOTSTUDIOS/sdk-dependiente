#include "token_vec.h"
#include <stdlib.h>
#include <string.h>

void token_vec_init(TokenVec *v) {
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

void token_vec_free(TokenVec *v) {
    for (size_t i = 0; i < v->size; i++)
        token_free_value(&v->data[i]);
    free(v->data);
    v->data = NULL;
    v->size = v->capacity = 0;
}

static Token *copy_token(const Token *tok) {
    Token *t = malloc(sizeof(Token));
    if (!t) return NULL;
    memcpy(t, tok, sizeof(Token));
    if (tok->type == TOK_STRING || tok->type == TOK_CONCEPT ||
        tok->type == TOK_IDENTIFIER || tok->type == TOK_KEYWORD ||
        tok->type == TOK_OPERATOR) {
        if (tok->value.str)
            t->value.str = strdup(tok->value.str);
        else
            t->value.str = NULL;
    }
    /* TOK_NUMBER, TOK_EOF: no modificar la union - memcpy ya copió value.i/f.
     * Asignar a value.str sobrescribiría los datos numéricos. */
    return t;
}

int token_vec_push(TokenVec *v, const Token *tok) {
    if (v->size >= v->capacity) {
        size_t new_cap = v->capacity ? v->capacity * 2 : 64;
        Token *p = realloc(v->data, new_cap * sizeof(Token));
        if (!p) return -1;
        v->data = p;
        v->capacity = new_cap;
    }
    Token *cpy = copy_token(tok);
    if (!cpy) return -1;
    v->data[v->size++] = *cpy;
    free(cpy);
    return 0;
}

const Token *token_vec_get(const TokenVec *v, size_t i) {
    if (!v || !v->data || i >= v->size) return NULL;
    return &v->data[i];
}

size_t token_vec_size(const TokenVec *v) {
    return v->size;
}
