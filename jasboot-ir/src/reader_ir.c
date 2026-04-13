#include "reader_ir.h"
#include <stdlib.h>
#include <string.h>

const char* ir_validation_result_to_string(IRValidationResult result) {
    switch (result) {
        case IR_VALID_OK: return "OK";
        case IR_VALID_INVALID_MAGIC: return "Invalid magic number";
        case IR_VALID_INVALID_VERSION: return "Invalid version";
        case IR_VALID_INVALID_SIZE: return "Invalid size";
        case IR_VALID_INVALID_OPCODE: return "Invalid opcode";
        case IR_VALID_INVALID_JUMP: return "Invalid jump address";
        case IR_VALID_INVALID_STACK: return "Invalid stack operation";
        case IR_VALID_INVALID_METADATA: return "Invalid IA metadata";
        case IR_VALID_SECURITY: return "Security policy violation";
        case IR_VALID_ERROR: return "Validation error";
        default: return "Unknown error";
    }
}

typedef struct {
    int present;
    IRJasbSecPolicy policy;
} IRJasbSecInfo;

static int ir_parse_jasb_sec(IRFile* ir, IRJasbSecInfo* out_info, const char** error_msg) {
    if (!out_info) return -1;
    out_info->present = 0;
    memset(&out_info->policy, 0, sizeof(out_info->policy));
    if (error_msg) *error_msg = NULL;

    if (!ir || !ir->ia_metadata || ir->ia_metadata_size < 8) {
        return 0;
    }

    const uint8_t* data = ir->ia_metadata;
    size_t size = ir->ia_metadata_size;

    if (data[0] != IR_IA_MAGIC_0 || data[1] != IR_IA_MAGIC_1 ||
        data[2] != IR_IA_MAGIC_2 || data[3] != IR_IA_MAGIC_3) {
        return 0; // Metadata legacy o no estructurada
    }

    if (data[4] != IR_IA_VERSION_1) {
        if (error_msg) *error_msg = "Unsupported IA metadata version";
        return -1;
    }

    size_t offset = 8;
    while (offset + 3 <= size) {
        uint8_t tag = data[offset];
        uint16_t len = (uint16_t)data[offset + 1] | ((uint16_t)data[offset + 2] << 8);
        offset += 3;
        if (offset + len > size) {
            if (error_msg) *error_msg = "IA metadata TLV length out of bounds";
            return -1;
        }
        if (tag == IR_IA_TAG_JASB_SEC) {
            if (len != 8) {
                if (error_msg) *error_msg = "JASB-SEC payload length invalid";
                return -1;
            }
            out_info->policy.version = data[offset + 0];
            out_info->policy.mode = data[offset + 1];
            out_info->policy.max_stack = (uint16_t)data[offset + 2] |
                                         ((uint16_t)data[offset + 3] << 8);
            out_info->policy.max_jump = (uint32_t)data[offset + 4] |
                                        ((uint32_t)data[offset + 5] << 8) |
                                        ((uint32_t)data[offset + 6] << 16) |
                                        ((uint32_t)data[offset + 7] << 24);
            out_info->present = 1;
        }
        offset += len;
    }

    if (offset != size) {
        if (error_msg) *error_msg = "IA metadata TLV trailing bytes";
        return -1;
    }

    return 0;
}

int ir_get_instruction(IRFile* ir, size_t index, IRInstruction* inst) {
    if (!ir || !inst) return -1;
    if (index >= ir->code_count) return -1;
    
    size_t offset = index * IR_INSTRUCTION_SIZE;
    inst->opcode = ir->code[offset + 0];
    inst->flags = ir->code[offset + 1];
    inst->operand_a = ir->code[offset + 2];
    inst->operand_b = ir->code[offset + 3];
    inst->operand_c = ir->code[offset + 4];
    
    return 0;
}

int ir_get_instruction_at_pc(IRFile* ir, size_t pc, IRInstruction* inst) {
    // PC es el offset en bytes, convertir a índice de instrucción
    if (pc % IR_INSTRUCTION_SIZE != 0) return -1;
    size_t index = pc / IR_INSTRUCTION_SIZE;
    return ir_get_instruction(ir, index, inst);
}

