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
    jb_var_t raiz = jb_new_map();
    jb_var_t nivel1 = jb_new_map();
    jb_map_put(&raiz, jb_new_texto("m"), nivel1);
    jb_var_t nivel2 = jb_new_map();
    ({ jb_var_t __jb_mp0 = jb_map_get(raiz, jb_new_texto("m")); jb_map_put(&__jb_mp0, jb_new_texto("n"), nivel2); jb_map_put(&raiz, jb_new_texto("m"), __jb_mp0); jb_var_clear(&__jb_mp0); jb_new_nulo(); });
    ({ jb_var_t __jb_as0 = jb_map_get(raiz, jb_new_texto("m")); jb_var_t __jb_as1 = jb_map_get(__jb_as0, jb_new_texto("n")); jb_map_put(&__jb_as1, jb_new_texto("val"), jb_new_entero(9)); jb_map_put(&__jb_as0, jb_new_texto("n"), __jb_as1); jb_map_put(&raiz, jb_new_texto("m"), __jb_as0); jb_var_clear(&__jb_as0); jb_var_clear(&__jb_as1); jb_new_nulo(); });
    jb_imprimir(jb_texto_desde_numero(jb_map_get(jb_map_get(jb_map_get(raiz, jb_new_texto("m")), jb_new_texto("n")), jb_new_texto("val"))));
    jb_cleanup();
    return 0;
}
