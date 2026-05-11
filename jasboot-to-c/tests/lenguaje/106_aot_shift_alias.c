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
    jb_var_t x = jb_new_entero(2);
    jb_var_t n = jb_new_entero(3);
    jb_imprimir(jb_texto_desde_numero(jb_bit_shl(x, n)));
    jb_imprimir(jb_texto_desde_numero(jb_bit_shr(jb_new_entero(64), jb_new_entero(3))));
    jb_var_t pal = jb_dividir_texto(jb_new_texto("uno dos tres"), jb_new_texto(" "));
    jb_imprimir(jb_texto_desde_numero(jb_list_len(pal)));
    jb_var_t q = jb_dividir_texto(jb_new_texto("a:b"), jb_new_texto(":"));
    jb_imprimir(jb_texto_desde_numero(jb_list_len(q)));
    jb_cleanup();
    return 0;
}
