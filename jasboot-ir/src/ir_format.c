#include "ir_format.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

IRFile* ir_file_create(void) {
    IRFile* ir = calloc(1, sizeof(IRFile));
    if (!ir) return NULL;
    
    // Inicializar header
    ir->header.magic[0] = IR_MAGIC_0;
    ir->header.magic[1] = IR_MAGIC_1;
    ir->header.magic[2] = IR_MAGIC_2;
    ir->header.magic[3] = IR_MAGIC_3;
    ir->header.version = IR_VERSION_1;
    ir->header.endian = IR_ENDIAN_LE;
    ir->header.target = IR_TARGET_GENERIC;
    ir->header.flags = 0;
    ir->header.code_size = 0;
    ir->header.data_size = 0;
    
    // Inicializar buffers
    ir->code_capacity = 1024;
    ir->data_capacity = 256;
    ir->code = malloc(ir->code_capacity);
    ir->data = malloc(ir->data_capacity);
    ir->ia_metadata = NULL;
    ir->ia_metadata_size = 0;
    
    if (!ir->code || !ir->data) {
        ir_file_destroy(ir);
        return NULL;
    }
    
    ir->code_count = 0;
    return ir;
}

void ir_file_destroy(IRFile* ir) {
    if (!ir) return;
    if (ir->code) free(ir->code);
    if (ir->data) free(ir->data);
    if (ir->ia_metadata) free(ir->ia_metadata);
    free(ir);
}

int ir_file_write_header(IRFile* ir, FILE* f) {
    if (!ir || !f) return -1;
    
    // Escribir magic
    if (fwrite(ir->header.magic, 1, 4, f) != 4) return -1;
    
    // Escribir version, endian, target, flags
    if (fputc(ir->header.version, f) == EOF) return -1;
    if (fputc(ir->header.endian, f) == EOF) return -1;
    if (fputc(ir->header.target, f) == EOF) return -1;
    if (fputc(ir->header.flags, f) == EOF) return -1;
    
    // Escribir code_size y data_size (little-endian)
    uint32_t code_size_le = ir->header.code_size;
    uint32_t data_size_le = ir->header.data_size;
    
    if (fwrite(&code_size_le, 4, 1, f) != 1) return -1;
    if (fwrite(&data_size_le, 4, 1, f) != 1) return -1;
    
    return 0;
}

int ir_file_read_header(IRFile* ir, FILE* f) {
    if (!ir || !f) return -1;
    
    // Leer magic
    uint8_t magic[4];
    if (fread(magic, 1, 4, f) != 4) return -1;
    
    // Validar magic
    if (magic[0] != IR_MAGIC_0 || magic[1] != IR_MAGIC_1 ||
        magic[2] != IR_MAGIC_2 || magic[3] != IR_MAGIC_3) {
/*
        printf("[DEBUG] Magic inválido: %02X %02X %02X %02X (esperado %02X %02X %02X %02X)\n",
               magic[0], magic[1], magic[2], magic[3],
               IR_MAGIC_0, IR_MAGIC_1, IR_MAGIC_2, IR_MAGIC_3);
*/
        return -1;  // Magic inválido
    }
    
    memcpy(ir->header.magic, magic, 4);
    
    // Leer version, endian, target, flags
    int version = fgetc(f);
    int endian = fgetc(f);
    int target = fgetc(f);
    int flags = fgetc(f);
    
    if (version == EOF || endian == EOF || target == EOF || flags == EOF) {
        return -1;
    }
    
    ir->header.version = version;
    ir->header.endian = endian;
    ir->header.target = target;
    ir->header.flags = flags;
    
    // Leer code_size y data_size
    if (fread(&ir->header.code_size, 4, 1, f) != 1) return -1;
    if (fread(&ir->header.data_size, 4, 1, f) != 1) return -1;
    
    return 0;
}

int ir_file_add_instruction(IRFile* ir, IRInstruction* inst) {
    if (!ir || !inst) return -1;
    
    // Verificar capacidad
    size_t needed = (ir->code_count + 1) * IR_INSTRUCTION_SIZE;
    if (needed > ir->code_capacity) {
        // Redimensionar
        size_t new_capacity = ir->code_capacity * 2;
        uint8_t* new_code = realloc(ir->code, new_capacity);
        if (!new_code) return -1;
        ir->code = new_code;
        ir->code_capacity = new_capacity;
    }
    
    // Escribir instrucción
    size_t offset = ir->code_count * IR_INSTRUCTION_SIZE;
    ir->code[offset + 0] = inst->opcode;
    ir->code[offset + 1] = inst->flags;
    ir->code[offset + 2] = inst->operand_a;
    ir->code[offset + 3] = inst->operand_b;
    ir->code[offset + 4] = inst->operand_c;
    
    ir->code_count++;
    ir->header.code_size = ir->code_count * IR_INSTRUCTION_SIZE;
    
    return 0;
}

