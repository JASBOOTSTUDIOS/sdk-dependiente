#include "vm.h"
#include "platform_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Persistencia JMN: cerrar memoria al salir (incl. Ctrl+C, exit rápido) */
static VM* g_vm_for_atexit = NULL;

static void ir_vm_atexit_persist(void) {
    if (g_vm_for_atexit) {
        vm_destroy(g_vm_for_atexit);
        g_vm_for_atexit = NULL;
    }
}

int main(int argc, char** argv) {
    atexit(ir_vm_atexit_persist);
    jasboot_init_console();
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [--step-limit N] [--continuo] <archivo.jbo>\n", argv[0]);
        return 1;
    }
    
    const char* filename = NULL;
    size_t step_limit = 0; // 0 = sin límite (o default interno)
    int modo_continuo = 0; // 7.1: EOF stdin no detiene VM
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--step-limit") == 0 && i + 1 < argc) {
            step_limit = (size_t)atoll(argv[++i]);
        } else if (strcmp(argv[i], "--continuo") == 0) {
            modo_continuo = 1;
        } else if (filename == NULL) {
            filename = argv[i];
        }
    }
    
    if (!filename) {
        fprintf(stderr, "Error: No se especificó archivo IR\n");
        return 1;
    }
    
    VM* vm = vm_create();
    if (!vm) {
        fprintf(stderr, "Error: No se pudo crear VM\n");
        return 1;
    }
    g_vm_for_atexit = vm;

    // Pasar argumentos de sistema
    vm->argc = argc;
    vm->argv = argv;
    
    if (modo_continuo) vm_set_modo_continuo(vm, 1);
    if (getenv("JASBOOT_DEBUG")) printf("[DEBUG] Cargando %s...\n", filename);
    if (vm_load_file(vm, filename) != 0) {
        fprintf(stderr, "Error: No se pudo cargar el archivo IR %s\n", filename);
        g_vm_for_atexit = NULL;
        vm_destroy(vm);
        return 1;
    }
    
    if (getenv("JASBOOT_DEBUG")) printf("[DEBUG] Iniciando ejecucion (argc=%d)...\n", vm->argc);
    int exit_code;
    if (step_limit > 0) {
        exit_code = vm_run_with_limit(vm, step_limit);
    } else {
        exit_code = vm_run(vm);
    }
    
    g_vm_for_atexit = NULL; /* Evitar doble destroy en atexit */
    vm_destroy(vm);
    return exit_code;
}
