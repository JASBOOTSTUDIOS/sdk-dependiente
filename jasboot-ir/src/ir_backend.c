#include "ir_format.h"
#include "reader_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void emitir_bytes(FILE* f, const uint8_t* data, size_t size) {
    if (!f || !data || size == 0) return;
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            fprintf(f, "    db ");
        }
        fprintf(f, "0x%02X", data[i]);
        if ((i % 16) == 15 || i + 1 == size) {
            fprintf(f, "\n");
        } else {
            fprintf(f, ", ");
        }
    }
}

static void emitir_cadena_data(FILE* f, const char* nombre, const char* texto) {
    fprintf(f, "%s db \"%s\", 0\n", nombre, texto);
}

static void emitir_load_reg(FILE* f, const char* reg_dst, uint8_t reg_idx) {
    fprintf(f, "    mov %s, [regs + %u*8]\n", reg_dst, (unsigned)reg_idx);
}

static void emitir_store_reg(FILE* f, uint8_t reg_idx, const char* reg_src) {
    if (reg_idx == 0) {
        return;
    }
    fprintf(f, "    mov [regs + %u*8], %s\n", (unsigned)reg_idx, reg_src);
}

static void emitir_load_operand(FILE* f, const char* reg_dst, uint8_t operand, uint8_t flags, uint8_t flag_mask) {
    if (flags & flag_mask) {
        fprintf(f, "    mov %s, %u\n", reg_dst, (unsigned)operand);
    } else {
        emitir_load_reg(f, reg_dst, operand);
    }
}

static void emitir_jump_indirecto(FILE* f, const char* reg_idx) {
    fprintf(f, "    jmp [jump_table + %s*8]\n", reg_idx);
}

static void emitir_jump_inmediato(FILE* f, size_t target_idx, size_t inst_count) {
    if (target_idx == inst_count) {
        fprintf(f, "    jmp finish\n");
    } else if (target_idx > inst_count) {
        fprintf(f, "    jmp finish_error\n");
    } else {
        fprintf(f, "    jmp inst_%zu\n", target_idx);
    }
}

static void emitir_dividir_por_5(FILE* f, const char* reg_inout) {
    fprintf(f, "    mov rax, %s\n", reg_inout);
    fprintf(f, "    xor rdx, rdx\n");
    fprintf(f, "    mov rcx, 5\n");
    fprintf(f, "    div rcx\n");
    fprintf(f, "    mov %s, rax\n", reg_inout);
}

/* Subconjunto Tier-1 pensado para AOT/JIT inicial: aritmética, memoria local, saltos y control simple.
 * Cualquier opcode fuera de este grupo debe seguir usando fallback al intérprete hasta tener semántica exacta.
 */
static int opcode_soportado_aot_tier1(uint8_t opcode) {
    switch (opcode) {
        case OP_MOVER:
        case OP_LEER:
        case OP_ESCRIBIR:
        case OP_SUMAR:
        case OP_RESTAR:
        case OP_MULTIPLICAR:
        case OP_DIVIDIR:
        case OP_Y:
        case OP_O:
        case OP_XOR:
        case OP_NO:
        case OP_COMPARAR:
        case OP_IR:
        case OP_SI:
        case OP_LLAMAR:
        case OP_RETORNAR:
        case OP_OBSERVAR:
        case OP_IMPRIMIR_TEXTO:
        case OP_MEM_RECORDAR_TEXTO:
        case OP_MEM_BUSCAR_PESO:
        case OP_MEM_APRENDER_PESO:
        case OP_MEM_IMPRIMIR_CONCEPTO:
        case OP_MARCAR_ESTADO:
        case OP_NOP:
            return 1;
        default:
            return 0;
    }
}

