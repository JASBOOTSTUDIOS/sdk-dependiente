#include "ir_format.h"
#include "reader_ir.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/wait.h>
#endif

// Test de rendimiento: Generar y ejecutar muchos programas
int test_rendimiento_generacion(void) {
    printf("Test Rendimiento: Generación de IR\n");
    printf("-----------------------------------\n");
    
    clock_t inicio = clock();
    
    // Generar 1000 programas pequeños
    for (int i = 0; i < 1000; i++) {
        IRFile* ir = ir_file_create();
        if (!ir) {
            printf("❌ Error creando IRFile\n");
            return 1;
        }
        
        // Agregar 100 instrucciones
        IRInstruction inst;
        for (int j = 0; j < 100; j++) {
            inst.opcode = OP_MOVER;
            inst.flags = IR_INST_FLAG_B_IMMEDIATE;
            inst.operand_a = (j % 16) + 1;
            inst.operand_b = j;
            inst.operand_c = 0;
            ir_file_add_instruction(ir, &inst);
        }
        
        ir_file_destroy(ir);
    }
    
    clock_t fin = clock();
    double tiempo = ((double)(fin - inicio)) / CLOCKS_PER_SEC;
    
    printf("✓ Generados 1000 programas (100 instrucciones c/u) en %.3f segundos\n", tiempo);
    printf("  Promedio: %.3f ms por programa\n", (tiempo * 1000) / 1000);
    
    return 0;
}

// Test de rendimiento: Validación masiva
int test_rendimiento_validacion(void) {
    printf("\nTest Rendimiento: Validación Masiva\n");
    printf("------------------------------------\n");
    
    // Crear un programa grande
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ Error creando IRFile\n");
        return 1;
    }
    
    IRInstruction inst;
    for (int i = 0; i < 10000; i++) {
        inst.opcode = OP_MOVER;
        inst.flags = IR_INST_FLAG_B_IMMEDIATE;
        inst.operand_a = (i % 16) + 1;
        inst.operand_b = i % 256;
        inst.operand_c = 0;
        ir_file_add_instruction(ir, &inst);
    }
    
    clock_t inicio = clock();
    
    // Validar 100 veces
    for (int i = 0; i < 100; i++) {
        IRValidationInfo info = ir_validate_memory(ir);
        if (info.result != IR_VALID_OK) {
            printf("❌ Validación falló\n");
            ir_file_destroy(ir);
            return 1;
        }
    }
    
    clock_t fin = clock();
    double tiempo = ((double)(fin - inicio)) / CLOCKS_PER_SEC;
    
    printf("✓ Validadas 100 veces (10000 instrucciones) en %.3f segundos\n", tiempo);
    printf("  Promedio: %.3f ms por validación\n", (tiempo * 1000) / 100);
    
    ir_file_destroy(ir);
    return 0;
}

