#include "optimizer_ir.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t known[IR_REGISTER_COUNT];
    uint64_t value[IR_REGISTER_COUNT];
} ConstState;

static int inst_is_control_flow(const IRInstruction* inst) {
    return inst->opcode == OP_IR || inst->opcode == OP_SI ||
           inst->opcode == OP_LLAMAR || inst->opcode == OP_RETORNAR;
}

static int inst_has_side_effects(const IRInstruction* inst) {
    return inst->opcode == OP_ESCRIBIR || inst->opcode == OP_OBSERVAR ||
           inst->opcode == OP_MARCAR_ESTADO || inst_is_control_flow(inst);
}

static void inst_to_nop(IRInstruction* inst) {
    inst->opcode = OP_NOP;
    inst->flags = 0;
    inst->operand_a = 0;
    inst->operand_b = 0;
    inst->operand_c = 0;
}

static void const_state_reset(ConstState* st) {
    memset(st->known, 0, sizeof(st->known));
    st->known[0] = 1;
    st->value[0] = 0;
}

static int get_operand_value(const IRInstruction* inst, int operand, int flag, const ConstState* st, uint64_t* out) {
    if (inst->flags & flag) {
        *out = (uint64_t)(uint8_t)operand;
        return 1;
    }
    if (st->known[operand]) {
        *out = st->value[operand];
        return 1;
    }
    return 0;
}

static int ir_encode_addr16(size_t offset, uint8_t* out_low, uint8_t* out_high, uint8_t* out_flags) {
    if (!out_low || !out_high || !out_flags) return -1;
    if (offset > 0xFFFF) return -1;
    *out_low = (uint8_t)(offset & 0xFF);
    *out_high = (uint8_t)((offset >> 8) & 0xFF);
    *out_flags = IR_INST_FLAG_B_IMMEDIATE;
    if (*out_high != 0) {
        *out_flags |= IR_INST_FLAG_C_IMMEDIATE;
    }
    return 0;
}

static int fold_to_literal(IRFile* ir, IRInstruction* inst, uint64_t value, uint8_t dest_reg, IROptimizationStats* stats) {
    if (value <= 0xFF) {
        inst->opcode = OP_MOVER;
        inst->flags = IR_INST_FLAG_B_IMMEDIATE;
        inst->operand_a = dest_reg;
        inst->operand_b = (uint8_t)value;
        inst->operand_c = 0;
        if (stats) stats->constantes_plegadas++;
        return 0;
    }
    
    size_t offset = 0;
    if (ir_file_add_u64(ir, value, &offset) != 0) {
        return -1;
    }
    
    uint8_t low = 0;
    uint8_t high = 0;
    uint8_t flags = 0;
    if (ir_encode_addr16(offset, &low, &high, &flags) != 0) {
        return -1;
    }
    
    inst->opcode = OP_LEER;
    inst->flags = flags;
    inst->operand_a = dest_reg;
    inst->operand_b = low;
    inst->operand_c = high;
    if (stats) stats->constantes_plegadas++;
    return 0;
}

