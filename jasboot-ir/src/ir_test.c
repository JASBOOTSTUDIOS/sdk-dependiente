#include "ir_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("🧠 jasboot IR Binario - Test Básico\n");
    printf("=====================================\n\n");
    
    // Crear archivo IR
    IRFile* ir = ir_file_create();
    if (!ir) {
        fprintf(stderr, "Error: No se pudo crear IRFile\n");
        return 1;
    }
    
    printf("✓ IRFile creado\n");
    
    // Agregar datos de prueba
    const char* texto = "hola";
    size_t data_offset = 0;
    if (ir_file_add_string(ir, texto, &data_offset) != 0) {
        fprintf(stderr, "Error: No se pudo agregar datos\n");
        ir_file_destroy(ir);
        return 1;
    }
    printf("✓ Datos agregados: \"%s\" (offset %zu)\n", texto, data_offset);
    
    // Agregar algunas instrucciones de prueba
    IRInstruction inst;
    
    // mover r1, 10
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;  // r1
    inst.operand_b = 10; // inmediato 10
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) {
        fprintf(stderr, "Error: No se pudo agregar instrucción\n");
        ir_file_destroy(ir);
        return 1;
    }
    printf("✓ Instrucción agregada: mover r1, 10\n");
    
    // sumar r2, r1, 5
    inst.opcode = OP_SUMAR;
    inst.flags = IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 2;  // r2
    inst.operand_b = 1;  // r1
    inst.operand_c = 5;  // inmediato 5
    if (ir_file_add_instruction(ir, &inst) != 0) {
        fprintf(stderr, "Error: No se pudo agregar instrucción\n");
        ir_file_destroy(ir);
        return 1;
    }
    printf("✓ Instrucción agregada: sumar r2, r1, 5\n");
    
    // retornar
    inst.opcode = OP_RETORNAR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 0;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) {
        fprintf(stderr, "Error: No se pudo agregar instrucción\n");
        ir_file_destroy(ir);
        return 1;
    }
    printf("✓ Instrucción agregada: retornar\n");
    
    // Escribir archivo
    const char* filename = "test.jbo";
    if (ir_file_write(ir, filename) != 0) {
        fprintf(stderr, "Error: No se pudo escribir archivo\n");
        ir_file_destroy(ir);
        return 1;
    }
    printf("✓ Archivo escrito: %s\n", filename);
    
    // Leer archivo de vuelta
    IRFile* ir2 = ir_file_create();
    if (!ir2) {
        fprintf(stderr, "Error: No se pudo crear IRFile para lectura\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (ir_file_read(ir2, filename) != 0) {
        fprintf(stderr, "Error: No se pudo leer archivo\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    printf("✓ Archivo leído: %s\n", filename);
    
    // Verificar
    if (ir->header.code_size != ir2->header.code_size) {
        fprintf(stderr, "Error: Tamaño de código no coincide\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    printf("✓ Verificación: Tamaño de código coincide (%u bytes)\n", ir->header.code_size);
    
    if (ir->header.data_size != ir2->header.data_size) {
        fprintf(stderr, "Error: Tamaño de datos no coincide\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    if (memcmp(ir->data, ir2->data, ir->header.data_size) != 0) {
        fprintf(stderr, "Error: Contenido de datos no coincide\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    printf("✓ Verificación: Datos coinciden (%u bytes)\n", ir->header.data_size);
    
    printf("\n✅ Test completado exitosamente!\n");
    printf("   Archivo generado: %s\n", filename);
    printf("   Instrucciones: %zu\n", ir->code_count);
    printf("   Tamaño código: %u bytes\n", ir->header.code_size);
    printf("   Tamaño datos: %u bytes\n", ir->header.data_size);
    
    ir_file_destroy(ir);
    ir_file_destroy(ir2);
    return 0;
}