// Test de rendimiento: Ejecución en VM
int test_rendimiento_vm(void) {
    printf("\nTest Rendimiento: Ejecución en VM\n");
    printf("----------------------------------\n");
    
    // Crear programa con muchas operaciones
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ Error creando IRFile\n");
        return 1;
    }
    
    IRInstruction inst;
    
    // Programa: calcular suma de 1 a 1000
    // mover r1, 0 (acumulador)
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);
    
    // mover r2, 1 (contador)
    inst.operand_a = 2;
    inst.operand_b = 1;
    ir_file_add_instruction(ir, &inst);
    
    // Nota: El límite de 1000 se maneja con el loop, no como inmediato
    // (los operandos son uint8_t, máximo 255)
    
    // Loop simplificado (1000 iteraciones)
    for (int i = 0; i < 1000; i++) {
        // sumar r1, r1, r2 (acumulador += contador)
        inst.opcode = OP_SUMAR;
        inst.flags = 0;
        inst.operand_a = 1;
        inst.operand_b = 1;
        inst.operand_c = 2;
        ir_file_add_instruction(ir, &inst);
        
        // sumar r2, r2, 1 (contador++)
        inst.opcode = OP_SUMAR;
        inst.flags = IR_INST_FLAG_C_IMMEDIATE;
        inst.operand_a = 2;
        inst.operand_b = 2;
        inst.operand_c = 1;
        ir_file_add_instruction(ir, &inst);
    }
    
    // Ejecutar
    VM* vm = vm_create();
    if (!vm) {
        printf("❌ Error creando VM\n");
        ir_file_destroy(ir);
        return 1;
    }
    
    if (vm_load(vm, ir) != 0) {
        printf("❌ Error cargando IR\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    clock_t inicio = clock();
    vm_run(vm);
    clock_t fin = clock();
    double tiempo = ((double)(fin - inicio)) / CLOCKS_PER_SEC;
    
    uint64_t resultado = vm_get_register(vm, 1);
    uint64_t esperado = 500500; // Suma de 1 a 1000
    
    if (resultado != esperado) {
        printf("❌ Resultado incorrecto: esperado %llu, obtenido %llu\n", 
               (unsigned long long)esperado, (unsigned long long)resultado);
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Ejecutado programa (2000+ instrucciones) en %.3f segundos\n", tiempo);
    printf("  Resultado: %llu (correcto)\n", (unsigned long long)resultado);
    printf("  Instrucciones/segundo: ~%.0f\n", (ir->code_count / tiempo));
    
    vm_destroy(vm);
    ir_file_destroy(ir);
    return 0;
}

// Test de estrés: Programa muy grande
int test_estres_grande(void) {
    printf("\nTest Estrés: Programa Muy Grande\n");
    printf("---------------------------------\n");
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ Error creando IRFile\n");
        return 1;
    }
    
    printf("Generando 100,000 instrucciones...\n");
    IRInstruction inst;
    
    // Lista de opcodes válidos para variar
    uint8_t opcodes_validos[] = {
        OP_MOVER, OP_LEER, OP_ESCRIBIR,
        OP_SUMAR, OP_RESTAR, OP_MULTIPLICAR, OP_DIVIDIR,
        OP_Y, OP_O, OP_XOR, OP_NO,
        OP_COMPARAR,
        OP_IR, OP_SI, OP_LLAMAR, OP_RETORNAR,
        OP_MARCAR_ESTADO, OP_OBSERVAR, OP_NOP
    };
    int num_opcodes = sizeof(opcodes_validos) / sizeof(opcodes_validos[0]);
    
    for (int i = 0; i < 100000; i++) {
        inst.opcode = opcodes_validos[i % num_opcodes]; // Solo opcodes válidos
        inst.flags = (i % 2) ? IR_INST_FLAG_B_IMMEDIATE : 0;
        inst.operand_a = (i % 16) + 1;
        inst.operand_b = i % 256;
        inst.operand_c = (i + 1) % 256;
        ir_file_add_instruction(ir, &inst);
    }
    
    printf("✓ Generadas %zu instrucciones\n", ir->code_count);
    
    // Validar
    clock_t inicio = clock();
    IRValidationInfo info = ir_validate_memory(ir);
    clock_t fin = clock();
    double tiempo = ((double)(fin - inicio)) / CLOCKS_PER_SEC;
    
    if (info.result != IR_VALID_OK) {
        printf("❌ Validación falló: %s\n", info.message);
        ir_file_destroy(ir);
        return 1;
    }
    
    printf("✓ Validación exitosa en %.3f segundos\n", tiempo);
    
    // Escribir a archivo
    inicio = clock();
    if (ir_file_write(ir, "test_estres.jbo") != 0) {
        printf("❌ Error escribiendo archivo\n");
        ir_file_destroy(ir);
        return 1;
    }
    fin = clock();
    tiempo = ((double)(fin - inicio)) / CLOCKS_PER_SEC;
    
    printf("✓ Archivo escrito en %.3f segundos\n", tiempo);
    printf("  Tamaño: %u bytes\n", ir->header.code_size);
    
    ir_file_destroy(ir);
    return 0;
}

// Test mínimo: Backend directo (compilar + ejecutar)
int test_rendimiento_backend(void) {
    printf("\nTest Rendimiento: Backend Directo\n");
    printf("----------------------------------\n");

    IRFile* ir = ir_file_create();
    if (!ir) {
        printf("❌ Error creando IRFile\n");
        return 1;
    }

    IRInstruction inst;
    // mover r1, 40
    inst.opcode = OP_MOVER;
    inst.flags = IR_INST_FLAG_B_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 40;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);

    // sumar r1, r1, 2
    inst.opcode = OP_SUMAR;
    inst.flags = IR_INST_FLAG_C_IMMEDIATE;
    inst.operand_a = 1;
    inst.operand_b = 1;
    inst.operand_c = 2;
    ir_file_add_instruction(ir, &inst);

    // retornar r1
    inst.opcode = OP_RETORNAR;
    inst.flags = 0;
    inst.operand_a = 1;
    inst.operand_b = 0;
    inst.operand_c = 0;
    ir_file_add_instruction(ir, &inst);

    if (ir_file_write(ir, "backend_bench.jbo") != 0) {
        printf("❌ Error escribiendo backend_bench.jbo\n");
        ir_file_destroy(ir);
        return 1;
    }

    VM* vm = vm_create();
    if (!vm) {
        printf("❌ Error creando VM\n");
        ir_file_destroy(ir);
        return 1;
    }
    if (vm_load(vm, ir) != 0) {
        printf("❌ Error cargando IR en VM\n");
        vm_destroy(vm);
        ir_file_destroy(ir);
        return 1;
    }
    int exit_vm = vm_run(vm);
    vm_destroy(vm);
    if (exit_vm != 42) {
        printf("❌ Resultado VM incorrecto: esperado 42, obtenido %d\n", exit_vm);
        ir_file_destroy(ir);
        return 1;
    }

    clock_t inicio = clock();
    int build_status = system("./bin/jasboot-ir-backend backend_bench.jbo -o backend_bench.elf");
    clock_t fin_build = clock();
    if (build_status != 0) {
        printf("❌ Error generando backend directo\n");
        ir_file_destroy(ir);
        return 1;
    }

    int run_status = system("./backend_bench.elf");
    clock_t fin_run = clock();

#ifdef _WIN32
    int exit_code = run_status;
#else
    int exit_code = WIFEXITED(run_status) ? WEXITSTATUS(run_status) : 1;
#endif
    if (exit_code != 42) {
        printf("❌ Resultado backend incorrecto: esperado 42, obtenido %d\n", exit_code);
        ir_file_destroy(ir);
        return 1;
    }

    double tiempo_build = ((double)(fin_build - inicio)) / CLOCKS_PER_SEC;
    double tiempo_run = ((double)(fin_run - fin_build)) / CLOCKS_PER_SEC;
    printf("✓ Backend directo OK (exit=42)\n");
    printf("  Build: %.3f s | Run: %.3f s\n", tiempo_build, tiempo_run);

    ir_file_destroy(ir);
    return 0;
}

int main(void) {
    printf("⚡ jasboot IR - Tests de Rendimiento y Estrés\n");
    printf("==============================================\n\n");
    
    int errors = 0;
    
    errors += test_rendimiento_generacion();
    errors += test_rendimiento_validacion();
    errors += test_rendimiento_vm();
    errors += test_estres_grande();
    errors += test_rendimiento_backend();
    
    printf("\n==============================================\n");
    if (errors == 0) {
        printf("✅ Todos los tests de rendimiento pasaron\n");
        return 0;
    } else {
        printf("❌ %d test(s) de rendimiento fallaron\n", errors);
        return 1;
    }
}
