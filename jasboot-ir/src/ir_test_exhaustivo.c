#include "ir_format.h"
#include "reader_ir.h"
#include "vm.h"
#include "optimizer_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Test exhaustivo: Todas las instrucciones individualmente
int test_todas_instrucciones(void) {
    printf("Test Exhaustivo: Todas las Instrucciones\n");
    printf("-----------------------------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) return 1;
    
    IRInstruction inst;
    int errores = 0;
    
    // Transferencia
    printf("Probando transferencia...\n");
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 42;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_LEER;
    inst.flags = 0;
    inst.operand_a = 2;
    inst.operand_b = 0;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_ESCRIBIR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 1;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    // Aritmética
    printf("Probando aritmética...\n");
    inst.opcode = OP_SUMAR;
    inst.flags = 0;
    inst.operand_a = 3;
    inst.operand_b = 1;
    inst.operand_c = 2;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_RESTAR;
    inst.operand_a = 4;
    inst.operand_b = 3;
    inst.operand_c = 1;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_MULTIPLICAR;
    inst.operand_a = 5;
    inst.operand_b = 2;
    inst.operand_c = 3;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_DIVIDIR;
    inst.operand_a = 6;
    inst.operand_b = 10;
    inst.operand_c = 2;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    // Lógica
    printf("Probando lógica...\n");
    inst.opcode = OP_Y;
    inst.operand_a = 7;
    inst.operand_b = 0xFF;
    inst.operand_c = 0x0F;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_O;
    inst.operand_a = 8;
    inst.operand_b = 0xF0;
    inst.operand_c = 0x0F;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_XOR;
    inst.operand_a = 9;
    inst.operand_b = 0xFF;
    inst.operand_c = 0x0F;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_NO;
    inst.flags = 0;
    inst.operand_a = 10;
    inst.operand_b = 0xFF;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    // Comparación
    printf("Probando comparación...\n");
    inst.opcode = OP_COMPARAR;
    inst.operand_a = 11;
    inst.operand_b = 5;
    inst.operand_c = 3;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    // Control de flujo
    printf("Probando control de flujo...\n");
    inst.opcode = OP_IR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 0;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_SI;
    inst.operand_a = 11;
    inst.operand_b = 0;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_LLAMAR;
    inst.operand_a = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_RETORNAR;
    inst.operand_a = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    // Sistema/IA
    printf("Probando sistema/IA...\n");
    inst.opcode = OP_MARCAR_ESTADO;
    inst.operand_a = 1;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_OBSERVAR;
    inst.operand_a = 1;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    inst.opcode = OP_NOP;
    inst.operand_a = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) errores++;
    
    // Validar
    IRValidationInfo info = ir_validate_memory(ir);
    if (info.result != IR_VALID_OK) {
        printf("❌ Validación falló: %s\n", info.message);
        errores++;
    }
    
    if (errores == 0) {
        printf("✓ Todas las instrucciones probadas correctamente (%zu instrucciones)\n", ir->code_count);
    } else {
        printf("❌ %d errores al agregar instrucciones\n", errores);
    }
    
    ir_file_destroy(ir);
    return errores;
}

