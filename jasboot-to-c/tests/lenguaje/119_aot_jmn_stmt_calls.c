#include "jasboot_rt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4505)
#endif


#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

int main(int argc, char **argv) {
    jb_init();
    jb_set_argv(argc, argv);
    jb_crear_memoria(jb_new_texto("sdk-dependiente/jasboot-to-c/tests/lenguaje/119_mem.jmn"));
    jb_recordar(jb_new_texto("k1"), jb_new_texto("v1"));
    jb_imprimir(jb_mem_obtener_fuerza(jb_new_texto("k1"), jb_new_texto("v1")));
    jb_consolidar_memoria();
    jb_cerrar_memoria();
    jb_cleanup();
    return 0;
}
