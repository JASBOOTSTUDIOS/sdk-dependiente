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
    jb_var_t s = jb_new_texto("abcdef");
    jb_imprimir(jb_extraer_subtexto(s, jb_new_entero(2), jb_new_entero(3)));
    jb_imprimir(jb_contiene_texto(s, jb_new_texto("bcd")));
    jb_imprimir(jb_termina_con(jb_new_texto("foto.png"), jb_new_texto(".png")));
    jb_imprimir(jb_texto_len(jb_new_texto("abc")));
    jb_var_t t = jb_texto_desde_numero(jb_new_entero(42));
    jb_imprimir(t);
    jb_var_t m = jb_new_map();
    jb_map_put(&m, jb_new_texto("k"), jb_entero_a_texto(jb_new_entero(1)));
    jb_map_put(&m, jb_new_texto("S"), jb_entero_a_texto(jb_new_entero(55)));
    jb_imprimir(m);
    jb_map_remove(&m, jb_new_texto("k"));
    jb_imprimir(m);
    jb_imprimir(jb_entero_a_texto(jb_map_len(m)));
    jb_cleanup();
    return 0;
}