static void emitir_instruccion(FILE* f, IRInstruction* inst, size_t idx, size_t inst_count, size_t data_size) {
    size_t next_idx = idx + 1;
    size_t curr_pc = idx * IR_INSTRUCTION_SIZE;
    uint64_t data_limit = data_size >= sizeof(uint64_t) ? (data_size - sizeof(uint64_t)) : 0;
    fprintf(f, "inst_%zu:\n", idx);
    switch (inst->opcode) {
        case OP_MOVER:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_LEER: {
            if (data_size < sizeof(uint64_t)) {
                emitir_jump_inmediato(f, next_idx, inst_count);
                break;
            }
            if (inst->flags & IR_INST_FLAG_B_IMMEDIATE) {
                uint16_t addr = inst->operand_b;
                if (inst->flags & IR_INST_FLAG_C_IMMEDIATE) {
                    addr |= (uint16_t)(inst->operand_c << 8);
                }
                fprintf(f, "    mov rax, %u\n", (unsigned)addr);
            } else {
                emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            }
            fprintf(f, "    cmp rax, %llu\n", (unsigned long long)data_limit);
            fprintf(f, "    ja skip_leer_%zu\n", idx);
            fprintf(f, "    mov rbx, [ir_data + rax]\n");
            emitir_store_reg(f, inst->operand_a, "rbx");
            fprintf(f, "skip_leer_%zu:\n", idx);
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        }
        case OP_ESCRIBIR: {
            if (data_size < sizeof(uint64_t)) {
                emitir_jump_inmediato(f, next_idx, inst_count);
                break;
            }
            if (inst->flags & IR_INST_FLAG_A_IMMEDIATE) {
                uint16_t addr = inst->operand_a;
                if (inst->flags & IR_INST_FLAG_C_IMMEDIATE) {
                    addr |= (uint16_t)(inst->operand_c << 8);
                }
                fprintf(f, "    mov rax, %u\n", (unsigned)addr);
            } else {
                emitir_load_operand(f, "rax", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
            }
            emitir_load_operand(f, "rbx", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            fprintf(f, "    cmp rax, %llu\n", (unsigned long long)data_limit);
            fprintf(f, "    ja skip_escribir_%zu\n", idx);
            fprintf(f, "    mov [ir_data + rax], rbx\n");
            fprintf(f, "skip_escribir_%zu:\n", idx);
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        }
        case OP_SUMAR:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    add rax, rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_RESTAR:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    sub rax, rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_MULTIPLICAR:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    imul rax, rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_DIVIDIR:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    cmp rbx, 0\n");
            fprintf(f, "    je finish_error\n");
            fprintf(f, "    xor rdx, rdx\n");
            fprintf(f, "    div rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_Y:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    and rax, rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_O:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    or rax, rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_XOR:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    xor rax, rbx\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_NO:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            fprintf(f, "    not rax\n");
            emitir_store_reg(f, inst->operand_a, "rax");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_COMPARAR:
            emitir_load_operand(f, "rax", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
            emitir_load_operand(f, "rbx", inst->operand_c, inst->flags, IR_INST_FLAG_C_IMMEDIATE);
            fprintf(f, "    xor rcx, rcx\n");
            fprintf(f, "    mov rdx, 2\n");
            fprintf(f, "    mov r8, 3\n");
            fprintf(f, "    cmp rax, rbx\n");
            fprintf(f, "    cmovb rcx, rdx\n");
            fprintf(f, "    cmova rcx, r8\n");
            emitir_store_reg(f, inst->operand_a, "rcx");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_IR: {
            if (inst->flags & IR_INST_FLAG_RELATIVE) {
                if (inst->flags & IR_INST_FLAG_A_IMMEDIATE) {
                    size_t target = (curr_pc + inst->operand_a) / IR_INSTRUCTION_SIZE;
                    emitir_jump_inmediato(f, target, inst_count);
                } else {
                    emitir_load_operand(f, "rax", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
                    fprintf(f, "    add rax, %zu\n", curr_pc);
                    emitir_dividir_por_5(f, "rax");
                    emitir_jump_indirecto(f, "rax");
                }
            } else {
                if (inst->flags & IR_INST_FLAG_A_IMMEDIATE) {
                    size_t target = inst->operand_a / IR_INSTRUCTION_SIZE;
                    emitir_jump_inmediato(f, target, inst_count);
                } else {
                    emitir_load_operand(f, "rax", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
                    emitir_dividir_por_5(f, "rax");
                    emitir_jump_indirecto(f, "rax");
                }
            }
            break;
        }
        case OP_SI: {
            emitir_load_operand(f, "rax", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
            fprintf(f, "    cmp rax, 0\n");
            if (next_idx >= inst_count) {
                fprintf(f, "    je finish\n");
            } else {
                fprintf(f, "    je inst_%zu\n", next_idx);
            }
            if (inst->flags & IR_INST_FLAG_RELATIVE) {
                if (inst->flags & IR_INST_FLAG_B_IMMEDIATE) {
                    size_t target = (curr_pc + inst->operand_b) / IR_INSTRUCTION_SIZE;
                    emitir_jump_inmediato(f, target, inst_count);
                } else {
                    emitir_load_operand(f, "rbx", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
                    fprintf(f, "    add rbx, %zu\n", curr_pc);
                    emitir_dividir_por_5(f, "rbx");
                    emitir_jump_indirecto(f, "rbx");
                }
            } else {
                if (inst->flags & IR_INST_FLAG_B_IMMEDIATE) {
                    size_t target = inst->operand_b / IR_INSTRUCTION_SIZE;
                    emitir_jump_inmediato(f, target, inst_count);
                } else {
                    emitir_load_operand(f, "rbx", inst->operand_b, inst->flags, IR_INST_FLAG_B_IMMEDIATE);
                    emitir_dividir_por_5(f, "rbx");
                    emitir_jump_indirecto(f, "rbx");
                }
            }
            break;
        }
        case OP_LLAMAR: {
            fprintf(f, "    mov rax, [stack_ptr]\n");
            fprintf(f, "    cmp rax, 256\n");
            fprintf(f, "    jae finish_error\n");
            fprintf(f, "    mov [stack + rax*8], %zu\n", next_idx);
            fprintf(f, "    inc rax\n");
            fprintf(f, "    mov [stack_ptr], rax\n");
            if (inst->flags & IR_INST_FLAG_RELATIVE) {
                if (inst->flags & IR_INST_FLAG_A_IMMEDIATE) {
                    size_t target = (curr_pc + inst->operand_a) / IR_INSTRUCTION_SIZE;
                    emitir_jump_inmediato(f, target, inst_count);
                } else {
                    emitir_load_operand(f, "rbx", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
                    fprintf(f, "    add rbx, %zu\n", curr_pc);
                    emitir_dividir_por_5(f, "rbx");
                    emitir_jump_indirecto(f, "rbx");
                }
            } else {
                if (inst->flags & IR_INST_FLAG_A_IMMEDIATE) {
                    size_t target = inst->operand_a / IR_INSTRUCTION_SIZE;
                    emitir_jump_inmediato(f, target, inst_count);
                } else {
                    emitir_load_operand(f, "rbx", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
                    emitir_dividir_por_5(f, "rbx");
                    emitir_jump_indirecto(f, "rbx");
                }
            }
            break;
        }
        case OP_RETORNAR: {
            fprintf(f, "    mov rax, [stack_ptr]\n");
            fprintf(f, "    cmp rax, 0\n");
            fprintf(f, "    jne ret_stack_%zu\n", idx);
            emitir_load_operand(f, "rbx", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
            fprintf(f, "    mov [exit_code], rbx\n");
            fprintf(f, "    jmp finish\n");
            fprintf(f, "ret_stack_%zu:\n", idx);
            fprintf(f, "    dec rax\n");
            fprintf(f, "    mov [stack_ptr], rax\n");
            fprintf(f, "    mov rbx, [stack + rax*8]\n");
            emitir_jump_indirecto(f, "rbx");
            break;
        }
        case OP_OBSERVAR:
            emitir_load_operand(f, "rdi", inst->operand_a, inst->flags, IR_INST_FLAG_A_IMMEDIATE);
            fprintf(f, "    mov rsi, ia_prefix\n");
            fprintf(f, "    call print_cstr\n");
            fprintf(f, "    mov rdi, rdi\n");
            fprintf(f, "    call print_u64\n");
            fprintf(f, "    mov rsi, newline\n");
            fprintf(f, "    call print_cstr\n");
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        case OP_IMPRIMIR_TEXTO: {
            uint32_t offset = (uint32_t)inst->operand_a |
                              ((uint32_t)inst->operand_b << 8) |
                              ((uint32_t)inst->operand_c << 16);
            fprintf(f, "    mov rax, %u\n", offset);
            fprintf(f, "    cmp rax, %u\n", (unsigned)data_size);
            fprintf(f, "    jae skip_print_%zu\n", idx);
            fprintf(f, "    lea rsi, [ir_data + rax]\n");
            fprintf(f, "    call print_cstr\n");
            fprintf(f, "    mov rsi, newline\n");
            fprintf(f, "    call print_cstr\n");
            fprintf(f, "skip_print_%zu:\n", idx);
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        }
        case OP_MEM_RECORDAR_TEXTO:
        case OP_MEM_BUSCAR_PESO:
        case OP_MEM_APRENDER_PESO:
        case OP_MEM_IMPRIMIR_CONCEPTO:
        case OP_MARCAR_ESTADO:
        case OP_NOP:
            emitir_jump_inmediato(f, next_idx, inst_count);
            break;
        default:
            if (!opcode_soportado_aot_tier1(inst->opcode)) {
                fprintf(f, "    ; opcode 0x%02X fuera del subset AOT Tier-1\n", inst->opcode);
            }
            fprintf(f, "    jmp finish_error\n");
            break;
    }
}

static int escribir_asm(const char* ruta, IRFile* ir) {
    if (!ruta || !ir) return -1;
    FILE* f = fopen(ruta, "w");
    if (!f) return -1;
    size_t inst_count = ir->header.code_size / IR_INSTRUCTION_SIZE;
    fprintf(f, "global _start\n");
    fprintf(f, "section .data\n");
    emitir_cadena_data(f, "ia_prefix", "IA observa: ");
    emitir_cadena_data(f, "newline", "\\n");
    fprintf(f, "ir_data:\n");
    if (ir->header.data_size > 0 && ir->data) {
        emitir_bytes(f, ir->data, ir->header.data_size);
    }
    fprintf(f, "section .bss\n");
    fprintf(f, "regs resq 256\n");
    fprintf(f, "stack resq 256\n");
    fprintf(f, "stack_ptr resq 1\n");
    fprintf(f, "exit_code resq 1\n");
    fprintf(f, "num_buf resb 32\n");
    fprintf(f, "out_buf resb 32\n");
    fprintf(f, "section .data\n");
    fprintf(f, "jump_table:\n");
    for (size_t i = 0; i < inst_count; i++) {
        fprintf(f, "    dq inst_%zu\n", i);
    }
    fprintf(f, "section .text\n");
    fprintf(f, "print_cstr:\n");
    fprintf(f, "    push rdi\n");
    fprintf(f, "    push rsi\n");
    fprintf(f, "    push rdx\n");
    fprintf(f, "    mov rdi, rsi\n");
    fprintf(f, "    xor rdx, rdx\n");
    fprintf(f, "pc_len_loop:\n");
    fprintf(f, "    cmp byte [rdi + rdx], 0\n");
    fprintf(f, "    je pc_len_done\n");
    fprintf(f, "    inc rdx\n");
    fprintf(f, "    jmp pc_len_loop\n");
    fprintf(f, "pc_len_done:\n");
    fprintf(f, "    mov rax, 1\n");
    fprintf(f, "    mov rsi, rdi\n");
    fprintf(f, "    mov rdi, 1\n");
    fprintf(f, "    syscall\n");
    fprintf(f, "    pop rdx\n");
    fprintf(f, "    pop rsi\n");
    fprintf(f, "    pop rdi\n");
    fprintf(f, "    ret\n");
    fprintf(f, "print_u64:\n");
    fprintf(f, "    push rax\n");
    fprintf(f, "    push rbx\n");
    fprintf(f, "    push rcx\n");
    fprintf(f, "    push rdx\n");
    fprintf(f, "    push r8\n");
    fprintf(f, "    mov rax, rdi\n");
    fprintf(f, "    mov r8, 0\n");
    fprintf(f, "    cmp rax, 0\n");
    fprintf(f, "    jne pu_loop\n");
    fprintf(f, "    mov byte [num_buf], '0'\n");
    fprintf(f, "    mov r8, 1\n");
    fprintf(f, "    jmp pu_done\n");
    fprintf(f, "pu_loop:\n");
    fprintf(f, "    xor rdx, rdx\n");
    fprintf(f, "    mov rcx, 10\n");
    fprintf(f, "    div rcx\n");
    fprintf(f, "    add dl, '0'\n");
    fprintf(f, "    mov [num_buf + r8], dl\n");
    fprintf(f, "    inc r8\n");
    fprintf(f, "    cmp rax, 0\n");
    fprintf(f, "    jne pu_loop\n");
    fprintf(f, "pu_done:\n");
    fprintf(f, "    xor rcx, rcx\n");
    fprintf(f, "pu_rev:\n");
    fprintf(f, "    cmp rcx, r8\n");
    fprintf(f, "    je pu_write\n");
    fprintf(f, "    mov rbx, r8\n");
    fprintf(f, "    dec rbx\n");
    fprintf(f, "    sub rbx, rcx\n");
    fprintf(f, "    mov al, [num_buf + rbx]\n");
    fprintf(f, "    mov [out_buf + rcx], al\n");
    fprintf(f, "    inc rcx\n");
    fprintf(f, "    jmp pu_rev\n");
    fprintf(f, "pu_write:\n");
    fprintf(f, "    mov rax, 1\n");
    fprintf(f, "    mov rdi, 1\n");
    fprintf(f, "    mov rsi, out_buf\n");
    fprintf(f, "    mov rdx, r8\n");
    fprintf(f, "    syscall\n");
    fprintf(f, "    pop r8\n");
    fprintf(f, "    pop rdx\n");
    fprintf(f, "    pop rcx\n");
    fprintf(f, "    pop rbx\n");
    fprintf(f, "    pop rax\n");
    fprintf(f, "    ret\n");
    fprintf(f, "_start:\n");
    fprintf(f, "    mov qword [stack_ptr], 0\n");
    fprintf(f, "    mov qword [exit_code], 0\n");
    if (inst_count == 0) {
        fprintf(f, "    mov qword [exit_code], 0\n");
        fprintf(f, "    jmp finish\n");
    } else {
        fprintf(f, "    jmp inst_0\n");
    }
    for (size_t i = 0; i < inst_count; i++) {
        IRInstruction inst;
        if (ir_get_instruction(ir, i, &inst) != 0) {
            fclose(f);
            return -1;
        }
        emitir_instruccion(f, &inst, i, inst_count, ir->header.data_size);
    }
    fprintf(f, "finish:\n");
    fprintf(f, "    mov rax, 60\n");
    fprintf(f, "    mov rdi, [exit_code]\n");
    fprintf(f, "    syscall\n");
    fprintf(f, "finish_error:\n");
    fprintf(f, "    mov rax, 60\n");
    fprintf(f, "    mov rdi, 1\n");
    fprintf(f, "    syscall\n");
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo.jbo> -o <salida.elf> [--asm <salida.asm>]\n", argv[0]);
        return 1;
    }
    const char* input = argv[1];
    const char* output = NULL;
    const char* asm_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--asm") == 0 && i + 1 < argc) {
            asm_path = argv[++i];
        }
    }
    if (!output) {
        fprintf(stderr, "Error: Se requiere -o <salida.elf>\n");
        return 1;
    }
    IRFile* ir = ir_file_create();
    if (!ir) {
        fprintf(stderr, "Error: No se pudo crear IRFile\n");
        return 1;
    }
    if (ir_file_read(ir, input) != 0) {
        fprintf(stderr, "Error: No se pudo leer archivo IR: %s\n", input);
        ir_file_destroy(ir);
        return 1;
    }
    char asm_tmp[1024];
    if (!asm_path) {
        snprintf(asm_tmp, sizeof(asm_tmp), "%s.asm", output);
        asm_path = asm_tmp;
    }
    if (escribir_asm(asm_path, ir) != 0) {
        fprintf(stderr, "Error: No se pudo escribir ASM\n");
        ir_file_destroy(ir);
        return 1;
    }
    char obj_path[1024];
    snprintf(obj_path, sizeof(obj_path), "%s.o", output);
    size_t cmd_len = (size_t)snprintf(NULL, 0, "nasm -f elf64 \"%s\" -o \"%s\"", asm_path, obj_path);
    char* cmd = (char*)malloc(cmd_len + 1);
    if (!cmd) {
        fprintf(stderr, "Error: No hay memoria para comando\n");
        ir_file_destroy(ir);
        return 1;
    }
    snprintf(cmd, cmd_len + 1, "nasm -f elf64 \"%s\" -o \"%s\"", asm_path, obj_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: No se pudo ensamblar ASM\n");
        free(cmd);
        ir_file_destroy(ir);
        return 1;
    }
    free(cmd);
    cmd_len = (size_t)snprintf(NULL, 0, "ld \"%s\" -o \"%s\"", obj_path, output);
    cmd = (char*)malloc(cmd_len + 1);
    if (!cmd) {
        fprintf(stderr, "Error: No hay memoria para comando\n");
        ir_file_destroy(ir);
        return 1;
    }
    snprintf(cmd, cmd_len + 1, "ld \"%s\" -o \"%s\"", obj_path, output);
    if (system(cmd) != 0) {
        fprintf(stderr, "Error: No se pudo enlazar ELF\n");
        free(cmd);
        ir_file_destroy(ir);
        return 1;
    }
    free(cmd);
    if (asm_path != asm_tmp) {
        (void)asm_path;
    } else {
        remove(asm_path);
    }
    remove(obj_path);
    printf("✓ Backend directo generado: %s\n", output);
    ir_file_destroy(ir);
    return 0;
}
