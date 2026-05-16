#ifndef IR_FORMAT_H
#define IR_FORMAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// Magic number: "JASB"
#define IR_MAGIC_0 0x4A  // 'J'
#define IR_MAGIC_1 0x41  // 'A'
#define IR_MAGIC_2 0x53  // 'S'
#define IR_MAGIC_3 0x42  // 'B'

// Versión del formato
#define IR_VERSION_1 0x01

// Endianness
#define IR_ENDIAN_LE 0x00  // Little-endian

// Target
#define IR_TARGET_GENERIC 0x00  // IR genérico

// Flags del header
#define IR_FLAG_DEBUG (1 << 0)
#define IR_FLAG_KERNEL (1 << 1)
#define IR_FLAG_IA_METADATA (1 << 2)
#define IR_FLAG_SECURITY_STRICT (1 << 3)

// Flags de instrucción
#define IR_INST_FLAG_A_IMMEDIATE (1 << 0)
#define IR_INST_FLAG_B_IMMEDIATE (1 << 1)
#define IR_INST_FLAG_C_IMMEDIATE (1 << 2)
#define IR_INST_FLAG_RELATIVE (1 << 3)
#define IR_INST_FLAG_SAFE (1 << 4)
#define IR_INST_FLAG_KERNEL_ONLY (1 << 5)
#define IR_INST_FLAG_A_REGISTER (1 << 6)
#define IR_INST_FLAG_B_REGISTER (1 << 7)
#define IR_INST_FLAG_C_REGISTER (1 << 5) // Reutilizamos bit de kernel para indicar C como registro

// IA metadata (estructura extendida)
#define IR_IA_MAGIC_0 'I'
#define IR_IA_MAGIC_1 'A'
#define IR_IA_MAGIC_2 '0'
#define IR_IA_MAGIC_3 '1'

#define IR_IA_VERSION_1 0x01

// IA metadata tags
#define IR_IA_TAG_PROFILE 0x01
#define IR_IA_TAG_BUILD_ID 0x02
#define IR_IA_TAG_JASB_SEC 0x10

typedef struct {
    uint8_t version;
    uint8_t mode;
    uint32_t max_stack;
    uint32_t max_jump;
} IRJasbSecPolicy;

// Tamaño del header
#define IR_HEADER_SIZE 16

// Tamaño de cada instrucción
#define IR_INSTRUCTION_SIZE 5

// Número de registros virtuales
#define IR_REGISTER_COUNT 256

// Header del IR binario
typedef struct __attribute__((packed)) {
    uint8_t magic[4];      // "JASB"
    uint8_t version;       // Versión del formato
    uint8_t endian;        // Endianness
    uint8_t target;        // Target (genérico por ahora)
    uint8_t flags;         // Flags
    uint32_t code_size;    // Tamaño del código
    uint32_t data_size;    // Tamaño de datos
} IRHeader;

// Instrucción IR (5 bytes fijos)
typedef struct __attribute__((packed)) {
    uint8_t opcode;        // Opcode
    uint8_t flags;         // Flags de instrucción
    uint8_t operand_a;     // Operando A (registro o inmediato)
    uint8_t operand_b;     // Operando B (registro o inmediato)
    uint8_t operand_c;     // Operando C (registro o inmediato)
} IRInstruction;