int ir_file_set_ia_metadata(IRFile* ir, const uint8_t* data, size_t size) {
    if (!ir) return -1;
    
    // Liberar metadata anterior si existe
    if (ir->ia_metadata) {
        free(ir->ia_metadata);
        ir->ia_metadata = NULL;
        ir->ia_metadata_size = 0;
        ir->header.flags &= ~IR_FLAG_IA_METADATA;
    }
    
    if (data && size > 0) {
        ir->ia_metadata = malloc(size);
        if (!ir->ia_metadata) return -1;
        memcpy(ir->ia_metadata, data, size);
        ir->ia_metadata_size = size;
        ir->header.flags |= IR_FLAG_IA_METADATA;
    }
    
    return 0;
}

static int ir_file_reservar_data(IRFile* ir, size_t size, size_t* out_offset) {
    if (!ir || size == 0) return -1;
    
    size_t offset = ir->header.data_size;
    size_t needed = offset + size;
    if (needed > ir->data_capacity) {
        size_t new_capacity = ir->data_capacity ? ir->data_capacity : 256;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }
        uint8_t* new_data = realloc(ir->data, new_capacity);
        if (!new_data) return -1;
        ir->data = new_data;
        ir->data_capacity = new_capacity;
    }
    
    if (out_offset) {
        *out_offset = offset;
    }
    
    return 0;
}

int ir_file_add_data(IRFile* ir, const uint8_t* data, size_t size, size_t* out_offset) {
    if (!ir || !data || size == 0) return -1;
    
    if (ir_file_reservar_data(ir, size, out_offset) != 0) return -1;
    
    size_t offset = ir->header.data_size;
    memcpy(ir->data + offset, data, size);
    ir->header.data_size += (uint32_t)size;
    
    return 0;
}

int ir_file_add_u64(IRFile* ir, uint64_t value, size_t* out_offset) {
    uint8_t buffer[8];
    memcpy(buffer, &value, sizeof(buffer));
    return ir_file_add_data(ir, buffer, sizeof(buffer), out_offset);
}

int ir_file_add_string(IRFile* ir, const char* text, size_t* out_offset) {
    if (!ir || !text) return -1;
    size_t len = strlen(text) + 1; // incluir terminador nulo
    return ir_file_add_data(ir, (const uint8_t*)text, len, out_offset);
}

int ir_file_write(IRFile* ir, const char* filename) {
    if (!ir || !filename) return -1;
    
    FILE* f = fopen(filename, "wb");
    if (!f) return -1;
    
    // Escribir header
    if (ir_file_write_header(ir, f) != 0) {
        fclose(f);
        return -1;
    }
    
    // Escribir metadata IA si está presente
    if (ir->header.flags & IR_FLAG_IA_METADATA && ir->ia_metadata_size > 0) {
        uint32_t ia_size_le = (uint32_t)ir->ia_metadata_size;
        if (fwrite(&ia_size_le, 4, 1, f) != 1) {
            fclose(f);
            return -1;
        }
        if (fwrite(ir->ia_metadata, 1, ir->ia_metadata_size, f) != ir->ia_metadata_size) {
            fclose(f);
            return -1;
        }
    }
    
    // Escribir código
    if (ir->header.code_size > 0) {
        if (fwrite(ir->code, 1, ir->header.code_size, f) != ir->header.code_size) {
            fclose(f);
            return -1;
        }
    }
    
    // Escribir datos
    if (ir->header.data_size > 0) {
        if (fwrite(ir->data, 1, ir->header.data_size, f) != ir->header.data_size) {
            fclose(f);
            return -1;
        }
    }
    
    fclose(f);
    return 0;
}

