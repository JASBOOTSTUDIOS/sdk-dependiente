#include "jbc_ir_opt.h"
#include "ir_format.h"
#include "optimizer_ir.h"
#include <stdlib.h>

uint8_t *jbc_optimize_ir_blob(const uint8_t *bin, size_t bin_len, size_t *out_len) {
    if (!bin || !out_len || bin_len < sizeof(IRHeader)) return NULL;

    IRFile *ir = ir_file_create();
    if (!ir) return NULL;
    if (ir_file_read_memory(ir, bin, bin_len) != 0) {
        ir_file_destroy(ir);
        return NULL;
    }

    IROptimizationStats stats = {0};
    if (ir_optimize(ir, &stats) != 0) {
        ir_file_destroy(ir);
        return NULL;
    }

    uint8_t *out = NULL;
    size_t ol = 0;
    if (ir_file_serialize(ir, &out, &ol) != 0 || !out || ol == 0) {
        ir_file_destroy(ir);
        return NULL;
    }
    ir_file_destroy(ir);
    *out_len = ol;
    return out;
}
