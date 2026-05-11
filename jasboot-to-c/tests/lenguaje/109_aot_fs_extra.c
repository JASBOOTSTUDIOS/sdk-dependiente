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
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_var_t r = jb_new_texto("tmp_109_onlyone_zz.txt");
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_var_t h = jb_fs_abrir(r, jb_new_texto("w"));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_fs_escribir(jb_new_texto("abc"), h);
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_fs_cerrar(h);
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_fs_tamano(r));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_var_t t = jb_fs_leer_texto(r);
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_texto_len(t));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_var_t glo = jb_fs_listar(jb_new_texto("tmp_109_onlyone_zz*"));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_texto_len(glo));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_fs_copiar(r, jb_new_texto("tmp_109_onlyone_copy.txt")));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_existe_archivo(jb_new_texto("tmp_109_onlyone_copy.txt")));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_fs_borrar(jb_new_texto("tmp_109_onlyone_copy.txt"));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_existe_archivo(jb_new_texto("tmp_109_onlyone_copy.txt")));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_fs_mover(r, jb_new_texto("tmp_109_onlyone_moved.txt")));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_existe_archivo(r));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_imprimir(jb_existe_archivo(jb_new_texto("tmp_109_onlyone_moved.txt")));
    jb_warn_aot("llamada AOT no implementada: pausa_milisegundos");
    jb_fs_borrar(jb_new_texto("tmp_109_onlyone_moved.txt"));
    jb_cleanup();
    return 0;
}
