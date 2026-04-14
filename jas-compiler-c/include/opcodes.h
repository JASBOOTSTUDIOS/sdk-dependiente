/* Opcodes IR - Paridad con sdk-dependiente/jasboot-ir/ir_format.h y opcodes.py
 * Nivel 8: Formato binario .jbo idéntico a ir_format.h
 */
#ifndef OPCODES_H
#define OPCODES_H

/* 8.1 Cabecera IR (ir_format.h IRHeader) */
#define IR_MAGIC_0 0x4A   /* 'J' */
#define IR_MAGIC_1 0x41   /* 'A' */
#define IR_MAGIC_2 0x53   /* 'S' */
#define IR_MAGIC_3 0x42   /* 'B' */
#define IR_VERSION_1      0x01
#define IR_ENDIAN_LE      0x00
#define IR_TARGET_GENERIC 0x00
#define IR_HEADER_SIZE    16
#define IR_INSTRUCTION_SIZE 5

#define IR_INST_FLAG_A_IMMEDIATE (1 << 0)
#define IR_INST_FLAG_B_IMMEDIATE (1 << 1)
#define IR_INST_FLAG_C_IMMEDIATE (1 << 2)
#define IR_INST_FLAG_RELATIVE    (1 << 3)
#define IR_INST_FLAG_A_REGISTER  (1 << 6)
#define IR_INST_FLAG_B_REGISTER  (1 << 7)
#define IR_INST_FLAG_C_REGISTER  0x00

#define OP_HALT           0x00
#define OP_MOVER          0x01
#define OP_LEER           0x02
#define OP_ESCRIBIR       0x03
#define OP_MOVER_U24      0x04
#define OP_LOAD_STR_HASH  0x05
#define OP_GET_FP         0x06   /* A <- FP (frame pointer) */
#define OP_DEBUG_LINE     0x07   /* VM guarda la linea actual en su estado (B|C) */

#define OP_SUMAR          0x10
#define OP_RESTAR         0x11
#define OP_MULTIPLICAR    0x12
#define OP_DIVIDIR        0x13
#define OP_MODULO         0x18

#define OP_CMP_EQ         0x31
#define OP_CMP_LT         0x32
#define OP_CMP_GT         0x33
#define OP_CMP_LE         0x34
#define OP_CMP_GE         0x35
#define OP_CMP_LT_FLT     0x36
#define OP_CMP_GT_FLT     0x37
#define OP_CMP_LE_FLT     0x3C
#define OP_CMP_GE_FLT     0x3D
#define OP_CMP_EQ_FLT     0x3E
#define OP_CMP_LT_U       0x38
#define OP_CMP_GT_U       0x39
#define OP_CMP_LE_U       0x3A
#define OP_CMP_GE_U       0x3B

#define OP_Y              0x20
#define OP_O              0x21
#define OP_XOR            0x22
#define OP_NO             0x23

#define OP_SUMAR_FLT      0x14
#define OP_RESTAR_FLT     0x15
#define OP_MULTIPLICAR_FLT 0x16
#define OP_DIVIDIR_FLT    0x17

