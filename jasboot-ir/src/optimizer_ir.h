#ifndef OPTIMIZER_IR_H
#define OPTIMIZER_IR_H

#include "ir_format.h"
#include <stddef.h>

typedef struct {
    size_t instrucciones_originales;
    size_t instrucciones_finales;
    size_t nops_eliminados;
    size_t dce_eliminados;
    size_t constantes_plegadas;
    size_t inmediatos_prop;
    size_t saltos_simplificados;
    size_t compactacion_exitosa;
} IROptimizationStats;

int ir_optimize(IRFile* ir, IROptimizationStats* stats);

#endif // OPTIMIZER_IR_H
