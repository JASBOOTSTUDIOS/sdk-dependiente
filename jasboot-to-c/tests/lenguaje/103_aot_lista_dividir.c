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
    jb_var_t xs = jb_new_list();
    jb_list_push(&xs, jb_entero_a_texto(jb_new_entero(10)));
    jb_list_push(&xs, jb_entero_a_texto(jb_new_entero(20)));
    jb_imprimir(jb_entero_a_texto(jb_list_len(xs)));
    (jb_list_clear(&xs), xs);
    jb_imprimir(jb_entero_a_texto(jb_list_len(xs)));
    jb_var_t partes = jb_dividir_texto(jb_new_texto("uno,dos,tres"), jb_new_texto(","));
    jb_imprimir(jb_entero_a_texto(jb_list_len(partes)));
    jb_var_t ys = jb_new_list();
    jb_list_push(&ys, jb_entero_a_texto(jb_new_entero(7)));
    jb_imprimir(jb_entero_a_texto((jb_list_release_in_place(&ys), jb_new_entero(0))));
    jb_cleanup();
    return 0;
}