// Opcodes
typedef enum {
    OP_HALT = 0x00,        // Detener la VM
    // Transferencia
    OP_MOVER = 0x01,       // A ← B
    OP_LEER = 0x02,        // A ← [B]
    OP_ESCRIBIR = 0x03,    // [A] ← B
    OP_MOVER_U24 = 0x04,   // A <- B|C|FLAGS (24 bits)
    OP_LOAD_STR_HASH = 0x05, // A ← hash(string en Data[B|C])
    OP_GET_FP = 0x06,      // A ← FP
    OP_DEBUG_LINE = 0x07,  // Guarda línea actual en estado de la VM (B|C)
    /* A <- MSE float (ultimo epoch); B = registro base de 7 args (pesos_id, sesgos_id, X_id, y_id, capas_id, lr f32, epochs u32). */
    OP_ANALITICA_MLP_FIT = 0x08,
    OP_MEM_BUSCAR_INTROSPECTIVA_LISTA = 0x09, // A <- lista_id con IDs; B=termino_id, C=max_resultados
    OP_MEM_BUSCAR_INTROSPECTIVA_CS = 0x0A, // A <- primer ID; B=termino_id, C=case_sensitive(0/1)
    OP_MEM_BUSCAR_INTROSPECTIVA_DETALLADA = 0x0B,
    OP_MEM_MAPA_LLAVES = 0x0C,
    OP_MEM_MAPA_CONTIENE = 0x0D,
    OP_BYTES_PUNTERO = 0x0E,
    OP_IMPRIMIR_BOOLEANO = 0x0F,
    
    // Aritmética
    OP_SUMAR = 0x10,       // A ← B + C
    OP_RESTAR = 0x11,      // A ← B - C
    OP_MULTIPLICAR = 0x12, // A ← B * C
    OP_DIVIDIR = 0x13,     // A ← B / C
    OP_MEM_CONFIGURAR_PESO_G = 0x14,
    OP_MEM_CONFIGURAR_PESOS_G_LISTA = 0x15,
    OP_MEM_NORMALIZAR_PESOS_G = 0x16,
    OP_SUMAR_FLT = 0x17,
    OP_RESTAR_FLT = 0x18,
    OP_MULTIPLICAR_FLT = 0x19,
    OP_DIVIDIR_FLT = 0x1A,
    OP_MODULO = 0x1B,
    OP_TCP_ENVIAR = 0x1C,
    OP_TCP_RECIBIR = 0x1D,
    OP_TCP_CERRAR = 0x1E,
    OP_STR_A_FLOTANTE = 0x1F, // Sincronizado con opcodes.h
    
    OP_Y = 0x20,           // A ← B & C
    OP_O = 0x21,           // A ← B | C
    OP_XOR = 0x22,         // A ← B ^ C
    OP_NO = 0x23,          // A ← ¬B
    OP_BIT_NOT = 0x24,     // A ← ~B
    OP_MEM_CARGAR_PERFIL_G = 0x25,
    OP_MEM_CONFIGURAR_MASK_G = 0x26,
    OP_MEM_CONFIGURAR_MASKS_G_LISTA = 0x27,
    OP_PAUSA_MILISEGUNDOS = 0x28,
    OP_STR_FORMATEAR_TIMESTAMP = 0x29,
    OP_FS_LISTAR = 0x2A,
    OP_FS_BORRAR = 0x2B,
    OP_FS_COPIAR = 0x2C,
    OP_FS_MOVER = 0x2D,
    OP_FS_TAMANO = 0x2E,
    OP_TLS_CLIENTE = 0x2F, // Sincronizado con opcodes.h original (aprox)
    
    // Comparación
    OP_COMPARAR = 0x30,    // A ← compare(B, C)
    OP_CMP_EQ = 0x31,      // A ← (B == C) ? 1 : 0
    OP_CMP_LT = 0x32,      // A ← (B <  C) ? 1 : 0
    OP_CMP_GT = 0x33,      // A ← (B >  C) ? 1 : 0
    OP_CMP_LE = 0x34,      // A ← (B <= C) ? 1 : 0
    OP_CMP_GE = 0x35,      // A ← (B >= C) ? 1 : 0
    OP_CMP_LT_FLT = 0x36,  // A ← (float)B < (float)C
    OP_CMP_GT_FLT = 0x37,  // A ← (float)B > (float)C
    OP_CMP_LE_FLT = 0x38,  // A ← (float)B <= (float)C
    OP_CMP_GE_FLT = 0x39,  // A ← (float)B >= (float)C
    OP_CMP_EQ_FLT = 0x3A,  // A ← ((float)B == (float)C) ? 1 : 0
    OP_CMP_LT_U = 0x3B,    // A ← (uint64)B < (uint64)C
    OP_CMP_GT_U = 0x3C,    // A ← (uint64)B > (uint64)C
    OP_CMP_LE_U = 0x3D,    // A ← (uint64)B <= (uint64)C
    OP_CMP_GE_U = 0x3E,    // A ← (uint64)B >= (uint64)C
    OP_MEM_BUSCAR_MAPA_ASOCIADOS = 0x3F,

    // Control de flujo
    OP_IR = 0x40,          // PC ← A
    OP_SI = 0x41,          // si A ≠ 0 → PC ← B
    OP_LLAMAR = 0x42,      // push PC; PC ← A
    OP_RETORNAR = 0x43,    // pop PC
    OP_RESERVAR_PILA = 0x44, // reservar pila para variables locales
    OP_HEAP_RESERVAR = 0x45, // A <- reservar(B bytes) - heap
    OP_HEAP_LIBERAR = 0x46,  // liberar(A)
    OP_IR_ESCRIBIR = 0x47,   // Escribir IR actual a archivo (ruta: reg A = id concepto)
    OP_ID_A_TEXTO = 0x48,    // A <- Texto del ID B
    OP_STR_DESDE_ANY = 0x49,
    OP_CONV_ANY2F = 0x4A,
    OP_CONV_ANY2I = 0x4B,
    OP_LANZAR = 0x4C,
    OP_MEM_OBTENER_SECUENCIA = 0x4D,
    OP_JSON_A_TEXTO = 0x4E,
    OP_STR_MAYUSCULAS = 0x4F,

    OP_STR_MINUSCULAS = 0x50,
    OP_STR_COPIAR = 0x51,
    OP_FS_ABRIR = 0x52,
    OP_FS_ESCRIBIR = 0x53,
    OP_FS_FIN_ARCHIVO = 0x54,
    OP_FS_CERRAR = 0x55,
    OP_FS_EXISTE = 0x56,
    OP_SYS_TIMESTAMP = 0x57,
    OP_FS_LEER_LINEA = 0x58,
    OP_STR_ASOCIAR_PESOS = 0x59,
    OP_MEM_RECORDAR_TEXTO = 0x5A,
    OP_IMPRIMIR_TEXTO = 0x5B,
    OP_STR_DIVIDIR_TEXTO = 0x5C,
    OP_BIT_SHL = 0x5D,
    OP_BIT_SHR = 0x5E,
    OP_SYS_EXEC = 0x5F,

    OP_FS_ESCRIBIR_BYTE = 0x60,
    OP_MEM_MAPA_CREAR = 0x61,
    OP_MEM_MAPA_PONER = 0x62,
    OP_MEM_MAPA_OBTENER = 0x63,
    OP_FS_LEER_BYTE = 0x64,
    OP_FS_ESCRIBIR_U32 = 0x65,
    OP_FS_LEER_ARCHIVO_REG = 0x66,
    OP_FS_ESCRIBIR_ARCHIVO_REG = 0x67,
    OP_FS_LEER_U32 = 0x68,
    OP_SYS_ARGC = 0x69,
    OP_SYS_ARGV = 0x6A,
    OP_STR_SUBTEXTO = 0x6B,
    OP_IO_PERCIBIR_TECLADO = 0x6C,
    OP_RASTRO_ACTIVACION_PESO = 0x6D,
    OP_RASTRO_ACTIVACION_LISTA = 0x6E,
    OP_MEM_ELEGIR_POR_PESO_IDX = 0x6F,

    OP_MEM_ELEGIR_POR_PESO_ID = 0x70,
    OP_MEM_ELEGIR_POR_PESO_SEMILLA = 0x71,
    OP_STR_REEMPLAZAR = 0x72,
    OP_ANALITICA_MLP_PREDICT = 0x73,
    OP_ANALITICA_MLP_SAVE = 0x74,
    OP_MAI_CONTEXTO_LISTA = 0x75,
    OP_JSON_TIPO = 0x76,
    OP_CLOSURE_CREAR = 0x77,
    OP_CLOSURE_CARGAR = 0x78,
    OP_BYTES_CREAR = 0x79,
    OP_BYTES_TAMANO = 0x7A,
    OP_BYTES_OBTENER = 0x7B,
    OP_BYTES_PONER = 0x7C,
    OP_BYTES_ANEXAR = 0x7D,
    OP_MEM_MAPA_TAMANO = 0x7E,
    OP_BYTES_SUBBYTES = 0x7F,

    OP_BYTES_DESDE_TEXTO = 0x80,
    OP_BYTES_A_TEXTO = 0x81,
    OP_DNS_RESOLVER = 0x82,
    OP_TCP_CONECTAR = 0x83,
    OP_TCP_ESCUCHAR = 0x84,
    OP_TCP_ACEPTAR = 0x85,
    OP_TRY_ENTER = 0x86,
    OP_TRY_LEAVE = 0x87,
    OP_JSON_PARSE = 0x88,
    OP_JSON_STRINGIFY = 0x89,
    OP_JSON_OBJETO_OBTENER = 0x8A,
    OP_JSON_LISTA_OBTENER = 0x8B,
    OP_JSON_LISTA_TAMANO = 0x8C,
    OP_MEM_LISTA_LIBERAR = 0x8D,
    OP_IO_ENTRADA_FLOTANTE = 0x8E,
    OP_CARGAR_BIBLIOTECA = 0x8F, // Movido para evitar colisión con TLS_CLIENTE
    
    // Conversión
    OP_CONV_I2F = 0x90,
    OP_CONV_F2I = 0x91,    // Conversión flotante -> entero
    OP_RAIZ = 0x92,        // A <- sqrt(B) flotante
    OP_FFI_OBTENER_SIMBOLO = 0x93,
    OP_FFI_LLAMAR = 0x94,
    OP_SIN = 0x95,         // A <- sin(B) radianes, flotante
    OP_COS = 0x96,         // A <- cos(B) radianes, flotante
    OP_TAN = 0x97,         // A <- tan(B) radianes, flotante
    OP_STR_DESDE_NUMERO = 0x98, // A ← string(B); C inm/reg: 0=float, 1=entero, 2=bool→"verdadero"/"falso"
    OP_ATAN2 = 0x99,       // A <- atan2(B=y, C=x) radianes, flotante
    OP_STR_DESDE_CODIGO = 0x9A,
    OP_MAT4_MUL_VEC4 = 0x9B,  // dest(A), mat(B), vec(C) = direcciones en bytes (regs)
    OP_MAT4_MUL = 0x9C,        // Restaurado a su valor original
    OP_EXP = 0x9D,             // A <- exp(B) flotante
    OP_LOG = 0x9E,             // A <- log(B) flotante
    OP_LOG10 = 0x9F,           // A <- log10(B) flotante

    OP_MAT4_IDENTIDAD = 0xA0,  // dest(A) = matriz identidad
    OP_MAT4_TRANSPUESTA = 0xA1, // dest(A), src(B) = direcciones en regs
    OP_MAT4_INVERSA = 0xA2,    // dest(A), src(B) = direcciones en regs
    OP_MAT3_MUL_VEC3 = 0xA3,   // dest(A), mat(B), vec(C) = direcciones en regs
    OP_MAT3_MUL = 0xA4,        // dest(A), matL(B), matR(C) = direcciones en regs
    OP_MEM_REFORZAR_CONCEPTO = 0xA5,  // A: reg id concepto; C inmediato 1..100 → delta = C/100 a aristas incidentes
    OP_MEM_PENALIZAR_CONCEPTO = 0xA6, // Igual; resta delta, fuerza mínima 0
    OP_MEM_CONSOLIDAR_SUENO = 0xA7,   // Sueño: decaer + olvidar + consolidar (ver vm.c)
    OP_MEM_OLVIDAR_DEBILES = 0xA8,    // Solo elimina aristas con fuerza ≤ umbral
    OP_PERCEPCION_REGISTRAR = 0xA9,   // B reg = id concepto a añadir al buffer circular
    OP_PERCEPCION_VENTANA = 0xAA,     // A=ok; capacidad = B|C inm (u16 LE, 0→64)
    OP_PERCEPCION_LIMPIAR = 0xAB,
    OP_PERCEPCION_TAMANO = 0xAC,      // A <- count
    OP_PERCEPCION_ANTERIOR = 0xAD,    // A <- id en índice B (0=más reciente)
    OP_PERCEPCION_LISTA = 0xAE,       // A <- id lista episódica (orden reciente→antiguo)
    OP_RASTRO_ACTIVACION_VENTANA = 0xAF,  // Cap B|C u16 LE (16..2048, 0→128); A=ok

    // Listas / Memoria Episódica
    OP_MEM_LISTA_CREAR = 0xB0,       // A <- Crear lista
    OP_MEM_LISTA_AGREGAR = 0xB1,     // A (nombre/ID) <- Agregar valor B
    OP_MEM_LISTA_OBTENER = 0xB2,     // A <- Obtener de lista B en index C
    OP_MEM_LISTA_TAMANO = 0xB3,      // A <- Tamaño de lista B
    OP_MEM_LISTA_ID = 0xB4,          // A <- Obtener ID interno de lista
    OP_MEM_PENSAR_RESPUESTA = 0xB5,  // A <- PensarRespuesta(B)
    OP_MEM_LISTA_LIMPIAR    = 0xB6,  // A <- Limpiar lista
    OP_MEM_LISTA_PONER = 0xB7,       // ListSet(A:list_id, B:index, C:val_reg)
    OP_MEM_LISTA_UNIR = 0xB8,        // A <- Unir(B, C)
    OP_RASTRO_ACTIVACION_TAMANO = 0xB9,
    OP_STR_LONGITUD = 0xBA,
    OP_MEM_CERRAR = 0xBB,
    OP_MEM_CREAR = 0xBC,
    OP_MEM_PENALIZAR = 0xBD,
    OP_STR_A_ENTERO = 0xBE,
    OP_RASTRO_ACTIVACION_OBTENER = 0xBF,

    OP_ESTABLECER_CONTEXTO = 0xC0,
    OP_STR_FLOTANTE_PREC = 0xC1,
    OP_MEM_REGISTRAR_PATRON = 0xC2, // A: id_lista -> Registra patrón secuencial
    OP_STR_ASOCIAR_SECUENCIA = 0xC3, // Asociar secuencia
    OP_MEM_PENSAR_SIGUIENTE = 0xC4,  // Pensar siguiente
    OP_MEM_PENSAR_ANTERIOR = 0xC5,   // Pensar anterior
    OP_MEM_CORREGIR_SECUENCIA = 0xC6, // Corregir secuencia
    OP_MEM_ASOCIAR_RELACION = 0xC7,   // Asociar relación (similitud/oposición)
    OP_MEM_COMPARAR_PATRONES = 0xC8,  // A: id_lista_a, B: id_lista_b -> reg C (similitud)
    OP_MEM_BUSCAR_ASOCIADOS = 0xC9,   // A <- mejor asociado de B (tipo C); umbral 0.1, prof 2
    OP_MEM_BUSCAR_ASOCIADOS_LISTA = 0xCA, // A <- lista con top-K ids asociados a B; C = tipo|(K<<8)
    OP_MEM_OBTENER_VALOR = 0xCB,      // A = valor en clave B (recordar); tipo ASOCIACION; si no hay, A=B
    OP_MEM_BUSCAR_INTROSPECTIVA = 0xCC, // A <- primer ID que contiene texto B (case insensitive)
    OP_MEM_DECAE_CONEXIONES = 0xCD,   // Decaimiento global; A=reg ok; B,C inm opcional factor%/1000‰ umbral
    OP_MEM_PROPAGAR_ACTIVACION = 0xCE, // A <- mejor id; B=origen; C=tipo|(K<<8)|(prof<<16) o *_mai+RELATIVE: máscara|(K<<16)|(prof<<24)
    OP_RASTRO_ACTIVACION_LIMPIAR = 0xCF,  // Vacía rastro manual

    OP_STR_EXTRAER_ANTES = 0xD0,     // Extraer texto antes de patrón
    OP_STR_EXTRAER_DESPUES = 0xD1,   // Extraer texto después de patrón
    OP_STR_CONCATENAR = 0xD2,
    OP_IMPRIMIR_NUMERO = 0xD3,       // Imprime valor numérico de un registro
    OP_MEM_ULTIMA_PALABRA = 0xD4,   // Extrae última palabra (concept ID -> dest_var_addr)
    OP_MEM_TERMINA_CON = 0xD5,      // Verifica sufijo (id_frase, id_sufijo) -> reg
    OP_MEM_ULTIMA_SILABA = 0xD6,    // Extrae última sílaba (concept ID -> dest_var_addr)
    OP_STR_CONCATENAR_REG = 0xD7,    // Concatenar regs B y C en destino A
    OP_STR_EXTRAER_ANTES_REG = 0xD8,   // Extraer (regs: A=frase, B=patron) -> C=dest_reg
    OP_STR_EXTRAER_DESPUES_REG = 0xD9, // Extraer (regs: A=frase, B=patron) -> C=dest_reg
    OP_MEM_IMPRIMIR_ID = 0xDA,       // Imprime texto de un concepto desde ID en registro
    OP_MEM_COPIAR_TEXTO = 0xDB,      // Copia texto de concepto B a concepto A
    OP_MEM_CONTIENE_TEXTO = 0xDC,    // Verifica si concepto A contiene concepto B -> reg
    OP_MEM_COMPARAR_TEXTO = 0xDD,    // Compara dos conceptos (id_a, id_b) -> resultado en reg
    OP_MEM_PROCESAR_TEXTO = 0xDE,    // Procesa texto cognitivamente (id)
    OP_MEM_PENSAR = 0xDF,            // Razonamiento multipath (BFS)

    OP_IO_INGRESAR_TEXTO = 0xE0, // Lee stdin y guarda texto en memoria neuronal (id desde data)
    OP_MEM_CONTIENE_TEXTO_REG = 0xE1, // A ← B contains C (B and C are reg IDs)
    OP_MEM_TERMINA_CON_REG = 0xE2,    // A ← B ends with C (B and C are reg IDs)
    OP_MEM_MAPA_BORRAR = 0xE3,
    OP_STR_REGISTRAR_LITERAL = 0xE4, // Registrar string literal desde data
    OP_IO_INPUT_REG = 0xE5,        // Leer texto de stdin -> Reg A (ID generado)
    OP_MEM_BUSCAR_PESO_REG = 0xE6, // Buscar peso usando ID en registro A -> DestAddr[B|C]
    OP_MEM_APRENDER_PESO_REG = 0xE7, // Aprender peso dinámico
    OP_MEM_ASOCIAR = 0xE8,           // Asociar legacy; si IR_INST_FLAG_SAFE y !A_IMM → mai_contexto (ver vm.c)
    OP_MEM_OBTENER_RELACIONADOS = 0xE9, // A <- GetRelated(A), Store at B|C
    OP_LEER_U32_IND = 0xEA,           // A <- *u32(Reg B)
    OP_MEM_ES_VARIABLE_SISTEMA = 0xEB,
    OP_STR_EXTRAER_CARACTER = 0xEC,
    OP_MEM_OBTENER_FUERZA = 0xED,
    OP_STR_CODIGO_CARACTER = 0xEE,
    OP_IMPRIMIR_FLOTANTE = 0xEF,

    // Sistema / IA / Memoria Neuronal
    OP_MEM_APRENDER_CONCEPTO = 0xF0,    // A: reg_id, B: reg_weight, C: 0
    OP_MEM_BUSCAR_CONCEPTO = 0xF1,      // A: reg_id, B: reg_dest, C: 0
    OP_MEM_ASOCIAR_CONCEPTOS = 0xF2,    // A: reg_id1, B: reg_id2, C: reg_weight
    OP_MEM_ACTUALIZAR_PESO = 0xF3,      // A: reg_id, B: reg_weight, C: 0
    OP_MEM_OBTENER_ASOCIACIONES = 0xF4, // A: reg_id, B: reg_dest_count, C: 0
    OP_MEM_OBTENER_RELACION = 0xF5,     // A: dest, B: id1, C: id2
    
    // TLS
    OP_TLS_SERVIDOR = 0xF6,
    OP_TLS_ENVIAR = 0xF7,
    OP_TLS_RECIBIR = 0xF8,
    OP_TLS_CERRAR = 0xF9,
    OP_IO_PAUSA = 0xFA,
    OP_FS_LEER_TEXTO = 0xFB,
    OP_FS_ESCRIBIR_TEXTO = 0xFC,
    OP_ACTIVAR_MODULO = 0xFD,
    OP_MEM_OBTENER_TODOS = 0xFE,      // A <- Obtener lista de todos los IDs
    
    OP_NOP = 0xFF             // No operation
} IROpcode;

