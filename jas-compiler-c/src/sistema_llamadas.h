/* 2.16 SISTEMA_LLAMADAS - Nombres reconocidos como llamadas de sistema */

#ifndef SISTEMA_LLAMADAS_H
#define SISTEMA_LLAMADAS_H

#include <stddef.h>

extern const char *const SISTEMA_LLAMADAS[];
extern const size_t SISTEMA_LLAMADAS_COUNT;

int is_sistema_llamada(const char *name, size_t len);

#endif