int ir_file_read(IRFile* ir, const char* filename) {
    if (!ir || !filename) return -1;
    
    // printf("[DEBUG] Abriendo archivo IR: %s\n", filename);
    FILE* f = fopen(filename, "rb");
    if (!f) {
//        perror("[DEBUG] Error al abrir archivo IR con fopen");
        return -1;
    }
    
    // Leer header
    if (ir_file_read_header(ir, f) != 0) {
//        printf("[DEBUG] Error al leer cabecera IR\n");
        fclose(f);
        return -1;
    }
//    printf("[DEBUG] Cabecera IR cargada: code_size=%u, data_size=%u\n", ir->header.code_size, ir->header.data_size);
    
    // Leer metadata IA si está presente
    if (ir->header.flags & IR_FLAG_IA_METADATA) {
        uint32_t ia_size;
        if (fread(&ia_size, 4, 1, f) != 1) {
            fclose(f);
            return -1;
        }
        ir->ia_metadata_size = ia_size;
        ir->ia_metadata = malloc(ia_size);
        if (!ir->ia_metadata) {
            fclose(f);
            return -1;
        }
        if (fread(ir->ia_metadata, 1, ia_size, f) != ia_size) {
            fclose(f);
            return -1;
        }
    } else {
        ir->ia_metadata = NULL;
        ir->ia_metadata_size = 0;
    }
    
    // Leer código
    if (ir->header.code_size > 0) {
        if (ir->header.code_size > ir->code_capacity) {
            ir->code_capacity = ir->header.code_size;
            ir->code = realloc(ir->code, ir->code_capacity);
            if (!ir->code) {
                fclose(f);
                return -1;
            }
        }
        if (fread(ir->code, 1, ir->header.code_size, f) != ir->header.code_size) {
            fclose(f);
            return -1;
        }
        ir->code_count = ir->header.code_size / IR_INSTRUCTION_SIZE;
    }
    
    // Leer datos
    if (ir->header.data_size > 0) {
        if (ir->header.data_size > ir->data_capacity) {
            ir->data_capacity = ir->header.data_size;
            ir->data = realloc(ir->data, ir->data_capacity);
            if (!ir->data) {
                fclose(f);
                return -1;
            }
        }
        if (fread(ir->data, 1, ir->header.data_size, f) != ir->header.data_size) {
            fclose(f);
            return -1;
        }
    }
    
    fclose(f);
    return 0;
}

int ir_file_read_memory(IRFile* ir, const uint8_t* buf, size_t len) {
    if (!ir || !buf || len < IR_HEADER_SIZE) return -1;
    if (buf[0] != IR_MAGIC_0 || buf[1] != IR_MAGIC_1 || buf[2] != IR_MAGIC_2 || buf[3] != IR_MAGIC_3)
        return -1;
    ir->header.magic[0] = buf[0];
    ir->header.magic[1] = buf[1];
    ir->header.magic[2] = buf[2];
    ir->header.magic[3] = buf[3];
    ir->header.version = buf[4];
    ir->header.endian = buf[5];
    ir->header.target = buf[6];
    ir->header.flags = buf[7];
    memcpy(&ir->header.code_size, buf + 8, 4);
    memcpy(&ir->header.data_size, buf + 12, 4);
    size_t off = IR_HEADER_SIZE;
    if (ir->header.flags & IR_FLAG_IA_METADATA) {
        if (off + 4 > len) return -1;
        uint32_t ia_size = 0;
        memcpy(&ia_size, buf + off, 4);
        off += 4;
        if (ia_size > (uint32_t)(64u * 1024 * 1024) || off + ia_size > len) return -1;
        uint8_t* copy = (uint8_t*)malloc(ia_size);
        if (!copy) return -1;
        memcpy(copy, buf + off, ia_size);
        off += ia_size;
        free(ir->ia_metadata);
        ir->ia_metadata = copy;
        ir->ia_metadata_size = ia_size;
    } else {
        if (ir->ia_metadata) {
            free(ir->ia_metadata);
            ir->ia_metadata = NULL;
        }
        ir->ia_metadata_size = 0;
    }
    uint32_t csz = ir->header.code_size;
    uint32_t dsz = ir->header.data_size;
    if (off + (size_t)csz + (size_t)dsz > len) return -1;
    if (csz > 0) {
        if (csz > ir->code_capacity) {
            uint8_t* nc = (uint8_t*)realloc(ir->code, csz);
            if (!nc) return -1;
            ir->code = nc;
            ir->code_capacity = csz;
        }
        memcpy(ir->code, buf + off, csz);
        off += csz;
        ir->code_count = csz / IR_INSTRUCTION_SIZE;
    } else {
        ir->code_count = 0;
    }
    if (dsz > 0) {
        if (dsz > ir->data_capacity) {
            uint8_t* nd = (uint8_t*)realloc(ir->data, dsz);
            if (!nd) return -1;
            ir->data = nd;
            ir->data_capacity = dsz;
        }
        memcpy(ir->data, buf + off, dsz);
    }
    return 0;
}