static void try_strength_reduce(IRInstruction* inst, uint64_t lhs, uint64_t rhs, int lhs_known, int rhs_known, IROptimizationStats* stats) {
    (void)lhs_known;
    (void)rhs_known;
    switch (inst->opcode) {
        case OP_SUMAR:
            if (rhs_known && rhs == 0) {
                inst->opcode = OP_MOVER;
                inst->flags = (inst->flags & IR_INST_FLAG_B_IMMEDIATE) ? IR_INST_FLAG_B_IMMEDIATE : 0;
                inst->operand_b = inst->operand_b;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            } else if (lhs_known && lhs == 0) {
                inst->opcode = OP_MOVER;
                inst->flags = (inst->flags & IR_INST_FLAG_C_IMMEDIATE) ? IR_INST_FLAG_B_IMMEDIATE : 0;
                inst->operand_b = inst->operand_c;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            }
            break;
        case OP_RESTAR:
            if (rhs_known && rhs == 0) {
                inst->opcode = OP_MOVER;
                inst->flags = (inst->flags & IR_INST_FLAG_B_IMMEDIATE) ? IR_INST_FLAG_B_IMMEDIATE : 0;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            }
            break;
        case OP_MULTIPLICAR:
            if ((rhs_known && rhs == 0) || (lhs_known && lhs == 0)) {
                inst->opcode = OP_MOVER;
                inst->flags = IR_INST_FLAG_B_IMMEDIATE;
                inst->operand_b = 0;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            } else if (rhs_known && rhs == 1) {
                inst->opcode = OP_MOVER;
                inst->flags = (inst->flags & IR_INST_FLAG_B_IMMEDIATE) ? IR_INST_FLAG_B_IMMEDIATE : 0;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            } else if (lhs_known && lhs == 1) {
                inst->opcode = OP_MOVER;
                inst->flags = (inst->flags & IR_INST_FLAG_C_IMMEDIATE) ? IR_INST_FLAG_B_IMMEDIATE : 0;
                inst->operand_b = inst->operand_c;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            }
            break;
        case OP_DIVIDIR:
            if (rhs_known && rhs == 1) {
                inst->opcode = OP_MOVER;
                inst->flags = (inst->flags & IR_INST_FLAG_B_IMMEDIATE) ? IR_INST_FLAG_B_IMMEDIATE : 0;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            }
            break;
        case OP_XOR:
            if (!(inst->flags & IR_INST_FLAG_B_IMMEDIATE) &&
                !(inst->flags & IR_INST_FLAG_C_IMMEDIATE) &&
                inst->operand_b == inst->operand_c) {
                inst->opcode = OP_MOVER;
                inst->flags = IR_INST_FLAG_B_IMMEDIATE;
                inst->operand_b = 0;
                inst->operand_c = 0;
                if (stats) stats->saltos_simplificados++;
            }
            break;
        default:
            break;
    }
}

static int try_propagate_immediate(IRInstruction* inst, uint8_t operand, uint8_t flag, const ConstState* st, IROptimizationStats* stats) {
    if (inst->flags & flag) return 0;
    if (!st->known[operand]) return 0;
    if (st->value[operand] > 0xFF) return 0;
    
    if (flag == IR_INST_FLAG_B_IMMEDIATE) {
        inst->operand_b = (uint8_t)st->value[operand];
    } else if (flag == IR_INST_FLAG_C_IMMEDIATE) {
        inst->operand_c = (uint8_t)st->value[operand];
    } else if (flag == IR_INST_FLAG_A_IMMEDIATE) {
        inst->operand_a = (uint8_t)st->value[operand];
    }
    inst->flags |= flag;
    if (stats) stats->inmediatos_prop++;
    return 1;
}