// Test de casos límite
int test_casos_limite(void) {
    printf("\nTest Exhaustivo: Casos Límite\n");
    printf("------------------------------\n");
    
    int errores = 0;
    
    // Test 1: Registro máximo (r255)
    printf("Probando registro máximo (r255)...\n");
    IRFile* ir = ir_file_create();
    if (!ir) return 1;
    
    IRInstruction inst;
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 255;  // r255
    inst.operand_b = 100;
    inst.operand_c = 0;
    if (ir_file_add_instruction(ir, &inst) != 0) {
        printf("❌ Error con registro 255\n");
        errores++;
    } else {
        printf("✓ Registro 255 funciona\n");
    }
    ir_file_destroy(ir);
    
    // Test 2: Valores inmediatos máximos
    printf("Probando valores inmediatos máximos...\n");
    ir = ir_file_create();
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 255;  // Máximo byte
    inst.operand_c = 255;
    if (ir_file_add_instruction(ir, &inst) != 0) {
        printf("❌ Error con valores inmediatos máximos\n");
        errores++;
    } else {
        printf("✓ Valores inmediatos máximos funcionan\n");
    }
    ir_file_destroy(ir);
    
    // Test 3: Programa vacío
    printf("Probando programa vacío...\n");
    ir = ir_file_create();
    IRValidationInfo info = ir_validate_memory(ir);
    if (info.result != IR_VALID_OK) {
        printf("❌ Programa vacío debería ser válido\n");
        errores++;
    } else {
        printf("✓ Programa vacío es válido\n");
    }
    ir_file_destroy(ir);
    
    // Test 4: Metadata IA grande
    printf("Probando metadata IA grande...\n");
    ir = ir_file_create();
    uint8_t* metadata_grande = malloc(10000);
    if (metadata_grande) {
        memset(metadata_grande, 0xAA, 10000);
        if (ir_file_set_ia_metadata(ir, metadata_grande, 10000) != 0) {
            printf("❌ Error estableciendo metadata grande\n");
            errores++;
        } else {
            printf("✓ Metadata IA grande funciona (%zu bytes)\n", ir->ia_metadata_size);
        }
        free(metadata_grande);
    }
    ir_file_destroy(ir);
    
    // Test 5: Direccionamiento inmediato 16-bit
    printf("Probando direccionamiento inmediato 16-bit...\n");
    ir = ir_file_create();
    if (!ir) return 1;
    uint8_t padding[300];
    memset(padding, 0, sizeof(padding));
    ir_file_add_data(ir, padding, sizeof(padding), NULL);
    uint64_t esperado = 0xDEADBEEF;
    size_t offset16 = 0;
    ir_file_add_u64(ir, esperado, &offset16);
    
    inst.opcode = OP_LEER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = (uint8_t)(offset16 & 0xFF);
    inst.operand_c = (uint8_t)((offset16 >> 8) & 0xFF);
    ir_file_add_instruction(ir, &inst);
    
    inst.opcode = OP_RETORNAR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    VM* vm = vm_create();
    if (!vm) {
        ir_file_destroy(ir);
        return 1;
    }
    if (vm_load(vm, ir) != 0) {
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    vm->running = 1;
    int steps = 0;
    const int max_steps = 256;
    while (vm->running && steps < max_steps) {
        if (vm_step(vm) != 0) {
            printf("❌ Error ejecutando VM tras optimizar saltos\n");
            vm_destroy(vm);
            ir_file_destroy(ir);
            return 1;
        }
        steps++;
    }
    if (steps >= max_steps) {
        printf("❌ Posible bucle infinito tras optimizar saltos\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    uint64_t leido = vm_get_register(vm, 1);
    if (leido != esperado) {
        printf("❌ Direccionamiento 16-bit incorrecto: esperado %llu, obtenido %llu\n",
               (unsigned long long)esperado, (unsigned long long)leido);
        errores++;
    } else {
        printf("✓ Direccionamiento 16-bit funciona (%llu)\n", (unsigned long long)leido);
    }
    vm_destroy(vm);
    ir_file_destroy(ir);
    
    return errores;
}

// Test de combinaciones complejas
int test_combinaciones_complejas(void) {
    printf("\nTest Exhaustivo: Combinaciones Complejas\n");
    printf("-----------------------------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) return 1;
    
    IRInstruction inst;
    
    // Programa complejo: calcular factorial de 5
    printf("Generando programa complejo (factorial)...\n");
    
    // mover r1, 5 (n)
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 5;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // mover r2, 1 (resultado)
    inst.operand_a = 2;
    inst.operand_b = 1;
    ir_file_add_instruction(ir, &inst);
    
    // Loop: multiplicar resultado por n, decrementar n
    for (int i = 0; i < 5; i++) {
        // multiplicar r2, r2, r1
        inst.opcode = OP_MULTIPLICAR;
        inst.flags = 0;
        inst.operand_a = 2;
        inst.operand_b = 2;
        inst.operand_c = 1;
        ir_file_add_instruction(ir, &inst);
        
        // restar r1, r1, 1
        inst.opcode = OP_RESTAR;
        inst.flags = IR_INST_FLAG_C_IMMEDIATE;
        inst.operand_a = 1;
        inst.operand_b = 1;
        inst.operand_c = 1;
        ir_file_add_instruction(ir, &inst);
    }
    
    // Validar
    IRValidationInfo info = ir_validate_memory(ir);
    if (info.result != IR_VALID_OK) {
        printf("❌ Validación falló: %s\n", info.message);
        ir_file_destroy(ir);
        return 1;
    }
    
    // Ejecutar en VM
    VM* vm = vm_create();
    if (!vm) {
        ir_file_destroy(ir);
        return 1;
    }
    
    if (vm_load(vm, ir) != 0) {
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    vm_run(vm);
    uint64_t resultado = vm_get_register(vm, 2);
    uint64_t esperado = 120; // 5! = 120
    
    if (resultado != esperado) {
        printf("❌ Resultado incorrecto: esperado %llu, obtenido %llu\n",
               (unsigned long long)esperado, (unsigned long long)resultado);
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Programa complejo ejecutado correctamente\n");
    printf("  Factorial de 5 = %llu (correcto)\n", (unsigned long long)resultado);
    
    vm_destroy(vm);
    ir_file_destroy(ir);
    return 0;
}

int test_optimizacion_saltos(void) {
    printf("\nTest Exhaustivo: Optimización de Saltos\n");
    printf("----------------------------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) return 1;
    
    IRInstruction inst;
    // 0: mover r1, 1
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 1;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // 1: si r1 -> PC = 15 (instrucción 3)
    inst.opcode = OP_SI;
    inst.flags = 0;
    inst.operand_a = 1;
    inst.operand_b = 15; // 3 * 5 bytes
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // 2: mover r2, 0 (muerto)
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 2;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // 3: observar r1
    inst.opcode = OP_OBSERVAR;
    inst.flags = 0;
    inst.operand_a = 1;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // 4: retornar 0
    inst.opcode = OP_RETORNAR;
    inst.flags = 0;
    inst.operand_a = 0;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    printf("Aplicando optimizador...\n");
    IROptimizationStats stats = {0};
    if (ir_optimize(ir, &stats) != 0) {
        printf("❌ Falló optimización de saltos\n");
        ir_file_destroy(ir);
        return 1;
    }
    printf("Optimizador aplicado\n");
    
    if (stats.saltos_simplificados == 0) {
        printf("❌ No se simplificaron saltos\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    VM* vm = vm_create();
    if (!vm) {
        ir_file_destroy(ir);
        return 1;
    }
    if (vm_load(vm, ir) != 0) {
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    vm->running = 1;
    int steps = 0;
    const int max_steps = 256;
    while (vm->running && steps < max_steps) {
        if (vm_step(vm) != 0) {
            printf("❌ Error ejecutando VM tras optimizar saltos\n");
            vm_destroy(vm);
            ir_file_destroy(ir);
            return 1;
        }
        steps++;
    }
    if (steps >= max_steps) {
        printf("❌ Posible bucle infinito tras optimizar saltos\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    uint64_t r1 = vm_get_register(vm, 1);
    if (r1 != 1) {
        printf("❌ Resultado incorrecto tras optimizar saltos\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    vm_destroy(vm);
    ir_file_destroy(ir);
    printf("✓ Optimización de saltos OK\n");
    return 0;
}

// Test de errores y casos inválidos
int test_casos_invalidos(void) {
    printf("\nTest Exhaustivo: Casos Inválidos\n");
    printf("---------------------------------\n");
    
    int errores = 0;
    
    // Test 1: Opcode inválido (simulado)
    printf("Probando validación de opcode inválido...\n");
    IRFile* ir = ir_file_create();
    if (!ir) return 1;
    
    // Agregar directamente al buffer (bypass de validación)
    size_t offset = ir->code_count * IR_INSTRUCTION_SIZE;
    if (offset < ir->code_capacity) {
        ir->code[offset + 0] = 0x99;  // Opcode inválido
        ir->code[offset + 1] = 0;
        ir->code[offset + 2] = 1;
        ir->code[offset + 3] = 2;
        ir->code[offset + 4] = 3;
        ir->code_count++;
        ir->header.code_size = ir->code_count * IR_INSTRUCTION_SIZE;
        
        IRValidationInfo info = ir_validate_memory(ir);
        if (info.result == IR_VALID_OK) {
            printf("❌ Debería detectar opcode inválido\n");
            errores++;
        } else {
            printf("✓ Opcode inválido detectado correctamente\n");
        }
    }
    ir_file_destroy(ir);
    
    // Test 2: Magic number inválido
    printf("Probando magic number inválido...\n");
    ir = ir_file_create();
    ir->header.magic[0] = 0x00;  // Magic inválido
    IRValidationInfo info = ir_validate_memory(ir);
    if (info.result == IR_VALID_OK) {
        printf("❌ Debería detectar magic inválido\n");
        errores++;
    } else {
        printf("✓ Magic inválido detectado correctamente\n");
    }
    ir_file_destroy(ir);
    
    // Test 3: Acceso inmediato fuera de datos
    printf("Probando acceso inmediato fuera de datos...\n");
    ir = ir_file_create();
    size_t data_offset = 0;
    uint32_t valor = 123;
    ir_file_add_data(ir, (const uint8_t*)&valor, sizeof(valor), &data_offset);
    IRInstruction inst;
    inst.opcode = OP_LEER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 200; // fuera de rango (data_size = 4)
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    info = ir_validate_memory(ir);
    if (info.result == IR_VALID_OK) {
        printf("❌ Debería detectar acceso fuera de datos\n");
        errores++;
    } else {
        printf("✓ Acceso fuera de datos detectado correctamente\n");
    }
    ir_file_destroy(ir);
    
    return errores;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("🔬 jasboot IR - Tests Exhaustivos\n");
    printf("==================================\n\n");
    
    int errors = 0;
    
    errors += test_todas_instrucciones();
    errors += test_casos_limite();
    errors += test_combinaciones_complejas();
    errors += test_optimizacion_saltos();
    errors += test_casos_invalidos();
    
    printf("\n==================================\n");
    if (errors == 0) {
        printf("✅ Todos los tests exhaustivos pasaron\n");
        return 0;
    } else {
        printf("❌ %d test(s) exhaustivos fallaron\n", errors);
        return 1;
    }
}
