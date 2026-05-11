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
    jb_var_t ruta = jb_new_texto("tmp_108_fs.bin");
    jb_var_t h = jb_fs_abrir(ruta, jb_new_texto("wb"));
    jb_fs_escribir_byte(jb_new_entero(74), h);
    jb_fs_escribir_byte(jb_new_entero(66), h);
    jb_fs_escribir_byte(jb_new_entero(77), h);
    jb_fs_escribir_byte(jb_new_entero(255), h);
    jb_fs_cerrar(h);
    jb_imprimir(jb_existe_archivo(ruta));
    jb_assign(&h, jb_fs_abrir(ruta, jb_new_texto("rb")));
    jb_imprimir(jb_fs_leer_byte(h));
    jb_imprimir(jb_fs_leer_byte(h));
    jb_var_t tercero = jb_fs_leer_byte(h);
    jb_imprimir(jb_fs_leer_byte(h));
    jb_imprimir(jb_entero_a_texto(tercero));
    jb_fs_cerrar(h);
    jb_cleanup();
    return 0;
}