static void analyze_block(IRFile* ir, IRInstruction* insts, size_t start, size_t end, uint8_t* removed, IROptimizationStats* stats) {
    ConstState st;
    const_state_reset(&st);
    
    for (size_t i = start; i <= end; i++) {
        IRInstruction* inst = &insts[i];
        if (removed[i]) continue;
        
        switch (inst->opcode) {
            case OP_MOVER: {
                if (inst->flags & IR_INST_FLAG_B_IMMEDIATE) {
                    uint64_t value = inst->operand_b;
                    if (st.known[inst->operand_a] && st.value[inst->operand_a] == value) {
                        inst_to_nop(inst);
                        removed[i] = 1;
                        if (stats) stats->nops_eliminados++;
                    } else {
                        st.known[inst->operand_a] = 1;
                        st.value[inst->operand_a] = value;
                    }
                } else {
                    uint8_t src = inst->operand_b;
                    if (src == inst->operand_a) {
                        inst_to_nop(inst);
                        removed[i] = 1;
                        if (stats) stats->nops_eliminados++;
                    } else if (st.known[src] && st.value[src] <= 0xFF) {
                        inst->flags |= IR_INST_FLAG_B_IMMEDIATE;
                        inst->operand_b = (uint8_t)st.value[src];
                        st.known[inst->operand_a] = 1;
                        st.value[inst->operand_a] = st.value[src];
                        if (stats) stats->inmediatos_prop++;
                    } else if (st.known[src]) {
                        st.known[inst->operand_a] = 1;
                        st.value[inst->operand_a] = st.value[src];
                    } else {
                        st.known[inst->operand_a] = 0;
                    }
                }
                break;
            }
            case OP_SUMAR:
            case OP_RESTAR:
            case OP_MULTIPLICAR:
            case OP_DIVIDIR:
            case OP_MODULO:
            case OP_Y:
            case OP_O:
            case OP_XOR:
            case OP_COMPARAR:
            case OP_CMP_EQ:
            case OP_CMP_LT:
            case OP_CMP_GT:
            case OP_CMP_LE:
            case OP_CMP_GE: {
                uint64_t lhs = 0;
                uint64_t rhs = 0;
                int lhs_known = get_operand_value(inst, inst->operand_b, IR_INST_FLAG_B_IMMEDIATE, &st, &lhs);
                int rhs_known = get_operand_value(inst, inst->operand_c, IR_INST_FLAG_C_IMMEDIATE, &st, &rhs);
                
                if (!lhs_known) {
                    try_propagate_immediate(inst, inst->operand_b, IR_INST_FLAG_B_IMMEDIATE, &st, stats);
                    lhs_known = get_operand_value(inst, inst->operand_b, IR_INST_FLAG_B_IMMEDIATE, &st, &lhs);
                }
                if (!rhs_known) {
                    try_propagate_immediate(inst, inst->operand_c, IR_INST_FLAG_C_IMMEDIATE, &st, stats);
                    rhs_known = get_operand_value(inst, inst->operand_c, IR_INST_FLAG_C_IMMEDIATE, &st, &rhs);
                }
                
                if (lhs_known && rhs_known) {
                    uint64_t result = 0;
                    if (inst->opcode == OP_SUMAR) result = lhs + rhs;
                    else if (inst->opcode == OP_RESTAR) result = lhs - rhs;
                    else if (inst->opcode == OP_MULTIPLICAR) result = lhs * rhs;
                    else if (inst->opcode == OP_DIVIDIR) {
                        if (rhs == 0) {
                            st.known[inst->operand_a] = 0;
                            break;
                        }
                        result = lhs / rhs;
                    } else if (inst->opcode == OP_MODULO) {
                        if (rhs == 0) {
                            st.known[inst->operand_a] = 0;
                            break;
                        }
                        result = lhs % rhs;
                    } else if (inst->opcode == OP_Y) result = lhs & rhs;
                    else if (inst->opcode == OP_O) result = lhs | rhs;
                    else if (inst->opcode == OP_XOR) result = lhs ^ rhs;
                    else if (inst->opcode == OP_COMPARAR) {
                        if (lhs < rhs) result = 2;
                        else if (lhs > rhs) result = 3;
                        else result = 0;
                    } else if (inst->opcode == OP_CMP_EQ) result = (lhs == rhs) ? 1 : 0;
                    else if (inst->opcode == OP_CMP_LT) result = ((int64_t)lhs < (int64_t)rhs) ? 1 : 0;
                    else if (inst->opcode == OP_CMP_GT) result = ((int64_t)lhs > (int64_t)rhs) ? 1 : 0;
                    else if (inst->opcode == OP_CMP_LE) result = ((int64_t)lhs <= (int64_t)rhs) ? 1 : 0;
                    else if (inst->opcode == OP_CMP_GE) result = ((int64_t)lhs >= (int64_t)rhs) ? 1 : 0;
                    
                    if (fold_to_literal(ir, inst, result, inst->operand_a, stats) == 0) {
                        st.known[inst->operand_a] = 1;
                        st.value[inst->operand_a] = result;
                    } else {
                        st.known[inst->operand_a] = 0;
                    }
                } else {
                    try_strength_reduce(inst, lhs, rhs, lhs_known, rhs_known, stats);
                    st.known[inst->operand_a] = 0;
                }
                break;
            }
            case OP_NO: {
                uint64_t val = 0;
                int val_known = get_operand_value(inst, inst->operand_b, IR_INST_FLAG_B_IMMEDIATE, &st, &val);
                if (!val_known) {
                    try_propagate_immediate(inst, inst->operand_b, IR_INST_FLAG_B_IMMEDIATE, &st, stats);
                    val_known = get_operand_value(inst, inst->operand_b, IR_INST_FLAG_B_IMMEDIATE, &st, &val);
                }
                if (val_known) {
                    uint64_t result = ~val;
                    if (fold_to_literal(ir, inst, result, inst->operand_a, stats) == 0) {
                        st.known[inst->operand_a] = 1;
                        st.value[inst->operand_a] = result;
                    } else {
                        st.known[inst->operand_a] = 0;
                    }
                } else {
                    st.known[inst->operand_a] = 0;
                }
                break;
            }
            case OP_SI: {
                uint64_t cond = 0;
                int cond_known = get_operand_value(inst, inst->operand_a, IR_INST_FLAG_A_IMMEDIATE, &st, &cond);
                if (cond_known) {
                    if (cond == 0) {
                        inst_to_nop(inst);
                        removed[i] = 1;
                        if (stats) stats->saltos_simplificados++;
                    } else {
                        IRInstruction reemplazo = *inst;
                        reemplazo.opcode = OP_IR;
                        reemplazo.operand_a = inst->operand_b;
                        reemplazo.operand_b = 0;
                        reemplazo.operand_c = 0;
                        reemplazo.flags = (inst->flags & IR_INST_FLAG_RELATIVE) | IR_INST_FLAG_A_IMMEDIATE;
                        *inst = reemplazo;
                        if (stats) stats->saltos_simplificados++;
                    }
                }
                break;
            }
            case OP_LEER:
            case OP_ESCRIBIR:
            case OP_OBSERVAR:
            case OP_MARCAR_ESTADO:
            case OP_IR:
            case OP_LLAMAR:
            case OP_RETORNAR:
            case OP_NOP:
            default:
                break;
        }
        
    }
    
    uint8_t live[IR_REGISTER_COUNT];
    int has_branch = inst_is_control_flow(&insts[end]) && insts[end].opcode != OP_RETORNAR;
    if (has_branch) {
        memset(live, 1, sizeof(live));
        live[0] = 1;
    } else {
        memset(live, 0, sizeof(live));
        live[0] = 1;
    }
    
    for (size_t idx = end + 1; idx-- > start; ) {
        IRInstruction* inst = &insts[idx];
        if (removed[idx]) continue;
        
        int defines_reg = 0;
        uint8_t def_reg = 0;
        
        switch (inst->opcode) {
            case OP_MOVER:
            case OP_LEER:
            case OP_SUMAR:
            case OP_RESTAR:
            case OP_MULTIPLICAR:
            case OP_DIVIDIR:
            case OP_MODULO:
            case OP_Y:
            case OP_O:
            case OP_XOR:
            case OP_NO:
            case OP_COMPARAR:
            case OP_CMP_EQ:
            case OP_CMP_LT:
            case OP_CMP_GT:
            case OP_CMP_LE:
            case OP_CMP_GE:
                defines_reg = 1;
                def_reg = inst->operand_a;
                break;
            default:
                break;
        }
        
        if (defines_reg && !inst_has_side_effects(inst) && !live[def_reg]) {
            inst_to_nop(inst);
            removed[idx] = 1;
            if (stats) stats->dce_eliminados++;
            continue;
        }
        
        if (defines_reg) {
            live[def_reg] = 0;
        }
        
        if (inst->opcode == OP_MOVER) {
            if (!(inst->flags & IR_INST_FLAG_B_IMMEDIATE)) live[inst->operand_b] = 1;
        } else if (inst->opcode == OP_LEER) {
            if (!(inst->flags & IR_INST_FLAG_B_IMMEDIATE)) live[inst->operand_b] = 1;
        } else if (inst->opcode == OP_ESCRIBIR) {
            if (!(inst->flags & IR_INST_FLAG_A_IMMEDIATE)) live[inst->operand_a] = 1;
            if (!(inst->flags & IR_INST_FLAG_B_IMMEDIATE)) live[inst->operand_b] = 1;
        } else if (inst->opcode == OP_SUMAR || inst->opcode == OP_RESTAR ||
                   inst->opcode == OP_MULTIPLICAR || inst->opcode == OP_DIVIDIR ||
                   inst->opcode == OP_MODULO ||
                   inst->opcode == OP_Y || inst->opcode == OP_O ||
                   inst->opcode == OP_XOR || inst->opcode == OP_COMPARAR ||
                   inst->opcode == OP_CMP_EQ || inst->opcode == OP_CMP_LT ||
                   inst->opcode == OP_CMP_GT || inst->opcode == OP_CMP_LE ||
                   inst->opcode == OP_CMP_GE) {
            if (!(inst->flags & IR_INST_FLAG_B_IMMEDIATE)) live[inst->operand_b] = 1;
            if (!(inst->flags & IR_INST_FLAG_C_IMMEDIATE)) live[inst->operand_c] = 1;
        } else if (inst->opcode == OP_NO) {
            if (!(inst->flags & IR_INST_FLAG_B_IMMEDIATE)) live[inst->operand_b] = 1;
        } else if (inst->opcode == OP_SI) {
            if (!(inst->flags & IR_INST_FLAG_A_IMMEDIATE)) live[inst->operand_a] = 1;
        } else if (inst->opcode == OP_IR || inst->opcode == OP_LLAMAR) {
            if (!(inst->flags & IR_INST_FLAG_A_IMMEDIATE)) live[inst->operand_a] = 1;
        } else if (inst->opcode == OP_OBSERVAR) {
            if (!(inst->flags & IR_INST_FLAG_A_IMMEDIATE)) live[inst->operand_a] = 1;
        }
    }
}

