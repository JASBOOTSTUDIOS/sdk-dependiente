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
    jb_var_t nombre = jb_new_texto("Jasboot");
    jb_var_t version = jb_new_texto("AOT 1.0");
    jb_imprimir(jb_new_texto("Lenguaje:"));
    jb_imprimir(nombre);
    jb_imprimir(jb_new_texto("Modo:"));
    jb_imprimir(version);
    jb_cleanup();
    return 0;
}
