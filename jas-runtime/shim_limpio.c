#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void _start();
extern void jasboot_init_args(int argc, char** argv);

// --- SHIM DE SALIDA LIMPIA ---
int __io_escribir(int fd, const char* buffer, int longitud) {
    if (longitud <= 0) return 0;
    FILE* out = (fd == 2) ? stderr : stdout;
    
    for (int i = 0; i < longitud; i++) {
        if (buffer[i] == '\\' && i + 1 < longitud && buffer[i+1] == 'n') {
            fputc('\n', out);
            i++;
        } else {
            fputc(buffer[i], out);
        }
    }
    fflush(out);
    return longitud;
}

int __sistema_ejecutar(const char* comando) {
    if (!comando) return -1;
    fflush(stdout); 
    return system(comando);
}

// ELIMINADOS LOS STUBS DE JMN_ PARA EVITAR MULTIPLE DEFINITION

int main(int argc, char** argv) {
    jasboot_init_args(argc, argv);
    _start();
    return 0;
}