#define OP_CONV_I2F       0x90
#define OP_CONV_F2I       0x91
#define OP_RAIZ           0x92
#define OP_FFI_OBTENER_SIMBOLO 0x93  /* A <- GetProcAddress(handle B, nombre en data offset reg C) */
#define OP_FFI_LLAMAR     0x94       /* A <- llamar fn reg B; args regs 3,4,5,6; C=num_args (0-4) */
#define OP_SIN            0x95      /* A <- sin(B) radianes */
#define OP_COS            0x96      /* A <- cos(B) radianes */
#define OP_TAN            0x97      /* A <- tan(B) radianes */
#define OP_ATAN2          0x99      /* A <- atan2(B, C) radianes (y, x) */
#define OP_MAT4_MUL_VEC4  0x9B      /* dest(A), mat(B), vec(C) = addrs en regs */
#define OP_MAT4_MUL       0x9C      /* dest(A), matL(B), matR(C) = addrs en regs */
#define OP_EXP            0x9D     /* A <- exp(B) flotante */
#define OP_LOG            0x9E     /* A <- log(B) flotante */
#define OP_LOG10          0x9F     /* A <- log10(B) flotante */
#define OP_MAT4_IDENTIDAD  0xA0     /* dest(A) = identidad */
#define OP_MAT4_TRANSPUESTA 0xA1    /* dest(A), src(B) en regs */
#define OP_MAT4_INVERSA    0xA2     /* dest(A), src(B) en regs */
#define OP_MAT3_MUL_VEC3   0xA3     /* dest(A), mat(B), vec(C) en regs */
#define OP_MAT3_MUL        0xA4     /* dest(A), matL(B), matR(C) en regs */
#define OP_MEM_REFORZAR_CONCEPTO   0xA5  /* A=reg id; C inm = magnitud 1..100 */
#define OP_MEM_PENALIZAR_CONCEPTO  0xA6
#define OP_MEM_CONSOLIDAR_SUENO    0xA7  /* A=reg éxito; B,C inm factor‰/umbral (ver VM) */
#define OP_MEM_OLVIDAR_DEBILES     0xA8  /* A=reg cuenta; C inm umbral milésimas */
#define OP_PERCEPCION_REGISTRAR    0xA9
#define OP_PERCEPCION_VENTANA      0xAA
#define OP_PERCEPCION_LIMPIAR      0xAB
#define OP_PERCEPCION_TAMANO       0xAC
#define OP_PERCEPCION_ANTERIOR     0xAD
#define OP_PERCEPCION_LISTA        0xAE
#define OP_RASTRO_ACTIVACION_VENTANA   0xAF
#define OP_RASTRO_ACTIVACION_LIMPIAR   0xCF
#define OP_RASTRO_ACTIVACION_TAMANO    0xB9
#define OP_RASTRO_ACTIVACION_OBTENER   0xBF
#define OP_RASTRO_ACTIVACION_PESO      0x6D
#define OP_RASTRO_ACTIVACION_LISTA     0x6E
#define OP_MEM_ELEGIR_POR_PESO_IDX     0x6F
#define OP_MEM_ELEGIR_POR_PESO_ID      0x70
#define OP_MEM_ELEGIR_POR_PESO_SEMILLA 0x71
#define OP_JSON_A_TEXTO      0x72
#define OP_JSON_A_ENTERO     0x73
#define OP_JSON_A_FLOTANTE   0x74
#define OP_JSON_A_BOOL       0x75
#define OP_JSON_TIPO         0x76
#define OP_CLOSURE_CREAR     0x77
#define OP_CLOSURE_CARGAR    0x78
#define OP_BYTES_CREAR       0x79
#define OP_BYTES_TAMANO      0x7A
#define OP_BYTES_OBTENER     0x7B
#define OP_BYTES_PONER       0x7C
#define OP_BYTES_ANEXAR      0x7D
#define OP_BYTES_PUNTERO     0x27
#define OP_BYTES_SUBBYTES    0x7F
#define OP_BYTES_DESDE_TEXTO 0x80
#define OP_BYTES_A_TEXTO     0x81
#define OP_DNS_RESOLVER      0x82
#define OP_TCP_CONECTAR      0x83
#define OP_TCP_ESCUCHAR      0x84
#define OP_TCP_ACEPTAR       0x85

#define OP_BIT_SHR        0x5E

#define OP_MEM_LISTA_CREAR   0xB0
#define OP_MEM_LISTA_AGREGAR 0xB1
#define OP_MEM_LISTA_OBTENER 0xB2
#define OP_MEM_LISTA_TAMANO  0xB3
#define OP_MEM_MAPA_CREAR    0x61
#define OP_MEM_MAPA_PONER    0x62
#define OP_MEM_MAPA_OBTENER  0x63
#define OP_MEM_MAPA_BORRAR   0xE3 /* A: map_id, B: key_id (penalizar se usa para borrar en mapas) */
#define OP_MEM_MAPA_TAMANO   0x7E /* A <- numero de entradas (B: map_id reg) */
#define OP_MEM_MAPA_CONTIENE 0x08 /* A <- 1 si clave C existe en mapa B, else 0 */

#define OP_IR             0x40
#define OP_SI             0x41
#define OP_LLAMAR         0x42
#define OP_RETORNAR       0x43
#define OP_RESERVAR_PILA  0x44
#define OP_HEAP_RESERVAR  0x45      /* A <- reservar(B bytes) */
#define OP_HEAP_LIBERAR   0x46      /* liberar(A) */
#define OP_ID_A_TEXTO     0x48      /* A <- Texto del ID B */

#define OP_IMPRIMIR_TEXTO 0x5B
#define OP_STR_REGISTRAR_LITERAL 0xE4

#define OP_IMPRIMIR_NUMERO  0xD3
#define OP_IMPRIMIR_FLOTANTE 0xEF
#define OP_MEM_IMPRIMIR_ID  0xDA
#define OP_BIT_SHL         0x5D

