/* Pase de resolución: popula la tabla de símbolos desde el AST */

#ifndef RESOLVE_H
#define RESOLVE_H

#include "nodes.h"
#include "symbol_table.h"

/* Recorre el AST y declara variables, structs, etc. en st. Devuelve cantidad de errores (0 = ok). */
int resolve_program(ASTNode *ast, SymbolTable *st);

#endif
