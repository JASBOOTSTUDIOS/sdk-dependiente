#ifndef JBC_IR_OPT_H
#define JBC_IR_OPT_H

#include <stddef.h>
#include <stdint.h>

/* Pasa el .jbo emitido por codegen por ir_optimize (jasboot-ir).
 * Devuelve nuevo buffer (malloc) y longitud, o NULL si no se aplica / falla. */
uint8_t *jbc_optimize_ir_blob(const uint8_t *bin, size_t bin_len, size_t *out_len);

#endif
