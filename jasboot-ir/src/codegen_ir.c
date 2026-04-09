#define _POSIX_C_SOURCE 200809L
#include "codegen_ir.h"
#include "ast.h"
#include "simbolos.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Mock de codegen_ir.c con firmas exactas de codegen_ir.h

CodegenIR* codegen_ir_create(TablaSimbolos* simbolos) {
    (void)simbolos;
    CodegenIR* cg = calloc(1, sizeof(CodegenIR));
    return cg;
}

void codegen_ir_destroy(CodegenIR* cg) {
    if (cg) free(cg);
}

int codegen_ir_generar_programa(CodegenIR* cg, ASTNode* programa) {
    (void)cg; (void)programa;
    fprintf(stderr, "ADVERTENCIA: El backend IR está deshabilitado en esta build.\n");
    return 0;
}

int codegen_ir_generar_nodo(CodegenIR* cg, ASTNode* nodo, int reg_destino) {
    (void)cg; (void)nodo; (void)reg_destino;
    return 0;
}

int codegen_ir_generar_expresion(CodegenIR* cg, ASTNode* nodo, int reg_destino) {
    (void)cg; (void)nodo; (void)reg_destino;
    return 0;
}
