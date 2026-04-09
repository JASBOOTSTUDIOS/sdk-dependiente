#include "optimizer_ir.h"
#include "reader_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo.jbo> -o <archivo_opt.jbo> [--stats]\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    const char* output_file = NULL;
    int show_stats = 0;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--stats") == 0) {
            show_stats = 1;
        }
    }
    
    if (!output_file) {
        fprintf(stderr, "Error: Se requiere archivo de salida (-o)\n");
        return 1;
    }
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        fprintf(stderr, "Error: No se pudo crear IRFile\n");
        return 1;
    }
    
    if (ir_file_read(ir, input_file) != 0) {
        fprintf(stderr, "Error: No se pudo leer archivo IR\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    IRValidationInfo info = ir_validate_memory(ir);
    if (info.result != IR_VALID_OK) {
        fprintf(stderr, "Error: IR inválido antes de optimizar (%s)\n",
                ir_validation_result_to_string(info.result));
        ir_file_destroy(ir);
        return 1;
    }
    
    IROptimizationStats stats = {0};
    if (ir_optimize(ir, &stats) != 0) {
        fprintf(stderr, "Error: No se pudo optimizar IR\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (ir_file_write(ir, output_file) != 0) {
        fprintf(stderr, "Error: No se pudo escribir archivo IR optimizado\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (show_stats) {
        printf("Optimización completada\n");
        printf("  Instrucciones: %zu -> %zu\n", stats.instrucciones_originales, stats.instrucciones_finales);
        printf("  Constantes plegadas: %zu\n", stats.constantes_plegadas);
        printf("  Inmediatos propagados: %zu\n", stats.inmediatos_prop);
        printf("  DCE eliminadas: %zu\n", stats.dce_eliminados);
        printf("  NOPs eliminados: %zu\n", stats.nops_eliminados);
        printf("  Saltos simplificados: %zu\n", stats.saltos_simplificados);
        printf("  Compactación: %s\n", stats.compactacion_exitosa ? "si" : "no");
    }
    
    ir_file_destroy(ir);
    return 0;
}
