#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "reader_ir.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Uso: jasboot-vm [--continuo] <archivo.jbo>\n");
        return 1;
    }
    VM* vm = vm_create();
    if (!vm) return 1;

    vm->argc = argc;
    vm->argv = argv;
    if (argc >= 3 && strcmp(argv[1], "--continuo") == 0) {
        vm_set_modo_continuo(vm, 1);
        argv++; argc--;
    }
    if (ir_file_read(vm->ir, argv[1]) != 0) {
        printf("Error: No se pudo cargar el archivo IR %s\n", argv[1]);
        vm_destroy(vm);
        return 1;
    }
    // printf("[VM_MAIN DEBUG] argc=%d\n", vm->argc);
    int code = vm_run(vm);
    vm_destroy(vm);
    return code != 0 ? code : 0;
}
