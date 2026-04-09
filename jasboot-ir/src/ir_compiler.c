#include "ir_format.h"
#include "codegen_ir.h"
#include "optimizer_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Nota: En una implementación real, esto se integraría con el parser de jasboot-lang
// Por ahora, creamos un ejemplo simple

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo.jasb> -o <archivo.jbo> [--opt]\n", argv[0]);
        fprintf(stderr, "  o: %s <archivo.jasb> --validate\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    const char* output_file = NULL;
    int validate_only = 0;
    int optimize = 0;
    
    // Parsear argumentos
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--validate") == 0) {
            validate_only = 1;
        } else if (strcmp(argv[i], "--opt") == 0) {
            optimize = 1;
        }
    }
    
    if (validate_only) {
        // Solo validar
        fprintf(stderr, "Validación de archivos IR no implementada aún\n");
        return 1;
    }
    
    if (!output_file) {
        fprintf(stderr, "Error: Se requiere archivo de salida (-o)\n");
        return 1;
    }
    
    // Por ahora, crear un ejemplo simple
    // En una implementación real, aquí se parsearía el archivo .jasb
    printf("🧠 jasboot IR Compiler\n");
    printf("======================\n\n");
    printf("Compilando: %s -> %s\n", input_file, output_file);
    
    // Crear archivo IR de ejemplo
    IRFile* ir = ir_file_create();
    if (!ir) {
        fprintf(stderr, "Error: No se pudo crear archivo IR\n");
        return 1;
    }
    
    // Agregar algunas instrucciones de ejemplo
    IRInstruction inst;
    
    // mover r1, 10
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 10;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // sumar r2, r1, 5
    inst.opcode = OP_SUMAR;
    inst.flags = IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 2;
    inst.operand_b = 1;
    inst.operand_c = 5;
    ir_file_add_instruction(ir, &inst);
    
    // observar r2
    inst.opcode = OP_OBSERVAR;
    inst.flags = 0;
    inst.operand_a = 2;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // retornar 0
    inst.opcode = OP_RETORNAR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    if (optimize) {
        IROptimizationStats stats = {0};
        if (ir_optimize(ir, &stats) != 0) {
            fprintf(stderr, "Error: No se pudo optimizar IR\n");
            ir_file_destroy(ir);
            return 1;
        }
        printf("✓ Optimización aplicada (%zu -> %zu instrucciones)\n",
               stats.instrucciones_originales, stats.instrucciones_finales);
    }
    
    // Escribir archivo
    if (ir_file_write(ir, output_file) != 0) {
        fprintf(stderr, "Error: No se pudo escribir archivo IR\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Archivo IR generado: %s\n", output_file);
    printf("  Instrucciones: %zu\n", ir->code_count);
    printf("  Tamaño código: %u bytes\n", ir->header.code_size);
    
    ir_file_destroy(ir);
    return 0;
}