// Estructura para leer/escribir IR
typedef struct {
    IRHeader header;
    uint8_t* code;         // Código (instrucciones)
    uint8_t* data;         // Datos
    uint8_t* ia_metadata;  // Metadata IA (opcional)
    size_t code_capacity;  // Capacidad del buffer de código
    size_t data_capacity;  // Capacidad del buffer de datos
    size_t ia_metadata_size; // Tamaño de metadata IA
    size_t code_count;     // Número de instrucciones
} IRFile;

// Utilidades para manejar IRFile
IRFile* ir_file_create(void);
void ir_file_destroy(IRFile* ir);
int ir_file_write_header(IRFile* ir, FILE* f);
int ir_file_read_header(IRFile* ir, FILE* f);
int ir_file_write(IRFile* ir, const char* filename);
int ir_file_read(IRFile* ir, const char* filename);
int ir_file_read_memory(IRFile* ir, const uint8_t* buf, size_t len);
int ir_file_save(IRFile* ir, const char* filename);
IRFile* ir_file_load(const char* filename);
int ir_file_add_u64(IRFile* ir, uint64_t val, uint32_t* out_offset);
int ir_file_add_string(IRFile* ir, const char* text, uint32_t* out_offset);
int ir_file_add_data(IRFile* ir, const uint8_t* data, size_t size, uint32_t* out_offset);
int ir_file_serialize(IRFile* ir, uint8_t** out_buf, size_t* out_len);

#endif // IR_FORMAT_H
