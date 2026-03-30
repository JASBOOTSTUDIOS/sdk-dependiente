/* Keywords y operadores - sincro con lexer.py */

#ifndef KEYWORDS_H
#define KEYWORDS_H

#include <stddef.h>

/* 1.9 Palabras en inglés no permitidas (paradigma 100% español) */
extern const char *const FORBIDDEN_ENGLISH[];
extern const size_t FORBIDDEN_ENGLISH_COUNT;

/* 1.10 Todas las keywords (~90) */
extern const char *const KEYWORDS[];
extern const size_t KEYWORDS_COUNT;

/* 1.6 Operadores */
extern const char *const OPERATORS_SINGLE;
int is_keyword(const char *str, size_t len);
int is_forbidden(const char *str, size_t len);
int is_operator_single(char c);
int is_operator_double(const char *s, size_t len, char *out_two);

#endif
