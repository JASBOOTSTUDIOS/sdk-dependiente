#include "ir_format.h"
#include "reader_ir.h"
#include "vm.h"
#include "optimizer_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test completo del sistema IR
int test_formato_basico(void) {
    printf("Test 1: Formato básico\n");
    printf("----------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ No se pudo crear IRFile\n");
        return 1;
    }
    
    // Agregar instrucciones
    IRInstruction inst;
    
    // mover r1, 42
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 42;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // sumar r2, r1, 8
    inst.opcode = OP_SUMAR;
    inst.flags = 0;
    inst.operand_a = 2;
    inst.operand_b = 1;
    inst.operand_c = 8;
    ir_file_add_instruction(ir, &inst);
    
    // Escribir y leer
    const char* test_file = "test_completo.jbo";
    if (ir_file_write(ir, test_file) != 0) {
        printf("❌ No se pudo escribir archivo\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    IRFile* ir2 = ir_file_create();
    if (ir_file_read(ir2, test_file) != 0) {
        printf("❌ No se pudo leer archivo\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    
    // Verificar
    if (ir->code_count != ir2->code_count) {
        printf("❌ Número de instrucciones no coincide\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    
    printf("✓ Formato básico OK\n");
    ir_file_destroy(ir);
    ir_file_destroy(ir2);
    return 0;
}

int test_validacion(void) {
    printf("\nTest 2: Validación\n");
    printf("-------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ No se pudo crear IRFile\n");
        return 1;
    }
    
    // Agregar instrucciones válidas
    IRInstruction inst;
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 10;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    IRValidationInfo info = ir_validate_memory(ir);
    if (info.result != IR_VALID_OK) {
        printf("❌ Validación falló: %s\n", info.message);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Validación OK\n");
    ir_file_destroy(ir);
    return 0;
}

int test_vm(void) {
    printf("\nTest 3: Máquina Virtual\n");
    printf("-----------------------\n");
    
    // Crear IR simple
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ No se pudo crear IRFile\n");
        return 1;
    }
    
    IRInstruction inst;
    
    // mover r1, 5
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 5;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // mover r2, 3
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 2;
    inst.operand_b = 3;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // sumar r3, r1, r2
    inst.opcode = OP_SUMAR;
    inst.flags = 0;
    inst.operand_a = 3;
    inst.operand_b = 1;
    inst.operand_c = 2;
    ir_file_add_instruction(ir, &inst);
    
    // observar r3
    inst.opcode = OP_OBSERVAR;
    inst.flags = 0;
    inst.operand_a = 3;
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
    
    // Ejecutar en VM
    VM* vm = vm_create();
    if (!vm) {
        printf("❌ No se pudo crear VM\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (vm_load(vm, ir) != 0) {
        printf("❌ No se pudo cargar IR en VM\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("Ejecutando programa...\n");
    int exit_code = vm_run(vm);
    
    // Verificar resultado
    uint64_t r3 = vm_get_register(vm, 3);
    if (r3 != 8) {
        printf("❌ Resultado incorrecto: esperado 8, obtenido %llu\n", (unsigned long long)r3);
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ VM OK (r3 = %llu, exit_code = %d)\n", (unsigned long long)r3, exit_code);
    vm_destroy(vm);
    ir_file_destroy(ir);
    return 0;
}

int test_datos(void) {
    printf("\nTest 5: Sección de Datos\n");
    printf("-------------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ No se pudo crear IRFile\n");
        return 1;
    }
    
    size_t data_offset = 0;
    uint8_t padding[300];
    memset(padding, 0, sizeof(padding));
    if (ir_file_add_data(ir, padding, sizeof(padding), NULL) != 0) {
        printf("❌ No se pudo agregar padding\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    uint64_t valor = 1234;
    if (ir_file_add_u64(ir, valor, &data_offset) != 0) {
        printf("❌ No se pudo agregar dato\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    IRInstruction inst;
    inst.opcode = OP_LEER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = (uint8_t)(data_offset & 0xFF);
    inst.operand_c = (uint8_t)((data_offset >> 8) & 0xFF);
    ir_file_add_instruction(ir, &inst);
    
    inst.opcode = OP_RETORNAR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    VM* vm = vm_create();
    if (!vm) {
        printf("❌ No se pudo crear VM\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (vm_load(vm, ir) != 0) {
        printf("❌ No se pudo cargar IR en VM\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    int exit_code = vm_run(vm);
    uint64_t r1 = vm_get_register(vm, 1);
    
    if (r1 != valor) {
        printf("❌ Lectura de datos incorrecta: esperado %llu, obtenido %llu\n",
               (unsigned long long)valor, (unsigned long long)r1);
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Datos OK (r1 = %llu, exit_code = %d)\n", (unsigned long long)r1, exit_code);
    vm_destroy(vm);
    ir_file_destroy(ir);
    return 0;
}

int test_optimizaciones(void) {
    printf("\nTest 6: Optimizaciones\n");
    printf("-----------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ No se pudo crear IRFile\n");
        return 1;
    }
    
    IRInstruction inst;
    // mover r1, 5
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 5;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // mover r1, 5 (redundante)
    ir_file_add_instruction(ir, &inst);
    
    // sumar r2, r1, 0 (plegable)
    inst.opcode = OP_SUMAR;
    inst.flags = IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 2;
    inst.operand_b = 1;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // sumar r3, r2, 10 (plegable)
    inst.opcode = OP_SUMAR;
    inst.flags = IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 3;
    inst.operand_b = 2;
    inst.operand_c = 10;
    ir_file_add_instruction(ir, &inst);
    
    // mover r4, 7 (muerto)
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 4;
    inst.operand_b = 7;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // sumar r4, r4, 1 (muerto)
    inst.opcode = OP_SUMAR;
    inst.flags = IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 4;
    inst.operand_b = 4;
    inst.operand_c = 1;
    ir_file_add_instruction(ir, &inst);
    
    // observar r3
    inst.opcode = OP_OBSERVAR;
    inst.flags = 0;
    inst.operand_a = 3;
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
    
    VM* vm = vm_create();
    if (!vm) {
        printf("❌ No se pudo crear VM\n");
        ir_file_destroy(ir);
        return 1;
    }
    if (vm_load(vm, ir) != 0) {
        printf("❌ No se pudo cargar IR en VM\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    vm_run(vm);
    uint64_t antes = vm_get_register(vm, 3);
    vm_destroy(vm);
    
    IROptimizationStats stats = {0};
    if (ir_optimize(ir, &stats) != 0) {
        printf("❌ Falló optimización\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    vm = vm_create();
    if (!vm) {
        printf("❌ No se pudo crear VM\n");
        ir_file_destroy(ir);
        return 1;
    }
    if (vm_load(vm, ir) != 0) {
        printf("❌ No se pudo cargar IR optimizado en VM\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    vm_run(vm);
    uint64_t despues = vm_get_register(vm, 3);
    vm_destroy(vm);
    
    if (antes != despues) {
        printf("❌ Optimización cambió resultado: %llu vs %llu\n",
               (unsigned long long)antes, (unsigned long long)despues);
        ir_file_destroy(ir);
        return 1;
    }
    
    if (stats.instrucciones_finales >= stats.instrucciones_originales) {
        printf("❌ No se redujeron instrucciones (%zu -> %zu)\n",
               stats.instrucciones_originales, stats.instrucciones_finales);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Optimizaciones OK (%zu -> %zu)\n",
           stats.instrucciones_originales, stats.instrucciones_finales);
    ir_file_destroy(ir);
    return 0;
}

int test_metadata_ia(void) {
    printf("\nTest 4: Metadata IA\n");
    printf("-------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ No se pudo crear IRFile\n");
        return 1;
    }
    
    // Agregar metadata IA (formato extendido)
    IRJasbSecPolicy policy;
    policy.version = 1;
    policy.mode = 1;  // warn
    policy.max_stack = 256;
    policy.max_jump = 64;
    uint8_t* metadata = NULL;
    size_t metadata_len = 0;
    if (ir_build_ia_metadata(&metadata, &metadata_len,
                             "jasboot-ir-test", "build-ia-01", &policy) != 0) {
        printf("❌ No se pudo construir metadata IA\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (ir_file_set_ia_metadata(ir, metadata, metadata_len) != 0) {
        printf("❌ No se pudo establecer metadata IA\n");
        free(metadata);
        ir_file_destroy(ir);
        return 1;
    }
    free(metadata);
    
    // Verificar flag
    if (!(ir->header.flags & IR_FLAG_IA_METADATA)) {
        printf("❌ Flag IA_METADATA no está activo\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    // Escribir y leer
    const char* test_file = "test_metadata.jbo";
    if (ir_file_write(ir, test_file) != 0) {
        printf("❌ No se pudo escribir archivo\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    IRFile* ir2 = ir_file_create();
    if (ir_file_read(ir2, test_file) != 0) {
        printf("❌ No se pudo leer archivo\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }
    
    // Verificar metadata
    if (ir2->ia_metadata_size != metadata_len) {
        printf("❌ Tamaño de metadata no coincide\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }

    if (ir2->ia_metadata_size < 8 ||
        ir2->ia_metadata[0] != IR_IA_MAGIC_0 ||
        ir2->ia_metadata[1] != IR_IA_MAGIC_1 ||
        ir2->ia_metadata[2] != IR_IA_MAGIC_2 ||
        ir2->ia_metadata[3] != IR_IA_MAGIC_3) {
        printf("❌ Metadata IA sin magic válido\n");
        ir_file_destroy(ir);
        ir_file_destroy(ir2);
        return 1;
    }

    printf("✓ Metadata IA OK\n");
    ir_file_destroy(ir);
    ir_file_destroy(ir2);
    return 0;
}

int main(void) {
    printf("🧠 jasboot IR - Test Completo\n");
    printf("==============================\n\n");
    
    int errors = 0;
    
    errors += test_formato_basico();
    errors += test_validacion();
    errors += test_vm();
    errors += test_metadata_ia();
    errors += test_datos();
    errors += test_optimizaciones();
    
    printf("\n==============================\n");
    if (errors == 0) {
        printf("✅ Todos los tests pasaron\n");
        return 0;
    } else {
        printf("❌ %d test(s) fallaron\n", errors);
        return 1;
    }
}