IRValidationInfo ir_validate_memory(IRFile* ir) {
    IRValidationInfo info = {IR_VALID_OK, 0, NULL};
    IRJasbSecInfo sec_info;
    const char* metadata_error = NULL;
    
    if (!ir) {
        info.result = IR_VALID_ERROR;
        info.message = "IR file is NULL";
        return info;
    }
    
    // Validar magic
    if (ir->header.magic[0] != IR_MAGIC_0 ||
        ir->header.magic[1] != IR_MAGIC_1 ||
        ir->header.magic[2] != IR_MAGIC_2 ||
        ir->header.magic[3] != IR_MAGIC_3) {
        info.result = IR_VALID_INVALID_MAGIC;
        info.message = "Invalid magic number";
        return info;
    }
    
    // Validar versión
    if (ir->header.version != IR_VERSION_1) {
        info.result = IR_VALID_INVALID_VERSION;
        info.message = "Unsupported version";
        return info;
    }
    
    // Validar tamaño de código
    if (ir->header.code_size % IR_INSTRUCTION_SIZE != 0) {
        info.result = IR_VALID_INVALID_SIZE;
        info.message = "Code size is not multiple of instruction size";
        return info;
    }
    
    // Validar metadata IA si está presente
    if (ir->header.flags & IR_FLAG_IA_METADATA) {
        if (ir_parse_jasb_sec(ir, &sec_info, &metadata_error) != 0) {
            info.result = IR_VALID_INVALID_METADATA;
            info.message = metadata_error ? metadata_error : "Invalid IA metadata";
            return info;
        }
    } else {
        memset(&sec_info, 0, sizeof(sec_info));
    }

    int strict = (ir->header.flags & IR_FLAG_SECURITY_STRICT) ? 1 : 0;
    if (sec_info.present && sec_info.policy.mode == 2) {
        strict = 1;
    }
    uint16_t max_stack = sec_info.present && sec_info.policy.max_stack > 0 ? sec_info.policy.max_stack : 256;
    uint32_t max_jump = sec_info.present ? sec_info.policy.max_jump : 0;
    uint16_t stack_depth = 0;

    // Validar cada instrucción

    for (size_t i = 0; i < ir->code_count; i++) {
        IRInstruction inst;
        if (ir_get_instruction(ir, i, &inst) != 0) {
            info.result = IR_VALID_ERROR;
            info.instruction_index = i;
            info.message = "Failed to read instruction";
            return info;
        }
        
        // Validar opcode
        if (inst.opcode != OP_HALT && inst.opcode != OP_MOVER && inst.opcode != OP_LEER && inst.opcode != OP_ESCRIBIR && inst.opcode != OP_MOVER_U24 &&
            inst.opcode != OP_LOAD_STR_HASH &&
            inst.opcode != OP_SUMAR && inst.opcode != OP_RESTAR && 
            inst.opcode != OP_MULTIPLICAR && inst.opcode != OP_DIVIDIR &&
            inst.opcode != OP_MODULO &&
            inst.opcode != OP_SUMAR_FLT && inst.opcode != OP_RESTAR_FLT &&
            inst.opcode != OP_MULTIPLICAR_FLT && inst.opcode != OP_DIVIDIR_FLT &&
            inst.opcode != OP_CONV_I2F && inst.opcode != OP_CONV_F2I && inst.opcode != OP_RAIZ &&
            inst.opcode != OP_SIN && inst.opcode != OP_COS && inst.opcode != OP_TAN && inst.opcode != OP_ATAN2 &&
            inst.opcode != OP_MAT4_MUL_VEC4 && inst.opcode != OP_MAT4_MUL &&
            inst.opcode != OP_EXP && inst.opcode != OP_LOG && inst.opcode != OP_LOG10 &&
            inst.opcode != OP_MAT4_IDENTIDAD && inst.opcode != OP_MAT4_TRANSPUESTA && inst.opcode != OP_MAT4_INVERSA &&
            inst.opcode != OP_MAT3_MUL_VEC3 && inst.opcode != OP_MAT3_MUL &&
            inst.opcode != OP_MEM_REFORZAR_CONCEPTO && inst.opcode != OP_MEM_PENALIZAR_CONCEPTO &&
            inst.opcode != OP_MEM_CONSOLIDAR_SUENO && inst.opcode != OP_MEM_OLVIDAR_DEBILES &&
            inst.opcode != OP_PERCEPCION_REGISTRAR && inst.opcode != OP_PERCEPCION_VENTANA &&
            inst.opcode != OP_PERCEPCION_LIMPIAR && inst.opcode != OP_PERCEPCION_TAMANO &&
            inst.opcode != OP_PERCEPCION_ANTERIOR && inst.opcode != OP_PERCEPCION_LISTA &&
            inst.opcode != OP_RASTRO_ACTIVACION_VENTANA && inst.opcode != OP_RASTRO_ACTIVACION_LIMPIAR &&
            inst.opcode != OP_RASTRO_ACTIVACION_TAMANO && inst.opcode != OP_RASTRO_ACTIVACION_OBTENER &&
            inst.opcode != OP_RASTRO_ACTIVACION_PESO && inst.opcode != OP_RASTRO_ACTIVACION_LISTA &&
            inst.opcode != OP_MEM_ELEGIR_POR_PESO_IDX && inst.opcode != OP_MEM_ELEGIR_POR_PESO_ID &&
            inst.opcode != OP_MEM_ELEGIR_POR_PESO_SEMILLA &&
            inst.opcode != OP_CMP_LT_FLT && inst.opcode != OP_CMP_GT_FLT &&
            inst.opcode != OP_CMP_LE_FLT && inst.opcode != OP_CMP_GE_FLT && inst.opcode != OP_CMP_EQ_FLT &&
            inst.opcode != OP_Y && inst.opcode != OP_O && inst.opcode != OP_XOR && inst.opcode != OP_NO &&
            inst.opcode != OP_COMPARAR && inst.opcode != OP_CMP_EQ && inst.opcode != OP_CMP_LT &&
            inst.opcode != OP_CMP_GT && inst.opcode != OP_CMP_LE && inst.opcode != OP_CMP_GE &&
            inst.opcode != OP_CMP_LT_U && inst.opcode != OP_CMP_GT_U && inst.opcode != OP_CMP_LE_U && inst.opcode != OP_CMP_GE_U &&
            inst.opcode != OP_IR && inst.opcode != OP_SI && inst.opcode != OP_LLAMAR && inst.opcode != OP_RETORNAR &&
            inst.opcode != OP_ID_A_TEXTO &&
            inst.opcode != OP_RESERVAR_PILA &&
            inst.opcode != OP_STR_DESDE_NUMERO &&
            inst.opcode != OP_STR_FLOTANTE_PREC &&
            inst.opcode != OP_JSON_A_TEXTO && inst.opcode != OP_JSON_A_ENTERO &&
            inst.opcode != OP_JSON_A_FLOTANTE && inst.opcode != OP_JSON_A_BOOL &&
            inst.opcode != OP_JSON_TIPO &&
            inst.opcode != OP_CLOSURE_CREAR && inst.opcode != OP_CLOSURE_CARGAR &&
            inst.opcode != OP_BYTES_CREAR && inst.opcode != OP_BYTES_TAMANO &&
            inst.opcode != OP_BYTES_OBTENER && inst.opcode != OP_BYTES_PONER &&
            inst.opcode != OP_BYTES_ANEXAR && inst.opcode != OP_BYTES_PUNTERO && inst.opcode != OP_BYTES_SUBBYTES &&
            inst.opcode != OP_BYTES_DESDE_TEXTO && inst.opcode != OP_BYTES_A_TEXTO &&
            inst.opcode != OP_DNS_RESOLVER &&
            inst.opcode != OP_TCP_CONECTAR && inst.opcode != OP_TCP_ESCUCHAR &&
            inst.opcode != OP_TCP_ACEPTAR && inst.opcode != OP_TCP_ENVIAR &&
            inst.opcode != OP_TCP_RECIBIR && inst.opcode != OP_TCP_CERRAR &&
            inst.opcode != OP_TLS_CLIENTE && inst.opcode != OP_TLS_SERVIDOR &&
            inst.opcode != OP_TLS_ENVIAR && inst.opcode != OP_TLS_RECIBIR &&
            inst.opcode != OP_TLS_CERRAR &&
            inst.opcode != OP_IO_PAUSA &&
            inst.opcode != OP_PAUSA_MILISEGUNDOS &&
            inst.opcode != OP_IMPRIMIR_TEXTO &&
            inst.opcode != OP_IO_INGRESAR_TEXTO &&
            inst.opcode != OP_IO_INPUT_REG &&
            inst.opcode != OP_IO_PERCIBIR_TECLADO &&
            inst.opcode != OP_IO_ENTRADA_FLOTANTE &&
            inst.opcode != OP_MEM_BUSCAR_PESO_REG &&
            inst.opcode != OP_MEM_APRENDER_PESO_REG &&
            inst.opcode != OP_MEM_RECORDAR_TEXTO && 
            inst.opcode != OP_MEM_APRENDER_CONCEPTO &&
            inst.opcode != OP_MEM_BUSCAR_CONCEPTO &&
            inst.opcode != OP_MEM_ASOCIAR_CONCEPTOS &&
            inst.opcode != OP_MEM_ACTUALIZAR_PESO &&
            inst.opcode != OP_MEM_OBTENER_ASOCIACIONES &&
            inst.opcode != OP_FS_LEER_TEXTO &&
            inst.opcode != OP_JSON_PARSE && inst.opcode != OP_JSON_STRINGIFY &&
            inst.opcode != OP_JSON_OBJETO_OBTENER && inst.opcode != OP_JSON_LISTA_OBTENER &&
            inst.opcode != OP_JSON_LISTA_TAMANO &&
            inst.opcode != OP_FS_ESCRIBIR_TEXTO && inst.opcode != OP_MEM_IMPRIMIR_CONCEPTO &&
            inst.opcode != OP_ACTIVAR_MODULO && inst.opcode != OP_ESTABLECER_CONTEXTO &&
            inst.opcode != OP_USA_CONCEPTO && inst.opcode != OP_ASOCIADO_CON &&
            inst.opcode != OP_MEM_COMPARAR_TEXTO && inst.opcode != OP_MEM_PROCESAR_TEXTO &&
            inst.opcode != OP_MEM_CONTIENE_TEXTO && inst.opcode != OP_MEM_COPIAR_TEXTO &&
            inst.opcode != OP_STR_EXTRAER_ANTES && inst.opcode != OP_STR_EXTRAER_DESPUES &&
            inst.opcode != OP_STR_EXTRAER_ANTES_REG && inst.opcode != OP_STR_EXTRAER_DESPUES_REG &&
            inst.opcode != OP_STR_CONCATENAR && inst.opcode != OP_STR_CONCATENAR_REG &&
            inst.opcode != OP_STR_REGISTRAR_LITERAL &&
            inst.opcode != OP_IMPRIMIR_NUMERO &&
            inst.opcode != OP_MEM_PENSAR &&
            inst.opcode != OP_MEM_ASOCIAR &&
            inst.opcode != OP_MEM_ULTIMA_PALABRA &&
            inst.opcode != OP_MEM_IMPRIMIR_ID &&
            inst.opcode != OP_MEM_ECO &&
            inst.opcode != OP_MEM_TERMINA_CON &&
            inst.opcode != OP_LEER_U32_IND &&
            inst.opcode != OP_MEM_LISTA_CREAR &&
            inst.opcode != OP_MEM_LISTA_AGREGAR &&
            inst.opcode != OP_MEM_LISTA_OBTENER &&
            inst.opcode != OP_MEM_LISTA_TAMANO &&
            inst.opcode != OP_MEM_LISTA_ID &&
            inst.opcode != OP_MEM_PENSAR_RESPUESTA &&
            inst.opcode != OP_MEM_LISTA_LIMPIAR &&
            inst.opcode != OP_MEM_LISTA_LIBERAR &&
            inst.opcode != OP_MEM_LISTA_PONER &&
            inst.opcode != OP_MEM_LISTA_UNIR &&
            inst.opcode != OP_MEM_MAPA_OBTENER &&
            inst.opcode != OP_MEM_MAPA_TAMANO &&
            inst.opcode != OP_MEM_MAPA_CONTIENE &&
            inst.opcode != OP_STR_MINUSCULAS &&
            inst.opcode != OP_STR_MAYUSCULAS &&
            inst.opcode != OP_STR_COPIAR &&
            inst.opcode != OP_FS_ABRIR &&
            inst.opcode != OP_FS_ESCRIBIR &&
            inst.opcode != OP_FS_FIN_ARCHIVO &&
            inst.opcode != OP_FS_CERRAR &&
            inst.opcode != OP_FS_EXISTE &&
            inst.opcode != OP_SYS_TIMESTAMP &&
            inst.opcode != OP_FS_LEER_LINEA &&
            inst.opcode != OP_STR_ASOCIAR_PESOS &&
            inst.opcode != OP_IMPRIMIR_FLOTANTE &&
            inst.opcode != OP_MEM_OBTENER_FUERZA &&
            inst.opcode != OP_STR_LONGITUD &&
            inst.opcode != OP_IO_PERCIBIR_TECLADO &&
            inst.opcode != OP_IO_ENTRADA_FLOTANTE &&
            inst.opcode != OP_STR_EXTRAER_CARACTER &&
            inst.opcode != OP_STR_CODIGO_CARACTER &&
            inst.opcode != OP_STR_DESDE_CODIGO &&
            inst.opcode != OP_STR_DIVIDIR_TEXTO &&
            inst.opcode != OP_BIT_SHL &&
            inst.opcode != OP_BIT_SHR &&
            inst.opcode != OP_SYS_EXEC &&
            inst.opcode != OP_FS_ESCRIBIR_BYTE &&
            inst.opcode != OP_FS_LEER_BYTE &&
            inst.opcode != OP_FS_ESCRIBIR_U32 &&
            inst.opcode != OP_FS_LEER_U32 &&
            inst.opcode != OP_MEM_MAPA_CREAR &&
            inst.opcode != OP_MEM_MAPA_PONER &&
            inst.opcode != OP_MEM_MAPA_OBTENER &&
            inst.opcode != OP_MEM_MAPA_TAMANO &&
            inst.opcode != OP_MEM_OBTENER_RELACIONADOS &&
            inst.opcode != OP_MEM_ES_VARIABLE_SISTEMA &&
            inst.opcode != OP_MEM_CONTIENE_TEXTO_REG &&
            inst.opcode != OP_MEM_TERMINA_CON_REG &&
            inst.opcode != OP_MEM_OBTENER_TODOS &&
            inst.opcode != OP_FS_LISTAR &&
            inst.opcode != OP_FS_BORRAR &&
            inst.opcode != OP_FS_COPIAR &&
            inst.opcode != OP_FS_MOVER &&
            inst.opcode != OP_FS_TAMANO &&
            inst.opcode != OP_CARGAR_BIBLIOTECA &&
            inst.opcode != OP_FFI_OBTENER_SIMBOLO &&
            inst.opcode != OP_FFI_LLAMAR &&
            inst.opcode != OP_HEAP_RESERVAR &&
            inst.opcode != OP_HEAP_LIBERAR &&
            inst.opcode != OP_IR_ESCRIBIR &&
            inst.opcode != OP_MEM_PENALIZAR &&
            inst.opcode != OP_MEM_ASOCIAR &&
            inst.opcode != OP_MEM_ECO &&
            inst.opcode != OP_MARCAR_ESTADO &&
            inst.opcode != OP_OBSERVAR &&
            inst.opcode != OP_MEM_CREAR &&
            inst.opcode != OP_MEM_CERRAR &&
            inst.opcode != OP_USA_CONCEPTO &&
            inst.opcode != OP_ASOCIADO_CON &&
            inst.opcode != OP_MEM_ULTIMA_SILABA &&
            inst.opcode != OP_STR_ASOCIAR_SECUENCIA &&
            inst.opcode != OP_MEM_PENSAR_SIGUIENTE &&
            inst.opcode != OP_MEM_PENSAR_ANTERIOR &&
            inst.opcode != OP_MEM_CORREGIR_SECUENCIA &&
            inst.opcode != OP_MEM_ASOCIAR_RELACION &&
            inst.opcode != OP_STR_ASOCIAR_SECUENCIA &&
            inst.opcode != OP_MEM_COMPARAR_PATRONES &&
            inst.opcode != OP_MEM_BUSCAR_ASOCIADOS &&
            inst.opcode != OP_MEM_BUSCAR_ASOCIADOS_LISTA &&
            inst.opcode != OP_MEM_OBTENER_VALOR &&
            inst.opcode != OP_MEM_DECAE_CONEXIONES &&
            inst.opcode != OP_MEM_PROPAGAR_ACTIVACION &&
            inst.opcode != OP_MEM_RESOLVER_CONFLICTOS &&
            inst.opcode != OP_MEM_REGISTRAR_PATRON &&
            inst.opcode != OP_MEM_OBTENER_RELACION &&
            inst.opcode != OP_MEM_IMPRIMIR_ID &&
            inst.opcode != OP_STR_A_ENTERO &&
            inst.opcode != OP_STR_A_FLOTANTE &&
            inst.opcode != OP_SUMAR_FLT &&
            inst.opcode != OP_RESTAR_FLT &&
            inst.opcode != OP_MULTIPLICAR_FLT &&
            inst.opcode != OP_DIVIDIR_FLT &&
            inst.opcode != OP_IMPRIMIR_FLOTANTE &&
            inst.opcode != OP_SIN && inst.opcode != OP_COS && inst.opcode != OP_TAN && inst.opcode != OP_ATAN2 &&
            inst.opcode != OP_MAT4_MUL_VEC4 && inst.opcode != OP_MAT4_MUL &&
            inst.opcode != OP_EXP && inst.opcode != OP_LOG && inst.opcode != OP_LOG10 &&
            inst.opcode != OP_MAT4_IDENTIDAD && inst.opcode != OP_MAT4_TRANSPUESTA && inst.opcode != OP_MAT4_INVERSA &&
            inst.opcode != OP_MAT3_MUL_VEC3 && inst.opcode != OP_MAT3_MUL &&
            inst.opcode != OP_MEM_REFORZAR_CONCEPTO && inst.opcode != OP_MEM_PENALIZAR_CONCEPTO &&
            inst.opcode != OP_MEM_CONSOLIDAR_SUENO && inst.opcode != OP_MEM_OLVIDAR_DEBILES &&
            inst.opcode != OP_PERCEPCION_REGISTRAR && inst.opcode != OP_PERCEPCION_VENTANA &&
            inst.opcode != OP_PERCEPCION_LIMPIAR && inst.opcode != OP_PERCEPCION_TAMANO &&
            inst.opcode != OP_PERCEPCION_ANTERIOR && inst.opcode != OP_PERCEPCION_LISTA &&
            inst.opcode != OP_RASTRO_ACTIVACION_VENTANA && inst.opcode != OP_RASTRO_ACTIVACION_LIMPIAR &&
            inst.opcode != OP_RASTRO_ACTIVACION_TAMANO && inst.opcode != OP_RASTRO_ACTIVACION_OBTENER &&
            inst.opcode != OP_RASTRO_ACTIVACION_PESO && inst.opcode != OP_RASTRO_ACTIVACION_LISTA &&
            inst.opcode != OP_MEM_ELEGIR_POR_PESO_IDX && inst.opcode != OP_MEM_ELEGIR_POR_PESO_ID &&
            inst.opcode != OP_MEM_ELEGIR_POR_PESO_SEMILLA &&
            inst.opcode != OP_MEM_CONTIENE_TEXTO_REG &&
            inst.opcode != OP_MEM_TERMINA_CON_REG &&
            inst.opcode != OP_STR_LONGITUD &&
            inst.opcode != OP_STR_EXTRAER_CARACTER &&
            inst.opcode != 0x69 && inst.opcode != 0x6A && inst.opcode != OP_STR_SUBTEXTO &&
            inst.opcode != OP_GET_FP &&
            inst.opcode != OP_TRY_ENTER && inst.opcode != OP_TRY_LEAVE &&
            inst.opcode != OP_NOP && inst.opcode != OP_DEBUG_LINE) {
            info.result = IR_VALID_INVALID_OPCODE;
            info.instruction_index = i;
            static char msg[64];
            snprintf(msg, sizeof(msg), "Invalid opcode: 0x%02X", inst.opcode);
            info.message = msg;
            return info;
        }
        
        // Validar saltos (básico)
        if (inst.opcode == OP_IR || inst.opcode == OP_SI || inst.opcode == OP_LLAMAR) {
            size_t target = 0;
            if (inst.opcode == OP_SI) {
                // Para OP_SI el target inmediato se codifica en B/C (u16)
                if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                    target = (size_t)inst.operand_b;
                    if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                        target |= ((size_t)inst.operand_c << 8);
                    }
                } else {
                    // No se puede validar target por registro aquí
                    target = 0;
                }
            } else {
                // OP_IR / OP_LLAMAR: target inmediato puede usar u24 en A/B/C
                if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                    target = (size_t)inst.operand_a;
                    if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) target |= ((size_t)inst.operand_b << 8);
                    if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) target |= ((size_t)inst.operand_c << 16);
                } else {
                    target = 0;
                }
            }

            if ((inst.opcode == OP_IR || inst.opcode == OP_LLAMAR) && !(inst.flags & IR_INST_FLAG_A_IMMEDIATE)) {
                // Target por registro: no validar aquí
            } else if (inst.opcode == OP_SI && !(inst.flags & IR_INST_FLAG_B_IMMEDIATE)) {
                // Target por registro: no validar aquí
            } else if (inst.flags & IR_INST_FLAG_RELATIVE) {
                size_t current_pc = i * IR_INSTRUCTION_SIZE;
                if (target > ir->header.code_size || current_pc + target >= ir->header.code_size) {
                    info.result = IR_VALID_INVALID_JUMP;
                    info.instruction_index = i;
                    info.message = "Jump target out of bounds";
                    return info;
                }
            } else {
                if (target >= ir->header.code_size) {
                    info.result = IR_VALID_INVALID_JUMP;
                    info.instruction_index = i;
                    info.message = "Jump target out of bounds";
                    return info;
                }
            }
        }

        if (!strict) {
            continue;
        }

        // JASB-SEC: kernel-only requiere flag de kernel
        if ((inst.flags & IR_INST_FLAG_KERNEL_ONLY) && !(ir->header.flags & IR_FLAG_KERNEL)) {
            info.result = IR_VALID_SECURITY;
            info.instruction_index = i;
            info.message = "Kernel-only instruction without kernel flag";
            return info;
        }

        // JASB-SEC: stack depth
        if (inst.opcode == OP_LLAMAR) {
            stack_depth++;
            if (stack_depth > max_stack) {
                info.result = IR_VALID_INVALID_STACK;
                info.instruction_index = i;
                info.message = "Stack depth exceeded";
                return info;
            }
        } else if (inst.opcode == OP_RETORNAR) {
            if (stack_depth > 0) {
                stack_depth--;
            }
        }

        // JASB-SEC: división por cero inmediata
        if (inst.opcode == OP_DIVIDIR && (inst.flags & IR_INST_FLAG_C_IMMEDIATE) && inst.operand_c == 0) {
            info.result = IR_VALID_SECURITY;
            info.instruction_index = i;
            info.message = "Immediate division by zero";
            return info;
        }

        // JASB-SEC: accesos inmediatos a memoria dentro de data_size
        if (inst.opcode == OP_LEER && (inst.flags & IR_INST_FLAG_B_IMMEDIATE)) {
            uint16_t addr = inst.operand_b;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                addr |= (uint16_t)(inst.operand_c << 8);
            }
            if ((uint64_t)addr + sizeof(uint64_t) > 64 * 1024) { // Relax for V1.0 (64KB memory limit)
                info.result = IR_VALID_SECURITY;
                info.instruction_index = i;
                info.message = "Immediate read out of bounds";
                return info;
            }
        }
        if (inst.opcode == OP_ESCRIBIR && (inst.flags & IR_INST_FLAG_A_IMMEDIATE)) {
            uint16_t addr = inst.operand_a;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                addr |= (uint16_t)(inst.operand_c << 8);
            }
            if ((uint64_t)addr + sizeof(uint64_t) > 64 * 1024) { // Relax for V1.0
                info.result = IR_VALID_SECURITY;
                info.instruction_index = i;
                info.message = "Immediate write out of bounds";
                return info;
            }
        }

        // JASB-SEC: saltos inmediatos alineados y dentro de límites
        if ((inst.opcode == OP_IR || inst.opcode == OP_LLAMAR) && (inst.flags & IR_INST_FLAG_A_IMMEDIATE)) {
            size_t current_pc = i * IR_INSTRUCTION_SIZE;
            size_t target = (size_t)inst.operand_a;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) target |= ((size_t)inst.operand_b << 8);
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) target |= ((size_t)inst.operand_c << 16);
            if (inst.flags & IR_INST_FLAG_RELATIVE) {
                size_t delta = (size_t)inst.operand_a;
                if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) delta |= ((size_t)inst.operand_b << 8);
                if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) delta |= ((size_t)inst.operand_c << 16);
                target = current_pc + delta;
                if (max_jump > 0 && delta > max_jump) {
                    info.result = IR_VALID_SECURITY;
                    info.instruction_index = i;
                    info.message = "Relative jump exceeds policy";
                    return info;
                }
            }
            if (target % IR_INSTRUCTION_SIZE != 0 || target >= ir->header.code_size) {
                info.result = IR_VALID_INVALID_JUMP;
                info.instruction_index = i;
                info.message = "Immediate jump not aligned or out of bounds";
                return info;
            }
        }
        if (inst.opcode == OP_SI && (inst.flags & IR_INST_FLAG_B_IMMEDIATE)) {
            size_t current_pc = i * IR_INSTRUCTION_SIZE;
            size_t target = (size_t)inst.operand_b;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) target |= ((size_t)inst.operand_c << 8);
            if (inst.flags & IR_INST_FLAG_RELATIVE) {
                size_t delta = (size_t)inst.operand_b;
                if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) delta |= ((size_t)inst.operand_c << 8);
                target = current_pc + delta;
                if (max_jump > 0 && delta > max_jump) {
                    info.result = IR_VALID_SECURITY;
                    info.instruction_index = i;
                    info.message = "Relative jump exceeds policy";
                    return info;
                }
            }
            if (target % IR_INSTRUCTION_SIZE != 0 || target >= ir->header.code_size) {
                info.result = IR_VALID_INVALID_JUMP;
                info.instruction_index = i;
                info.message = "Immediate conditional jump not aligned or out of bounds";
                return info;
            }
        }
    }
    
    return info;
}

IRValidationInfo ir_validate_file(const char* filename) {
    IRValidationInfo info = {IR_VALID_OK, 0, NULL};
    
    if (!filename) {
        info.result = IR_VALID_ERROR;
        info.message = "Filename is NULL";
        return info;
    }
    
    IRFile* ir = ir_file_create();
    if (!ir) {
        info.result = IR_VALID_ERROR;
        info.message = "Failed to create IR file";
        return info;
    }
    
    if (ir_file_read(ir, filename) != 0) {
        info.result = IR_VALID_ERROR;
        info.message = "Failed to read IR file";
        ir_file_destroy(ir);
        return info;
    }
    
    info = ir_validate_memory(ir);
    ir_file_destroy(ir);
    
    return info;
}
