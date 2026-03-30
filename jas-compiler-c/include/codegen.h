/* CodeGen - emisión de IR .jbo */

#ifndef CODEGEN_H
#define CODEGEN_H

#include "nodes.h"
#include <stddef.h>
#include <stdint.h>

typedef struct CodeGen CodeGen;

CodeGen *codegen_create(void);
void codegen_free(CodeGen *cg);

/* Registrar firma de función externa (de módulos usar) para inferencia de tipos */
void codegen_register_external_func(CodeGen *cg, const char *name, const char *return_type);

const char *codegen_get_error(CodeGen *cg, int *out_line, int *out_col);

/* Genera binario .jbo; retorna buffer (ownership al caller, free) y longitud */
uint8_t *codegen_generate(CodeGen *cg, ASTNode *ast, size_t *out_len);

#endif
