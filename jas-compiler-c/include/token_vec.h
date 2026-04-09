/* Vector de tokens para el parser */

#ifndef TOKEN_VEC_H
#define TOKEN_VEC_H

#include "lexer.h"

typedef struct {
    Token *data;
    size_t size;
    size_t capacity;
} TokenVec;

void token_vec_init(TokenVec *v);
void token_vec_free(TokenVec *v);
int token_vec_push(TokenVec *v, const Token *tok);
const Token *token_vec_get(const TokenVec *v, size_t i);
size_t token_vec_size(const TokenVec *v);

#endif
