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
    jb_var_t vector_c = jb_new_vec3_from_jbvals(jb_new_flotante(3), jb_new_flotante(4), jb_new_flotante(0));
    jb_imprimir(jb_new_texto("vec_mat_ok"));
    jb_var_t len = jb_vec_longitud(vector_c);
    jb_imprimir_flotante(len);
    jb_var_t eje = jb_new_vec3_from_jbvals(jb_new_flotante(1), jb_new_flotante(0), jb_new_flotante(0));
    jb_var_t u = jb_new_vec3_from_jbvals(jb_new_flotante(0), jb_new_flotante(0), jb_new_flotante(0));
    jb_vec3_normalizar(&u, &eje);
    jb_imprimir_flotante(jb_member_get(u, "x"));
    jb_imprimir_flotante(jb_member_get(u, "y"));
    jb_var_t M = jb_new_mat4_zero();
    jb_mat4_identidad(&M);
    jb_var_t v = jb_new_vec4_from_jbvals(jb_new_flotante(1), jb_new_flotante(0), jb_new_flotante(0), jb_new_flotante(1));
    jb_var_t sal = jb_new_vec4_from_jbvals(jb_new_flotante(0), jb_new_flotante(0), jb_new_flotante(0), jb_new_flotante(0));
    jb_mat4_mul_vec4(&sal, &M, &v);
    jb_imprimir_flotante(jb_member_get(sal, "x"));
    jb_imprimir_flotante(jb_member_get(sal, "w"));
    jb_cleanup();
    return 0;
}
