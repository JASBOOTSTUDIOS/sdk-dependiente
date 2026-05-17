#ifndef IR_FORMAT_H
#define IR_FORMAT_H

#include <stdint.h>
#include <stddef.h>

/* Cabecera IR */
#define IR_MAGIC_0 0x4A   /* 'J' */
#define IR_MAGIC_1 0x41   /* 'A' */
#define IR_MAGIC_2 0x53   /* 'S' */
#define IR_MAGIC_3 0x42   /* 'B' */
#define IR_VERSION_1      0x01
#define IR_ENDIAN_LE      0x00
#define IR_HEADER_SIZE    16
#define IR_INSTRUCTION_SIZE 5
#define IR_TARGET_GENERIC 0x00

/* Flags de cabecera */
#define IR_FLAG_IA_METADATA     (1u << 0)
#define IR_FLAG_SECURITY_STRICT (1u << 1)
#define IR_FLAG_KERNEL          (1u << 2)

/* Metadatos IA */
#define IR_IA_MAGIC_0 0x4A   /* 'J' */
#define IR_IA_MAGIC_1 0x41   /* 'A' */
#define IR_IA_MAGIC_2 0x53   /* 'S' */
#define IR_IA_MAGIC_3 0x49   /* 'I' - Jasb IA */
#define IR_IA_VERSION_1 0x01

#define IR_IA_TAG_PROFILE  0x01
#define IR_IA_TAG_BUILD_ID 0x02
#define IR_IA_TAG_JASB_SEC 0x03

typedef struct {
    uint8_t version;
    uint8_t mode;
    uint16_t max_stack;
    uint32_t max_jump;
} IRJasbSecPolicy;

/* Flags de instrucción */
#define IR_INST_FLAG_NONE         0x00
#define IR_INST_FLAG_A_IMMEDIATE  (1u << 0)
#define IR_INST_FLAG_B_IMMEDIATE  (1u << 1)
#define IR_INST_FLAG_C_IMMEDIATE  (1u << 2)
#define IR_INST_FLAG_RELATIVE     (1u << 3)
#define IR_INST_FLAG_SAFE         (1u << 4)
#define IR_INST_FLAG_KERNEL_ONLY  (1u << 5)
#define IR_INST_FLAG_C_REGISTER   (1u << 5)
#define IR_INST_FLAG_A_REGISTER   (1u << 6)
#define IR_INST_FLAG_B_REGISTER   (1u << 7)

#define IR_REGISTER_COUNT 256

#pragma pack(push, 1)
typedef struct {
    uint8_t magic[4];
    uint8_t version;
    uint8_t endian;
    uint8_t target;
    uint8_t flags;
    uint32_t code_size;
    uint32_t data_size;
} IRHeader;

typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint8_t operand_a;
    uint8_t operand_b;
    uint8_t operand_c;
} IRInstruction;
#pragma pack(pop)

typedef struct {
    IRHeader header;
    uint8_t* code;
    uint32_t code_count;
    uint8_t* data;
    uint32_t data_size;
    uint8_t* ia_metadata;
    uint32_t ia_metadata_size;
    size_t code_capacity;
    size_t data_capacity;
} IRFile;

IRFile* ir_file_create(void);
void ir_file_destroy(IRFile* ir);
int ir_file_read(IRFile* ir, const char* filename);
int ir_file_read_mem(IRFile* ir, const uint8_t* buffer, size_t size);
int ir_file_write(IRFile* ir, const char* filename);
int ir_file_add_instruction(IRFile* ir, IRInstruction* inst);
int ir_file_set_ia_metadata(IRFile* ir, const uint8_t* data, size_t size);
int ir_file_add_data(IRFile* ir, const uint8_t* data, size_t size, uint32_t* out_offset);
int ir_file_add_u64(IRFile* ir, uint64_t value, uint32_t* out_offset);
int ir_file_add_string(IRFile* ir, const char* text, uint32_t* out_offset);
int ir_file_save(IRFile* ir, const char* filename);
IRFile* ir_file_load(const char* filename);
int ir_file_read_memory(IRFile* ir, const uint8_t* data, size_t size);
int ir_file_serialize(IRFile* ir, uint8_t** out_data, size_t* out_size);

