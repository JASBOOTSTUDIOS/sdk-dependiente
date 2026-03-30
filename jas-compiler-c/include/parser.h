/* Parser - construye AST desde tokens */

#ifndef PARSER_H
#define PARSER_H

#include "nodes.h"
#include "token_vec.h"

typedef struct {
    const TokenVec *tokens;
    size_t pos;
    char *last_error;
    char *accumulated_errors; /* Errores acumulados en toda la pasada */
    int error_count;
    const char *source_path;  /* opcional: ruta del .jasb para mensajes (puede ser NULL) */
    const char *source_text; /* fuente completa para mostrar linea en errores (puede ser NULL) */
} Parser;

void parser_init(Parser *p, const TokenVec *tokens, const char *source_path, const char *source_text);
void parser_free(Parser *p);

/* Retorna AST o NULL en error (last_error seteado) */
ASTNode *parser_parse(Parser *p);

/* Parsea una sola expresión desde string (para interpolación ${expr}) */
ASTNode *parser_parse_expression_from_string(const char *str, char **err_out);

#endif
