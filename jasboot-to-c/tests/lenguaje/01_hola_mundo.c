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
    jb_imprimir(jb_new_texto("Test 1: Hola Mundo Nativo con Jasboot"));
    jb_imprimir_sin_salto(jb_new_texto("Hola Mundo"));
    jb_imprimir(jb_new_texto("Modo: Nativo"));
    jb_imprimir(jb_new_texto("Lenguaje: Jasboot"));
    jb_imprimir(jb_new_texto("Version: AOT 1.0"));
    jb_imprimir(jb_new_texto("Autor: Jasboot"));
    jb_imprimir(jb_new_texto("Fecha: 2023-08-01"));
    jb_imprimir(jb_new_texto("Descripcion: Imprime 'Hola Mundo' en la consola"));
    jb_imprimir(jb_new_texto("Autor: Jasboot"));
    jb_imprimir(jb_new_texto("Fecha: 2023-08-01"));
    jb_imprimir(jb_new_texto("Descripcion: Imprime 'Hola Mundo' en la consola"));
    jb_imprimir(jb_new_texto("Autor: Jasboot"));
    jb_imprimir(jb_new_texto("Fecha: 2023-08-01"));
    jb_imprimir(jb_new_texto("Descripcion: Imprime 'Hola Mundo' en la consola"));
    jb_imprimir(jb_new_texto("Autor: Jasboot"));
    jb_imprimir(jb_new_texto("Fecha: 2023-08-01"));
    jb_imprimir_sin_salto(jb_new_texto("Descripcion: Imprime 'Hola Mundo' en la consola"));
    jb_cleanup();
    return 0;
}
