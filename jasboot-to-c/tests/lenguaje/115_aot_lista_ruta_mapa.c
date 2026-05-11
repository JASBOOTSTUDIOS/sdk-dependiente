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
    jb_var_t m = jb_new_map();
    jb_var_t externa = jb_new_list();
    jb_map_put(&m, jb_new_texto("L"), externa);
    ({ jb_var_t __jb_lp0 = jb_map_get(m, jb_new_texto("L")); jb_list_push(&__jb_lp0, jb_new_entero(10)); jb_map_put(&m, jb_new_texto("L"), __jb_lp0); jb_var_clear(&__jb_lp0); jb_new_nulo(); });
    ({ jb_var_t __jb_lp0 = jb_map_get(m, jb_new_texto("L")); jb_list_push(&__jb_lp0, jb_new_entero(20)); jb_map_put(&m, jb_new_texto("L"), __jb_lp0); jb_var_clear(&__jb_lp0); jb_new_nulo(); });
    jb_imprimir(jb_list_len(jb_member_get(m, "L")));
    ({ jb_var_t __jb_lp0 = jb_map_get(m, jb_new_texto("L")); jb_list_set(&__jb_lp0, jb_new_entero(0), jb_new_entero(99)); jb_map_put(&m, jb_new_texto("L"), __jb_lp0); jb_var_clear(&__jb_lp0); jb_new_nulo(); });
    jb_imprimir(jb_list_get(jb_member_get(m, "L"), jb_new_entero(0)));
    jb_cleanup();
    return 0;
}
