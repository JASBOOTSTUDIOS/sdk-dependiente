#ifndef READER_IR_H
#define READER_IR_H

#include "ir_format.h"
#include <stddef.h>

// Resultado de validación
typedef enum {
    IR_VALID_OK,
    IR_VALID_INVALID_MAGIC,
    IR_VALID_INVALID_VERSION,
    IR_VALID_INVALID_SIZE,
    IR_VALID_INVALID_OPCODE,
    IR_VALID_INVALID_JUMP,
    IR_VALID_INVALID_STACK,
    IR_VALID_INVALID_METADATA,
    IR_VALID_SECURITY,
    IR_VALID_ERROR
} IRValidationResult;

// Información de validación
typedef struct {
    IRValidationResult result;
    size_t instruction_index;  // Índice de instrucción con error (si aplica)
    const char* message;        // Mensaje de error
} IRValidationInfo;

// Validar archivo IR
IRValidationInfo ir_validate_file(const char* filename);
IRValidationInfo ir_validate_memory(IRFile* ir);

// Obtener instrucción por índice
int ir_get_instruction(IRFile* ir, size_t index, IRInstruction* inst);

// Obtener instrucción en PC (para VM)
int ir_get_instruction_at_pc(IRFile* ir, size_t pc, IRInstruction* inst);

// Utilidades
const char* ir_validation_result_to_string(IRValidationResult result);

#endif // READER_IR_H