static int rebuild_code(IRFile* ir, IRInstruction* insts, size_t count) {
    size_t needed = count * IR_INSTRUCTION_SIZE;
    if (needed > ir->code_capacity) {
        uint8_t* new_code = realloc(ir->code, needed);
        if (!new_code) return -1;
        ir->code = new_code;
        ir->code_capacity = needed;
    }
    
    for (size_t i = 0; i < count; i++) {
        size_t offset = i * IR_INSTRUCTION_SIZE;
        ir->code[offset + 0] = insts[i].opcode;
        ir->code[offset + 1] = insts[i].flags;
        ir->code[offset + 2] = insts[i].operand_a;
        ir->code[offset + 3] = insts[i].operand_b;
        ir->code[offset + 4] = insts[i].operand_c;
    }
    
    ir->code_count = count;
    ir->header.code_size = (uint32_t)(count * IR_INSTRUCTION_SIZE);
    return 0;
}

int ir_optimize(IRFile* ir, IROptimizationStats* stats) {
    if (!ir || !ir->code || ir->code_count == 0) return -1;
    
    if (stats) {
        memset(stats, 0, sizeof(*stats));
        stats->instrucciones_originales = ir->code_count;
    }
    
    size_t count = ir->code_count;
    IRInstruction* insts = calloc(count, sizeof(IRInstruction));
    uint8_t* removed = calloc(count, sizeof(uint8_t));
    if (!insts || !removed) {
        free(insts);
        free(removed);
        return -1;
    }
    
    for (size_t i = 0; i < count; i++) {
        size_t offset = i * IR_INSTRUCTION_SIZE;
        insts[i].opcode = ir->code[offset + 0];
        insts[i].flags = ir->code[offset + 1];
        insts[i].operand_a = ir->code[offset + 2];
        insts[i].operand_b = ir->code[offset + 3];
        insts[i].operand_c = ir->code[offset + 4];
    }
    
    size_t start = 0;
    for (size_t i = 0; i < count; i++) {
        if (inst_is_control_flow(&insts[i]) || i == count - 1) {
            size_t end = (inst_is_control_flow(&insts[i]) ? i : i);
            analyze_block(ir, insts, start, end, removed, stats);
            start = i + 1;
        }
    }
    
    size_t removed_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (insts[i].opcode == OP_NOP && !removed[i]) {
            removed[i] = 1;
        }
        if (removed[i]) removed_count++;
    }
    
    if (removed_count == 0) {
        int result = rebuild_code(ir, insts, count);
        if (stats) {
            stats->instrucciones_finales = ir->code_count;
            stats->compactacion_exitosa = 0;
        }
        free(insts);
        free(removed);
        return result;
    }
    
    size_t* map = calloc(count, sizeof(size_t));
    size_t* old_index_for_new = calloc(count, sizeof(size_t));
    if (!map || !old_index_for_new) {
        free(insts);
        free(removed);
        free(map);
        free(old_index_for_new);
        return -1;
    }
    
    size_t new_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (!removed[i]) {
            map[i] = new_count;
            old_index_for_new[new_count] = i;
            new_count++;
        }
    }
    
    if (new_count == 0) {
        ir->code_count = 0;
        ir->header.code_size = 0;
        if (stats) {
            stats->instrucciones_finales = 0;
            stats->compactacion_exitosa = 1;
        }
        free(insts);
        free(removed);
        free(map);
        free(old_index_for_new);
        return 0;
    }
    
    size_t last_kept = new_count - 1;
    for (size_t i = 0; i < count; i++) {
        if (removed[i]) {
            size_t j = i + 1;
            while (j < count && removed[j]) j++;
            if (j < count) map[i] = map[j];
            else map[i] = last_kept;
        }
    }
    
    IRInstruction* compacted = calloc(new_count, sizeof(IRInstruction));
    if (!compacted) {
        free(insts);
        free(removed);
        free(map);
        free(old_index_for_new);
        return -1;
    }
    
    for (size_t i = 0; i < new_count; i++) {
        compacted[i] = insts[old_index_for_new[i]];
    }
    
    int compact_ok = 1;
    for (size_t i = 0; i < new_count; i++) {
        IRInstruction* inst = &compacted[i];
        size_t old_index = old_index_for_new[i];
        
        if (inst->opcode == OP_IR || inst->opcode == OP_LLAMAR || inst->opcode == OP_SI) {
            uint8_t target = 0;
            int uses_b = (inst->opcode == OP_SI);
            if (inst->flags & IR_INST_FLAG_RELATIVE) {
                target = uses_b ? inst->operand_b : inst->operand_a;
                size_t old_pc = old_index * IR_INSTRUCTION_SIZE;
                size_t target_pc = old_pc + target;
                if (target_pc % IR_INSTRUCTION_SIZE != 0) {
                    compact_ok = 0;
                    break;
                }
                size_t target_index = target_pc / IR_INSTRUCTION_SIZE;
                if (target_index >= count) {
                    compact_ok = 0;
                    break;
                }
                size_t new_target_index = map[target_index];
                size_t new_pc = i * IR_INSTRUCTION_SIZE;
                size_t new_target_pc = new_target_index * IR_INSTRUCTION_SIZE;
                if (new_target_pc < new_pc || (new_target_pc - new_pc) > 0xFF) {
                    compact_ok = 0;
                    break;
                }
                uint8_t new_offset = (uint8_t)(new_target_pc - new_pc);
                if (uses_b) inst->operand_b = new_offset;
                else inst->operand_a = new_offset;
            } else {
                target = uses_b ? inst->operand_b : inst->operand_a;
                if (target % IR_INSTRUCTION_SIZE != 0) {
                    compact_ok = 0;
                    break;
                }
                size_t target_index = target / IR_INSTRUCTION_SIZE;
                if (target_index >= count) {
                    compact_ok = 0;
                    break;
                }
                size_t new_target_index = map[target_index];
                size_t new_target_pc = new_target_index * IR_INSTRUCTION_SIZE;
                if (new_target_pc > 0xFF) {
                    compact_ok = 0;
                    break;
                }
                uint8_t new_target = (uint8_t)new_target_pc;
                if (uses_b) inst->operand_b = new_target;
                else inst->operand_a = new_target;
            }
        }
    }
    
    int result = 0;
    if (compact_ok) {
        result = rebuild_code(ir, compacted, new_count);
        if (stats) {
            stats->instrucciones_finales = ir->code_count;
            stats->compactacion_exitosa = 1;
        }
    } else {
        result = rebuild_code(ir, insts, count);
        if (stats) {
            stats->instrucciones_finales = ir->code_count;
            stats->compactacion_exitosa = 0;
        }
    }
    
    free(compacted);
    free(insts);
    free(removed);
    free(map);
    free(old_index_for_new);
    return result;
}
