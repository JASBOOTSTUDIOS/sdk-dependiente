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
    jb_var_t raw = jb_new_texto("{\"a\":1,\"b\":[2,3],\"c\":\"x\"}");
    jb_var_t j = jb_json_parse(raw);
    jb_imprimir(jb_json_tipo(j));
    jb_var_t ja = jb_json_objeto_obtener(j, jb_new_texto("a"));
    jb_imprimir(jb_json_a_entero(ja));
    jb_var_t jarr = jb_json_objeto_obtener(j, jb_new_texto("b"));
    jb_imprimir(jb_json_tipo(jarr));
    jb_imprimir(jb_json_lista_tamano(jarr));
    jb_var_t j0 = jb_json_lista_obtener(jarr, jb_new_entero(0));
    jb_imprimir(jb_json_a_entero(j0));
    jb_var_t ser = jb_json_stringify(j, jb_new_entero(0));
    jb_imprimir(jb_entero_a_texto(jb_texto_len(ser)));
    jb_cleanup();
    return 0;
}
