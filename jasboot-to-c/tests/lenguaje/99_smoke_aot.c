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
    jb_var_t x1 = jb_new_entero(3);
    jb_var_t x2 = jb_new_entero(4);
    jb_imprimir(jb_concat(jb_new_texto("suma = "), jb_entero_a_texto(jb_add(x1, x2))));
    jb_var_t xs = jb_new_list();
    jb_list_push(&xs, jb_entero_a_texto(jb_new_entero(1)));
    jb_list_push(&xs, jb_entero_a_texto(jb_new_entero(2)));
    jb_imprimir(jb_concat(jb_new_texto("len = "), jb_entero_a_texto(jb_list_len(xs))));
    {
        jb_try_depth++;
        if (setjmp(jb_try_stack[jb_try_depth - 1]) == 0) {
            jb_var_t z = jb_div(jb_new_entero(10), jb_new_entero(0));
            jb_imprimir(jb_entero_a_texto(z));
        jb_try_depth--;
        } else {
            jb_var_t err = jb_var_clone(g_last_throw);
            jb_imprimir(jb_concat(jb_new_texto("error = "), err));
            jb_imprimir(jb_new_texto("atrapado"));
            jb_try_depth--;
        }
    }
    jb_cleanup();
    return 0;
}
