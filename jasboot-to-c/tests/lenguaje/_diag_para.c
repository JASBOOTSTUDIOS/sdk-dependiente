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
    jb_var_t idx = jb_new_nulo();
    for (    jb_assign(&idx, jb_new_entero(0));
jb_lt(idx, jb_new_entero(3));     jb_warn_aot("sentencia AOT tipo=44");
) {
        jb_imprimir(idx);
    }
    jb_cleanup();
    return 0;
}
