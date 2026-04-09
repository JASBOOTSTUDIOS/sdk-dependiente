#ifndef CODEGEN_IR_H
#define CODEGEN_IR_H

#include "ir_format.h"
#include <stdbool.h>

// Forward declarations
typedef struct ASTNode ASTNode;
typedef struct TablaSimbolos TablaSimbolos;

// Contexto para generación de IR
typedef struct {
    IRFile* ir_file;
    TablaSimbolos* simbolos;
    int siguiente_registro;  // DEPRECATED: Usar registers_in_use
    bool registers_in_use[256]; // Mapa de registros en uso
    int label_counter;       // Contador para etiquetas
    void* vars;              // Mapa interno de variables (uso interno)
    uint16_t next_var_addr;  // Próxima dirección de variable
    // Control de flujo (labels/jumps) para IR
    size_t* label_pc;        // Offset (bytes) desde inicio de sección code, o (size_t)-1 si no definido
    size_t label_cap;
    struct IRJumpPatch* patches;
    size_t patches_count;
    size_t patches_cap;
    // v0.9: Seguimiento de funciones para llamadas
    struct FuncIR* funciones;
    
    // v0.9.5: Pila de control de bucles para 'romper' / 'continuar'
    struct {
        int label_start;
        int label_end;
    } loop_stack[32]; // Soporte para 32 bucles anidados
    int loop_depth;
} CodegenIR;

typedef struct FuncIR {
    const char* nombre;
    int label_id;
    int num_params;
    const char** params;
    struct FuncIR* next;
} FuncIR;

// Crear contexto de codegen IR
CodegenIR* codegen_ir_create(TablaSimbolos* simbolos);
void codegen_ir_destroy(CodegenIR* cg);

// Generar IR desde AST
int codegen_ir_generar_programa(CodegenIR* cg, ASTNode* ast);

// Funciones internas (pueden ser usadas directamente si es necesario)
int codegen_ir_generar_nodo(CodegenIR* cg, ASTNode* nodo, int reg_destino);
int codegen_ir_generar_expresion(CodegenIR* cg, ASTNode* nodo, int reg_destino);
int codegen_ir_obtener_registro(CodegenIR* cg);
void codegen_ir_liberar_registro(CodegenIR* cg, int reg);

#endif // CODEGEN_IR_H
