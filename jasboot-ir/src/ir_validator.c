#include "reader_ir.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo.jbo>\n", argv[0]);
        return 1;
    }
    
    const char* filename = argv[1];
    
    printf("🔍 jasboot IR Validator\n");
    printf("========================\n\n");
    printf("Validando: %s\n\n", filename);
    
    IRValidationInfo info = ir_validate_file(filename);
    
    if (info.result == IR_VALID_OK) {
        printf("✅ Archivo IR válido\n");
        
        // Mostrar información adicional
        IRFile* ir = ir_file_create();
        if (ir && ir_file_read(ir, filename) == 0) {
            printf("\nInformación del archivo:\n");
            printf("  Versión: 0x%02X\n", ir->header.version);
            printf("  Instrucciones: %zu\n", ir->code_count);
            printf("  Tamaño código: %u bytes\n", ir->header.code_size);
            printf("  Tamaño datos: %u bytes\n", ir->header.data_size);
            printf("  Flags: 0x%02X", ir->header.flags);
            if (ir->header.flags & IR_FLAG_IA_METADATA) {
                printf(" (IA Metadata presente)");
            }
            printf("\n");

            if (ir->header.flags & IR_FLAG_IA_METADATA && ir->ia_metadata && ir->ia_metadata_size >= 8) {
                const uint8_t* data = ir->ia_metadata;
                if (data[0] == IR_IA_MAGIC_0 && data[1] == IR_IA_MAGIC_1 &&
                    data[2] == IR_IA_MAGIC_2 && data[3] == IR_IA_MAGIC_3 &&
                    data[4] == IR_IA_VERSION_1) {
                    size_t offset = 8;
                    while (offset + 3 <= ir->ia_metadata_size) {
                        uint8_t tag = data[offset];
                        uint16_t len = (uint16_t)data[offset + 1] | ((uint16_t)data[offset + 2] << 8);
                        offset += 3;
                        if (offset + len > ir->ia_metadata_size) break;
                        if (tag == IR_IA_TAG_JASB_SEC && len == 8) {
                            IRJasbSecPolicy policy;
                            policy.version = data[offset + 0];
                            policy.mode = data[offset + 1];
                            policy.max_stack = (uint16_t)data[offset + 2] |
                                               ((uint16_t)data[offset + 3] << 8);
                            policy.max_jump = (uint32_t)data[offset + 4] |
                                              ((uint32_t)data[offset + 5] << 8) |
                                              ((uint32_t)data[offset + 6] << 16) |
                                              ((uint32_t)data[offset + 7] << 24);
                            const char* mode = policy.mode == 2 ? "strict" :
                                               policy.mode == 1 ? "warn" : "off";
                            printf("  JASB-SEC: %s (max_stack=%u, max_jump=%u)\n",
                                   mode, policy.max_stack, policy.max_jump);
                            break;
                        }
                        offset += len;
                    }
                }
            }
            
            ir_file_destroy(ir);
        }
        
        return 0;
    } else {
        printf("❌ Archivo IR inválido\n");
        printf("  Error: %s\n", ir_validation_result_to_string(info.result));
        if (info.message) {
            printf("  Detalle: %s\n", info.message);
        }
        if (info.instruction_index > 0) {
            printf("  Instrucción: %zu\n", info.instruction_index);
        }
        return 1;
    }
}
