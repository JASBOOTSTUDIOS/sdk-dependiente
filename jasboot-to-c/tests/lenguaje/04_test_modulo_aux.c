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

static jb_var_t jbf_multiplicar_dos(jb_var_t num_a, jb_var_t num_b);

static jb_var_t jbf_multiplicar_dos(jb_var_t num_a, jb_var_t num_b) {
    return jb_mul(num_a, num_b);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

int main(int argc, char **argv) {
    jb_init();
    jb_set_argv(argc, argv);
    jb_cleanup();
    return 0;
}