typedef enum {
    OP_HALT = 0x00,
    OP_MOVER = 0x01,
    OP_LEER = 0x02,
    OP_ESCRIBIR = 0x03,
    OP_MOVER_U24 = 0x04,
    OP_LOAD_STR_HASH = 0x05,
    OP_GET_FP = 0x06,
    OP_TLS_CERRAR = 0x07,
    OP_FS_LEER_TEXTO = 0x08,
    OP_FS_ESCRIBIR_TEXTO = 0x09,
    OP_TLS_IO = 0x0A,
    OP_MEM_OBTENER_TODOS = 0x0B,
    OP_DEBUG_LINE = 0x0C,
    OP_ANALITICA_MLP_FIT = 0x0D,
    OP_MEM_BUSCAR_INTROSPECTIVA_LISTA = 0x0E,
    OP_MEM_BUSCAR_INTROSPECTIVA_CS = 0x0F,

    OP_MEM_BUSCAR_INTROSPECTIVA_DETALLADA = 0x10,
    OP_MEM_MAPA_LLAVES = 0x11,
    OP_MEM_MAPA_CONTIENE = 0x12,
    OP_BYTES_PUNTERO = 0x13,
    OP_IMPRIMIR_BOOLEANO = 0x14,
    OP_SUMAR = 0x15,
    OP_RESTAR = 0x16,
    OP_MULTIPLICAR = 0x17,
    OP_DIVIDIR = 0x18,
    OP_MEM_CONFIGURAR_PESO_G = 0x19,
    OP_MEM_CONFIGURAR_PESOS_G_LISTA = 0x1A,
    OP_MEM_NORMALIZAR_PESOS_G = 0x1B,
    OP_SUMAR_FLT = 0x1C,
    OP_RESTAR_FLT = 0x1D,
    OP_MULTIPLICAR_FLT = 0x1E,
    OP_DIVIDIR_FLT = 0x1F,

    OP_MODULO = 0x20,
    OP_TCP_IO = 0x21,
    OP_TCP_CERRAR = 0x22,
    OP_STR_A_FLOTANTE = 0x23,
    OP_Y = 0x24,
    OP_O = 0x25,
    OP_XOR = 0x26,
    OP_NO = 0x27,
    OP_BIT_NOT = 0x28,
    OP_MEM_CARGAR_PERFIL_G = 0x29,
    OP_MEM_CONFIGURAR_MASK_G = 0x2A,
    OP_MEM_CONFIGURAR_MASKS_G_LISTA = 0x2B,
    OP_PAUSA_MILISEGUNDOS = 0x2C,
    OP_STR_FORMATEAR_TIMESTAMP = 0x2D,
    OP_FS_LISTAR = 0x2E,
    OP_FS_BORRAR = 0x2F,

    OP_FS_COPIAR = 0x30,
    OP_FS_MOVER = 0x31,
    OP_FS_TAMANO = 0x32,
    OP_TLS_CLIENTE = 0x33,
    OP_COMPARAR = 0x34,
    OP_CMP_EQ = 0x35,
    OP_CMP_LT = 0x36,
    OP_CMP_GT = 0x37,
    OP_CMP_LE = 0x38,
    OP_CMP_GE = 0x39,
    OP_CMP_LT_FLT = 0x3A,
    OP_CMP_GT_FLT = 0x3B,
    OP_CMP_LE_FLT = 0x3C,
    OP_CMP_GE_FLT = 0x3D,
    OP_CMP_EQ_FLT = 0x3E,
    OP_CMP_LT_U = 0x3F,

    OP_CMP_GT_U = 0x40,
    OP_CMP_LE_U = 0x41,
    OP_CMP_GE_U = 0x42,
    OP_MEM_BUSCAR_MAPA_ASOCIADOS = 0x43,
    OP_IR = 0x44,
    OP_SI = 0x45,
    OP_LLAMAR = 0x46,
    OP_RETORNAR = 0x47,
    OP_RESERVAR_PILA = 0x48,
    OP_HEAP_RESERVAR = 0x49,
    OP_HEAP_LIBERAR = 0x4A,
    OP_IR_ESCRIBIR = 0x4B,
    OP_ID_A_TEXTO = 0x4C,
    OP_STR_DESDE_ANY = 0x4D,
    OP_CONV_ANY2F = 0x4E,
    OP_CONV_ANY2I = 0x4F,

    OP_LANZAR = 0x50,
    OP_MEM_OBTENER_SECUENCIA = 0x51,
    OP_JSON_A_TEXTO = 0x52,
    OP_STR_MAYUSCULAS = 0x53,
    OP_STR_MINUSCULAS = 0x54,
    OP_MEM_CONFIGURAR_H_PARAM = 0x55,
    OP_FS_ABRIR = 0x56,
    OP_FS_ESCRIBIR = 0x57,
    OP_FS_FIN_ARCHIVO = 0x58,
    OP_FS_CERRAR = 0x59,
    OP_FS_EXISTE = 0x5A,
    OP_SYS_TIMESTAMP = 0x5B,
    OP_FS_LEER_LINEA = 0x5C,
    OP_STR_ASOCIAR_PESOS = 0x5D,
    OP_MEM_RECORDAR_TEXTO = 0x5E,
    OP_IMPRIMIR_TEXTO = 0x5F,

    OP_STR_DIVIDIR_TEXTO = 0x60,
    OP_BIT_SHL = 0x61,
    OP_BIT_SHR = 0x62,
    OP_SYS_EXEC = 0x63,
    OP_FS_ESCRIBIR_BYTE = 0x64,
    OP_MEM_MAPA_CREAR = 0x65,
    OP_MEM_MAPA_PONER = 0x66,
    OP_MEM_MAPA_OBTENER = 0x67,
    OP_FS_LEER_BYTE = 0x68,
    OP_MEM_CARGAR_PERFIL_G_FILE = 0x69,
    OP_MEM_CONFIGURAR_AUDITORIA = 0x6A,
    OP_MEM_OBTENER_AUDITORIA_JSON = 0x6B,
    OP_FS_LEER_ARCHIVO_REG = 0x6C,
    OP_FS_ESCRIBIR_ARCHIVO_REG = 0x6D,
    OP_FS_LEER_U32 = 0x6E,
    OP_SYS_ARGC = 0x6F,

    OP_SYS_ARGV = 0x70,
    OP_STR_SUBTEXTO = 0x71,
    OP_IO_PAUSA = 0x72,
    OP_IO_PERCIBIR_TECLADO = 0x73,
    OP_RASTRO_ACTIVACION_PESO = 0x74,
    OP_RASTRO_ACTIVACION_LISTA = 0x75,
    OP_MEM_ELEGIR_POR_PESO_IDX = 0x76,
    OP_MEM_ELEGIR_POR_PESO_ID = 0x77,
    OP_MEM_ELEGIR_POR_PESO_SEMILLA = 0x78,
    OP_STR_REEMPLAZAR = 0x79,
    OP_ANALITICA_MLP_PREDICT = 0x7A,
    OP_ANALITICA_MLP_SAVE = 0x7B,
    OP_MAI_CONTEXTO_LISTA = 0x7C,
    OP_JSON_TIPO = 0x7D,
    OP_CLOSURE_CREAR = 0x7E,
    OP_CLOSURE_CARGAR = 0x7F,

    OP_BYTES_CREAR = 0x80,
    OP_BYTES_TAMANO = 0x81,
    OP_BYTES_OBTENER = 0x82,
    OP_BYTES_PONER = 0x83,
    OP_BYTES_DESDE_TEXTO = 0x84,
    OP_MEM_MAPA_TAMANO = 0x85,
    OP_BYTES_SUBBYTES = 0x86,
    OP_BYTES_A_TEXTO = 0x87,
    OP_DNS_RESOLVER = 0x88,
    OP_TCP_CONECTAR = 0x89,
    OP_TCP_ESCUCHAR = 0x8A,
    OP_TCP_ACEPTAR = 0x8B,
    OP_TRY_ENTER = 0x8C,
    OP_TRY_LEAVE = 0x8D,
    OP_JSON_PARSE = 0x8E,
    OP_JSON_STRINGIFY = 0x8F,

    OP_JSON_OBJETO_OBTENER = 0x90,
    OP_JSON_LISTA_OBTENER = 0x91,
    OP_JSON_LISTA_TAMANO = 0x92,
    OP_MEM_LISTA_LIBERAR = 0x93,
    OP_IO_ENTRADA_FLOTANTE = 0x94,
    OP_CARGAR_BIBLIOTECA = 0x95,
    OP_CONV_I2F = 0x96,
    OP_CONV_F2I = 0x97,
    OP_RAIZ = 0x98,
    OP_FFI_OBTENER_SIMBOLO = 0x99,
    OP_FFI_LLAMAR = 0x9A,
    OP_SIN = 0x9B,
    OP_COS = 0x9C,
    OP_TAN = 0x9D,
    OP_STR_DESDE_NUMERO = 0x9E,
    OP_ATAN2 = 0x9F,

    OP_STR_DESDE_CODIGO = 0xA0,
    OP_MAT4_MUL_VEC4 = 0xA1,
    OP_MAT4_MUL = 0xA2,
    OP_EXP = 0xA3,
    OP_LOG = 0xA4,
    OP_LOG10 = 0xA5,
    OP_MAT4_IDENTIDAD = 0xA6,
    OP_MAT4_TRANSPUESTA = 0xA7,
    OP_MAT4_INVERSA = 0xA8,
    OP_MAT3_MUL_VEC3 = 0xA9,
    OP_MAT3_MUL = 0xAA,
    OP_MEM_REFORZAR_CONCEPTO = 0xAB,
    OP_MEM_PENALIZAR_CONCEPTO = 0xAC,
    OP_MEM_CONSOLIDAR_SUENO = 0xAD,
    OP_MEM_OLVIDAR_DEBILES = 0xAE,
    OP_PERCEPCION_REGISTRAR = 0xAF,

    OP_PERCEPCION_VENTANA = 0xB0,
    OP_PERCEPCION_LIMPIAR = 0xB1,
    OP_PERCEPCION_TAMANO = 0xB2,
    OP_PERCEPCION_ANTERIOR = 0xB3,
    OP_PERCEPCION_LISTA = 0xB4,
    OP_RASTRO_ACTIVACION_VENTANA = 0xB5,
    OP_MEM_LISTA_CREAR = 0xB6,
    OP_MEM_LISTA_AGREGAR = 0xB7,
    OP_MEM_LISTA_OBTENER = 0xB8,
    OP_MEM_LISTA_TAMANO = 0xB9,
    OP_MEM_LISTA_ID = 0xBA,
    OP_MEM_PENSAR_RESPUESTA = 0xBB,
    OP_MEM_LISTA_LIMPIAR = 0xBC,
    OP_MEM_LISTA_PONER = 0xBD,
    OP_MEM_LISTA_UNIR = 0xBE,
    OP_STR_LONGITUD = 0xBF,

    OP_RASTRO_ACTIVACION_TAMANO = 0xC0,
    OP_MEM_CERRAR = 0xC1,
    OP_MEM_CREAR = 0xC2,
    OP_MEM_PENALIZAR = 0xC3,
    OP_STR_A_ENTERO = 0xC4,
    OP_RASTRO_ACTIVACION_OBTENER = 0xC5,
    OP_ESTABLECER_CONTEXTO = 0xC6,
    OP_STR_FLOTANTE_PREC = 0xC7,
    OP_MEM_REGISTRAR_PATRON = 0xC8,
    OP_STR_ASOCIAR_SECUENCIA = 0xC9,
    OP_MEM_PENSAR_SIGUIENTE = 0xCA,
    OP_MEM_PENSAR_ANTERIOR = 0xCB,
    OP_MEM_CORREGIR_SECUENCIA = 0xCC,
    OP_MEM_ASOCIAR_RELACION = 0xCD,
    OP_MEM_COMPARAR_PATRONES = 0xCE,
    OP_MEM_BUSCAR_ASOCIADOS = 0xCF,

    OP_MEM_BUSCAR_ASOCIADOS_LISTA = 0xD0,
    OP_MEM_OBTENER_VALOR = 0xD1,
    OP_MEM_BUSCAR_INTROSPECTIVA = 0xD2,
    OP_MEM_DECAE_CONEXIONES = 0xD3,
    OP_MEM_PROPAGAR_ACTIVACION = 0xD4,
    OP_RASTRO_ACTIVACION_LIMPIAR = 0xD5,
    OP_STR_EXTRAER_ANTES = 0xD6,
    OP_STR_EXTRAER_DESPUES = 0xD7,
    OP_STR_CONCATENAR = 0xD8,
    OP_IMPRIMIR_NUMERO = 0xD9,
    OP_MEM_ULTIMA_PALABRA = 0xDA,
    OP_MEM_TERMINA_CON = 0xDB,
    OP_MEM_ULTIMA_SILABA = 0xDC,
    OP_STR_CONCATENAR_REG = 0xDD,
    OP_STR_EXTRAER_ANTES_REG = 0xDE,
    OP_STR_EXTRAER_DESPUES_REG = 0xDF,

    OP_MEM_IMPRIMIR_ID = 0xE0,
    OP_MEM_COPIAR_TEXTO = 0xE1,
    OP_MEM_CONTIENE_TEXTO = 0xE2,
    OP_MEM_COMPARAR_TEXTO = 0xE3,
    OP_MEM_PROCESAR_TEXTO = 0xE4,
    OP_MEM_PENSAR = 0xE5,
    OP_IO_INGRESAR_TEXTO = 0xE6,
    OP_MEM_CONTIENE_TEXTO_REG = 0xE7,
    OP_MEM_TERMINA_CON_REG = 0xE8,
    OP_MEM_MAPA_BORRAR = 0xE9,
    OP_STR_REGISTRAR_LITERAL = 0xEA,
    OP_IO_INPUT_REG = 0xEB,
    OP_MEM_BUSCAR_PESO_REG = 0xEC,
    OP_MEM_APRENDER_PESO_REG = 0xED,
    OP_MEM_ASOCIAR = 0xEE,
    OP_MEM_OBTENER_RELACIONADOS = 0xEF,

    OP_LEER_U32_IND = 0xF0,
    OP_STR_EXTRAER_CARACTER = 0xF1,
    OP_CONFIGURAR_REGLAS_CONTEXTO = 0xF2,
    OP_MEM_OBTENER_FUERZA = 0xF3,
    OP_STR_CODIGO_CARACTER = 0xF4,
    OP_IMPRIMIR_FLOTANTE = 0xF5,
    OP_MEM_ES_VARIABLE_SISTEMA = 0xF6,
    OP_ACTIVAR_MODULO = 0xF7,
    OP_MEM_APRENDER_CONCEPTO = 0xF8,
    OP_MEM_BUSCAR_CONCEPTO = 0xF9,
    OP_MEM_ASOCIAR_CONCEPTOS = 0xFA,
    OP_MEM_ACTUALIZAR_PESO = 0xFB,
    OP_MEM_OBTENER_ASOCIACIONES = 0xFC,
    OP_MEM_OBTENER_RELACION = 0xFD,
    OP_TLS_SERVIDOR = 0xFE,
    OP_MEM_INFERIR_MIL = 0xFF
} VMOpcode;

#ifndef OP_NOP
#define OP_NOP 0x00
#endif

#endif