int ir_file_serialize(IRFile* ir, uint8_t** out_buf, size_t* out_len) {
    if (!ir || !out_buf || !out_len) return -1;
    size_t ia_extra = 0;
    if ((ir->header.flags & IR_FLAG_IA_METADATA) && ir->ia_metadata && ir->ia_metadata_size > 0)
        ia_extra = 4 + ir->ia_metadata_size;
    size_t total = IR_HEADER_SIZE + ia_extra + (size_t)ir->header.code_size + (size_t)ir->header.data_size;
    uint8_t* out = (uint8_t*)malloc(total);
    if (!out) return -1;
    size_t off = 0;
    memcpy(out + off, ir->header.magic, 4);
    off += 4;
    out[off++] = ir->header.version;
    out[off++] = ir->header.endian;
    out[off++] = ir->header.target;
    out[off++] = ir->header.flags;
    {
        uint32_t csz = ir->header.code_size;
        uint32_t dsz = ir->header.data_size;
        memcpy(out + off, &csz, 4);
        off += 4;
        memcpy(out + off, &dsz, 4);
        off += 4;
    }
    if (ia_extra) {
        uint32_t ia_le = (uint32_t)ir->ia_metadata_size;
        memcpy(out + off, &ia_le, 4);
        off += 4;
        memcpy(out + off, ir->ia_metadata, ir->ia_metadata_size);
        off += ir->ia_metadata_size;
    }
    if (ir->header.code_size > 0 && ir->code) {
        memcpy(out + off, ir->code, ir->header.code_size);
        off += ir->header.code_size;
    }
    if (ir->header.data_size > 0 && ir->data) {
        memcpy(out + off, ir->data, ir->header.data_size);
        off += ir->header.data_size;
    }
    *out_buf = out;
    *out_len = off;
    return 0;
}

static int ir_append_tlv(uint8_t** buffer, size_t* size, uint8_t tag,
                         const uint8_t* payload, uint16_t payload_len) {
    if (!buffer || !size) return -1;
    size_t new_size = *size + 1 + 2 + payload_len;
    uint8_t* out = realloc(*buffer, new_size);
    if (!out) return -1;
    out[*size] = tag;
    out[*size + 1] = (uint8_t)(payload_len & 0xFF);
    out[*size + 2] = (uint8_t)((payload_len >> 8) & 0xFF);
    if (payload_len > 0 && payload) {
        memcpy(out + *size + 3, payload, payload_len);
    }
    *buffer = out;
    *size = new_size;
    return 0;
}

int ir_build_ia_metadata(uint8_t** out, size_t* out_size,
                         const char* profile,
                         const char* build_id,
                         const IRJasbSecPolicy* policy) {
    if (!out || !out_size) return -1;
    *out = NULL;
    *out_size = 0;

    uint8_t* buffer = malloc(8);
    if (!buffer) return -1;
    buffer[0] = IR_IA_MAGIC_0;
    buffer[1] = IR_IA_MAGIC_1;
    buffer[2] = IR_IA_MAGIC_2;
    buffer[3] = IR_IA_MAGIC_3;
    buffer[4] = IR_IA_VERSION_1;
    buffer[5] = 0;
    buffer[6] = 0;
    buffer[7] = 0;
    size_t size = 8;

    if (profile && profile[0]) {
        uint16_t len = (uint16_t)strlen(profile);
        if (ir_append_tlv(&buffer, &size, IR_IA_TAG_PROFILE,
                          (const uint8_t*)profile, len) != 0) {
            free(buffer);
            return -1;
        }
    }

    if (build_id && build_id[0]) {
        uint16_t len = (uint16_t)strlen(build_id);
        if (ir_append_tlv(&buffer, &size, IR_IA_TAG_BUILD_ID,
                          (const uint8_t*)build_id, len) != 0) {
            free(buffer);
            return -1;
        }
    }

    if (policy) {
        uint8_t payload[8];
        payload[0] = policy->version;
        payload[1] = policy->mode;
        payload[2] = (uint8_t)(policy->max_stack & 0xFF);
        payload[3] = (uint8_t)((policy->max_stack >> 8) & 0xFF);
        payload[4] = (uint8_t)(policy->max_jump & 0xFF);
        payload[5] = (uint8_t)((policy->max_jump >> 8) & 0xFF);
        payload[6] = (uint8_t)((policy->max_jump >> 16) & 0xFF);
        payload[7] = (uint8_t)((policy->max_jump >> 24) & 0xFF);
        if (ir_append_tlv(&buffer, &size, IR_IA_TAG_JASB_SEC, payload, sizeof(payload)) != 0) {
            free(buffer);
            return -1;
        }
    }

    *out = buffer;
    *out_size = size;
    return 0;
}
