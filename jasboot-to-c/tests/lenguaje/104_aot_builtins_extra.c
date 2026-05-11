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
    jb_imprimir(jb_bit_shl(jb_new_entero(1), jb_new_entero(4)));
    jb_imprimir(jb_bit_shr(jb_new_entero(16), jb_new_entero(4)));
    jb_var_t s = jb_new_texto("abcdef");
    jb_imprimir(jb_extraer_subtexto(s, jb_new_entero(2), jb_sub(jb_texto_len(s), jb_new_entero(2))));
    jb_imprimir(jb_codigo_caracter(jb_new_texto("A")));
    jb_imprimir(jb_caracter_a_texto(jb_new_entero(65)));
    jb_imprimir(jb_str_extraer_caracter(jb_new_texto("hi"), jb_new_entero(0)));
    jb_var_t u = jb_copiar_texto(jb_new_texto("z"));
    jb_imprimir(u);
    jb_var_t cien = jb_new_flotante(100);
    jb_imprimir(jb_texto_desde_numero(jb_exp(jb_new_entero(0))));
    jb_imprimir(jb_texto_desde_numero(jb_log10(cien)));
    jb_cleanup();
    return 0;
}