/* Nivel 6 - Llamadas de sistema */
#define OP_STR_CONCATENAR_REG  0xD7
#define OP_STR_LONGITUD        0xBA
#define OP_STR_DIVIDIR_TEXTO   0x5C
#define OP_MEM_CONTIENE_TEXTO_REG 0xE1
#define OP_MEM_TERMINA_CON_REG 0xE2
#define OP_STR_SUBTEXTO        0x6B
#define OP_STR_EXTRAER_CARACTER 0xEC
#define OP_STR_EXTRAER_ANTES_REG  0xD8
#define OP_STR_EXTRAER_DESPUES_REG 0xD9
#define OP_MEM_COPIAR_TEXTO    0xDB
#define OP_STR_COPIAR          0x51
#define OP_MEM_APRENDER_PESO_REG 0xE7
#define OP_MEM_OBTENER_VALOR   0xCB
#define OP_MEM_DECAE_CONEXIONES 0xCC
#define OP_MEM_PROPAGAR_ACTIVACION 0xCD
#define OP_IO_INPUT_REG        0xE5
#define OP_IO_PERCIBIR_TECLADO 0x6C
#define OP_IO_ENTRADA_FLOTANTE 0x8E
#define OP_IO_PAUSA            0x26
#define OP_PAUSA_MILISEGUNDOS  0x28
#define OP_SYS_TIMESTAMP       0x57
#define OP_FS_LEER_LINEA       0x58
#define OP_FS_ESCRIBIR         0x53
#define OP_FS_ABRIR            0x52
#define OP_FS_CERRAR           0x55
#define OP_FS_FIN_ARCHIVO      0x54
#define OP_FS_EXISTE           0x56
#define OP_FS_LEER_TEXTO       0xF6
#define OP_FS_ESCRIBIR_BYTE    0x60
#define OP_FS_LEER_BYTE      0x64 /* A <- fgetc(handle B) */
#define OP_FS_LISTAR           0x2A
#define OP_FS_BORRAR           0x2B
#define OP_FS_COPIAR           0x2C
#define OP_FS_MOVER            0x2D
#define OP_FS_TAMANO           0x2E
#define OP_CARGAR_BIBLIOTECA   0x2F  /* A <- handle; B|C = offset ruta en data */
#define OP_MEM_CREAR           0xBC
#define OP_MEM_CERRAR          0xBB
#define OP_MEM_ASOCIAR_CONCEPTOS 0xF2
#define OP_MEM_OBTENER_RELACION  0xF5
#define OP_MEM_OBTENER_RELACIONADOS 0xE9
#define OP_MEM_ES_VARIABLE_SISTEMA 0xEB
#define OP_MEM_OBTENER_TODOS   0xFE
#define OP_SYS_EXEC            0x5F
#define OP_SYS_ARGC            0x69
#define OP_SYS_ARGV            0x6A
#define OP_STR_DESDE_NUMERO    0x98
#define OP_STR_FLOTANTE_PREC   0x8F
#define OP_STR_CODIGO_CARACTER 0xEE
#define OP_STR_DESDE_CODIGO    0x9A
#define OP_STR_A_ENTERO        0xBD
#define OP_STR_A_FLOTANTE      0xBE
#define OP_STR_MINUSCULAS      0x50
#define OP_STR_MAYUSCULAS      0x4F
#define OP_MEM_LISTA_LIMPIAR   0xB6
#define OP_MEM_LISTA_LIBERAR   0x8D
#define OP_TCP_ENVIAR          0x19
#define OP_TCP_RECIBIR         0x1A
#define OP_TCP_CERRAR          0x1B
#define OP_TLS_CLIENTE         0x1C
#define OP_TLS_SERVIDOR        0x1D
#define OP_TLS_ENVIAR          0x1E
#define OP_TLS_RECIBIR         0x1F
#define OP_TLS_CERRAR          0x25
#define OP_JSON_PARSE         0x88
#define OP_JSON_STRINGIFY     0x89
#define OP_JSON_OBJETO_OBTENER 0x8A
#define OP_JSON_LISTA_OBTENER  0x8B
#define OP_JSON_LISTA_TAMANO   0x8C
#define OP_TRY_ENTER           0x86
#define OP_TRY_LEAVE           0x87
#define OP_MEM_LISTA_ID        0xB4
#define OP_MEM_PENSAR_RESPUESTA 0xB5
#define OP_LEER_U32_IND        0xEA
#define OP_MEM_OBTENER_FUERZA  0xED

/* Nivel 7 - Sentencias cognitivas */
#define OP_MEM_APRENDER_CONCEPTO 0xF0
#define OP_MEM_RECORDAR_TEXTO   0x5A
#define OP_ACTIVAR_MODULO       0xF9
#define OP_MEM_ULTIMA_PALABRA   0xD4

/* Secuencias / patrones (paridad ir_format.h) */
#define OP_MEM_REGISTRAR_PATRON   0xC2
#define OP_STR_ASOCIAR_SECUENCIA    0xC3
#define OP_MEM_PENSAR_SIGUIENTE     0xC4
#define OP_MEM_PENSAR_ANTERIOR      0xC5
#define OP_MEM_CORREGIR_SECUENCIA   0xC6
#define OP_MEM_ASOCIAR_RELACION     0xC7
#define OP_MEM_COMPARAR_PATRONES    0xC8
#define OP_MEM_BUSCAR_ASOCIADOS     0xC9
#define OP_MEM_BUSCAR_ASOCIADOS_LISTA 0xCA
#define OP_MEM_PROCESAR_TEXTO         0xDE
#define OP_MEM_PENSAR                 0xDF

#endif
