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
    
    // Aritmética
    OP_SUMAR = 0x10,       // A ← B + C
    OP_RESTAR = 0x11,      // A ← B - C
    OP_MULTIPLICAR = 0x12, // A ← B * C
    OP_DIVIDIR = 0x13,     // A ← B / C
    OP_SUMAR_FLT = 0x14,   // A ← (float)B + (float)C
    OP_RESTAR_FLT = 0x15,  // A ← (float)B - (float)C
    OP_MULTIPLICAR_FLT = 0x16, // A ← (float)B * (float)C
    OP_DIVIDIR_FLT = 0x17,     // A ← (float)B / (float)C
    OP_MODULO = 0x18,          // A ← B % C
    
    // Lógica
    OP_Y = 0x20,           // A ← B & C
    OP_O = 0x21,           // A ← B | C
    OP_XOR = 0x22,         // A ← B ^ C
    OP_NO = 0x23,          // A ← ¬B
    OP_BIT_NOT = 0x24,     // A ← ~B
    
    // Comparación
    OP_COMPARAR = 0x30,    // A ← compare(B, C)
    OP_CMP_EQ = 0x31,      // A ← (B == C) ? 1 : 0
    OP_CMP_LT = 0x32,      // A ← (B <  C) ? 1 : 0
    OP_CMP_GT = 0x33,      // A ← (B >  C) ? 1 : 0
    OP_CMP_LE = 0x34,      // A ← (B <= C) ? 1 : 0
    OP_CMP_GE = 0x35,      // A ← (B >= C) ? 1 : 0
    OP_CMP_LT_FLT = 0x36,  // A ← (float)B < (float)C
    OP_CMP_GT_FLT = 0x37,  // A ← (float)B > (float)C
    OP_CMP_LE_FLT = 0x3C,  // A ← (float)B <= (float)C (semántica C / IEEE orden parcial)
    OP_CMP_GE_FLT = 0x3D,  // A ← (float)B >= (float)C
    OP_CMP_EQ_FLT = 0x3E,  // A ← ((float)B == (float)C) ? 1 : 0 (semántica C; NaN == NaN es falso)
    OP_CMP_LT_U = 0x38,    // A ← (uint64)B < (uint64)C
    OP_CMP_GT_U = 0x39,    // A ← (uint64)B > (uint64)C
    OP_CMP_LE_U = 0x3A,    // A ← (uint64)B <= (uint64)C
    OP_CMP_GE_U = 0x3B,    // A ← (uint64)B >= (uint64)C

    // Control de flujo
    OP_IR = 0x40,          // PC ← A
    OP_SI = 0x41,          // si A ≠ 0 → PC ← B
    OP_LLAMAR = 0x42,      // push PC; PC ← A
    OP_RETORNAR = 0x43,    // pop PC
    OP_RESERVAR_PILA = 0x44, // reservar pila para variables locales
    OP_HEAP_RESERVAR = 0x45, // A <- reservar(B bytes) - heap
    OP_HEAP_LIBERAR = 0x46,  // liberar(A)
    OP_IR_ESCRIBIR = 0x47,   // Escribir IR actual a archivo (ruta: reg A = id concepto)

    // Conversión
    OP_CONV_I2F = 0x90,    // Conversión entero -> flotante
    OP_CONV_F2I = 0x91,    // Conversión flotante -> entero
    OP_RAIZ = 0x92,        // A <- sqrt(B) flotante
    OP_SIN = 0x95,         // A <- sin(B) radianes, flotante
    OP_COS = 0x96,         // A <- cos(B) radianes, flotante
    OP_TAN = 0x97,         // A <- tan(B) radianes, flotante
    OP_STR_FLOTANTE_PREC = 0x8F, // A ← id texto: formatear (float)B con C decimales (C inm 0..20 o reg entero)
    OP_STR_DESDE_NUMERO = 0x98, // A ← string(B)
    OP_ATAN2 = 0x99,       // A <- atan2(B=y, C=x) radianes, flotante
    OP_MAT4_MUL_VEC4 = 0x9B,  // dest(A), mat(B), vec(C) = direcciones en bytes (regs)
    OP_MAT4_MUL = 0x9C,        // dest(A), matL(B), matR(C) = direcciones en bytes (regs)
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
    OP_RASTRO_ACTIVACION_LIMPIAR = 0xCF,  // Vacía rastro manual
    OP_RASTRO_ACTIVACION_TAMANO = 0xB9,   // A <- count
    OP_RASTRO_ACTIVACION_OBTENER = 0xBF,  // A <- id en índice B (0=primer toque en la invocación)
    OP_RASTRO_ACTIVACION_PESO = 0x6D,     // A <- bits float32 activación en índice B
    OP_RASTRO_ACTIVACION_LISTA = 0x6E,    // A <- id lista episódica (orden cronológico viejo→nuevo)
    OP_MEM_ELEGIR_POR_PESO_IDX = 0x6F,   // A <- índice ganador (lista en mem_colecciones); B=contexto, C=lista_id; máx 32
    OP_MEM_ELEGIR_POR_PESO_ID = 0x70,    // A <- id concepto ganador (misma semántica que 0x6F)
    OP_MEM_ELEGIR_POR_PESO_SEMILLA = 0x71, // A=ok; B reg o inm: semilla u32 para desempate (0 = solo score+id+índice)

    // Sistema / IA
    OP_IO_INGRESAR_TEXTO = 0xE0, // Lee stdin y guarda texto en memoria neuronal (id desde data)
    OP_IO_INPUT_REG = 0xE5,        // Leer texto de stdin -> Reg A (ID generado)
    OP_MEM_BUSCAR_PESO_REG = 0xE6, // Buscar peso usando ID en registro A -> DestAddr[B|C]
    OP_MEM_BUSCAR_PESO = 0xE6,      // Alias compatible
    
    // Sistema / IA / Memoria Neuronal
    OP_MEM_APRENDER_CONCEPTO = 0xF0,    // A: reg_id, B: reg_weight, C: 0
    OP_MEM_BUSCAR_CONCEPTO = 0xF1,      // A: reg_id, B: reg_dest, C: 0
    OP_MEM_ASOCIAR_CONCEPTOS = 0xF2,    // A: reg_id1, B: reg_id2, C: reg_weight
    OP_MEM_ACTUALIZAR_PESO = 0xF3,      // A: reg_id, B: reg_weight, C: 0
    OP_MEM_OBTENER_ASOCIACIONES = 0xF4, // A: reg_id, B: reg_dest_count, C: 0
    OP_MEM_OBTENER_RELACION = 0xF5,     // A: dest, B: id1, C: id2
    
    OP_FS_LEER_TEXTO = 0xF6,      // Leer archivo a concepto (ruta + id)
    OP_FS_ESCRIBIR_TEXTO = 0xF7,  // Escribir archivo desde concepto (ruta + id)
    OP_MEM_IMPRIMIR_CONCEPTO = 0xF8, // Imprime texto de un concepto (id) con fallback a nombre
    OP_ACTIVAR_MODULO = 0xF9,        // Activa un módulo (ruta en data)
    OP_ESTABLECER_CONTEXTO = 0xFA,   // Establece contexto (nombre en data)
    OP_USA_CONCEPTO = 0xFB,          // Declara uso de concepto (nombre en data)
    OP_ASOCIADO_CON = 0xFC,          // Declara asociación (nombre en data)
    OP_MEM_COMPARAR_TEXTO = 0xDD,    // Compara dos conceptos (id_a, id_b) -> resultado en reg
    OP_MEM_PROCESAR_TEXTO = 0xDE,    // Procesa texto cognitivamente (id)
    OP_MEM_CONTIENE_TEXTO = 0xDC,    // Verifica si concepto A contiene concepto B -> reg
    OP_MEM_CONTIENE_TEXTO_REG = 0xE1, // A ← B contains C (B and C are reg IDs)
    OP_MEM_TERMINA_CON_REG = 0xE2,    // A ← B ends with C (B and C are reg IDs)
    OP_LEER_U32_IND = 0xEA,           // A ← *u32(Reg B)
    OP_MEM_OBTENER_RELACIONADOS = 0xE9, // A ← GetRelated(A), Store at B|C

    OP_MEM_COPIAR_TEXTO = 0xDB,      // Copia texto de concepto B a concepto A
    OP_MEM_PENSAR = 0xDF,            // Razonamiento multipath (BFS)
    OP_MEM_IMPRIMIR_ID = 0xDA,       // Imprime texto de un concepto desde ID en registro
    OP_MEM_RECORDAR_TEXTO = 0x5A,    // Guardar texto en memoria neuronal
    OP_IMPRIMIR_TEXTO = 0x5B,        // Imprime texto desde sección de datos
    OP_STR_DIVIDIR_TEXTO = 0x5C,     // Dividir texto (B=id_frase, C=id_sep) -> A=id_lista
    OP_BIT_SHL = 0x5D,               // A <- B << C
    OP_BIT_SHR = 0x5E,               // A <- B >> C
    OP_SYS_EXEC = 0x5F,              // A <- system(B:cmd_id)
    OP_FS_ESCRIBIR_BYTE = 0x60,      // fputc(B:byte, vm->current_file)
    OP_MEM_MAPA_CREAR = 0x61,        // A <- NewMapConcept()
    OP_MEM_MAPA_PONER = 0x62,        // SetMap(A:map_id, B:key_id, C:val_reg)
    OP_MEM_MAPA_OBTENER = 0x63,      // A <- GetMap(B:map_id, C:key_id)
    OP_MEM_MAPA_TAMANO = 0x7E,       // A <- count entries (B: map_id reg)
    OP_FS_LEER_BYTE = 0x64,          // A <- fgetc(handle B)
    OP_FS_ESCRIBIR_U32 = 0x65,       // fwrite(u32 B, f handle A)
    OP_FS_LEER_ARCHIVO_REG = 0x66,   // A: path_reg, B: dest_id_reg
    OP_FS_ESCRIBIR_ARCHIVO_REG = 0x67, // A: path_reg, B: origin_id_reg
    OP_FS_LEER_U32 = 0x68,           // A <- fread_u32(handle B)
    OP_SYS_ARGC = 0x69,              // A <- system_argc
    OP_SYS_ARGV = 0x6A,              // A <- system_argv[B] (string ID)
    OP_STR_SUBTEXTO = 0x6B,          // A <- Substring(B:str, C:start, R(B+1):len)
    OP_IO_PERCIBIR_TECLADO = 0x6C,   // A <- Leer stdin sin bloquear ("" si no hay; 7.2)
    OP_IO_ENTRADA_FLOTANTE = 0x8E,   // A <- float32: teclas 0-9, un '.', '-' solo al inicio, Retroceso, Enter termina
    OP_MEM_LISTA_LIBERAR = 0x8D,     // Liberar lista por id en reg A (JMN: slot al pool; sin colisión 0xB9=rastro)

    OP_STR_EXTRAER_ANTES = 0xD0,     // Extraer texto antes de patrón
    OP_STR_EXTRAER_DESPUES = 0xD1,   // Extraer texto después de patrón
    OP_MARCAR_ESTADO = 0xC0,         // Marcar estado interno para auditoría
    OP_OBSERVAR = 0xC1,              // Observar valor de registro/memoria
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
    OP_MEM_DECAE_CONEXIONES = 0xCC,   // Decaimiento global; A=reg ok; B,C inm opcional factor%/1000‰ umbral
    OP_MEM_PROPAGAR_ACTIVACION = 0xCD, // A <- mejor id por propagación; B=origen, C=tipo|(K<<8)|(prof<<16)
    OP_MEM_RESOLVER_CONFLICTOS = 0xCE, // A <- id_ganador; B=origen, C=tipo; cuando 2+ candidatos con peso similar
    OP_STR_EXTRAER_ANTES_REG = 0xD8,   // Extraer (regs: A=frase, B=patron) -> C=dest_reg
    OP_STR_EXTRAER_DESPUES_REG = 0xD9, // Extraer (regs: A=frase, B=patron) -> C=dest_reg
    OP_STR_CONCATENAR = 0xD2,        // Concatenar A y B en destino
    OP_STR_CONCATENAR_REG = 0xD7,    // Concatenar regs B y C en destino A

    OP_IMPRIMIR_NUMERO = 0xD3,       // Imprime valor numérico de un registro
    OP_MEM_ULTIMA_PALABRA = 0xD4,   // Extrae última palabra (concept ID -> dest_var_addr)
    OP_MEM_TERMINA_CON = 0xD5,      // Verifica sufijo (id_frase, id_sufijo) -> reg
    OP_MEM_ULTIMA_SILABA = 0xD6,    // Extrae última sílaba (concept ID -> dest_var_addr)
    
    OP_MEM_ASOCIAR = 0xE8,           // Crear asociación entre dos conceptos
    OP_MEM_ECO = 0xFD,               // Eco de concepto (imitación)
    OP_MEM_PENALIZAR = 0xE3,         // Penalizar peso de asociación
    OP_STR_REGISTRAR_LITERAL = 0xE4, // Registrar string literal desde data
    OP_MEM_APRENDER_PESO_REG = 0xE7, // Aprender peso dinámico
    OP_MEM_APRENDER_PESO = 0xE7,     // Alias compatible
    OP_MEM_ES_VARIABLE_SISTEMA = 0xEB, // A ← EsVariableSistema(B)
    OP_MEM_CREAR = 0xBC,             // Crear memoria neuronal
    OP_MEM_CERRAR = 0xBB,            // Cerrar memoria neuronal
    OP_STR_A_ENTERO = 0xBD,          // A <- atoi(B:frase_id)
    OP_STR_A_FLOTANTE = 0xBE,        // A <- atof(B:frase_id)
    
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
    OP_STR_LONGITUD = 0xBA,          // A <- Longitud de string
    
    // Nuevos opcodes Fase 0
    OP_STR_MINUSCULAS = 0x50,        // A <- Convertir a minúsculas
    OP_STR_COPIAR = 0x51,            // Copiar string
    OP_FS_ABRIR = 0x52,              // A <- Abrir archivo
    OP_FS_ESCRIBIR = 0x53,            // Escribir en archivo actual
    OP_FS_FIN_ARCHIVO = 0x54,        // A <- EOF check
    OP_FS_CERRAR = 0x55,             // Cerrar archivo actual
    OP_FS_EXISTE = 0x56,             // A <- Existe archivo
    OP_SYS_TIMESTAMP = 0x57,         // A <- Timestamp actual
    OP_FS_LEER_LINEA = 0x58,          // A <- Leer línea de archivo actual
    OP_STR_ASOCIAR_PESOS = 0x59,      // A <- Asociar conceptos densamente
    
    OP_IMPRIMIR_FLOTANTE = 0xEF,     // Imprime float en Reg A
    OP_MEM_OBTENER_FUERZA = 0xED,     // A <- GetForce(B, C)
    OP_FS_LISTAR = 0x2A,             // A <- Listar archivos
    OP_FS_BORRAR = 0x2B,             // Borrar archivo
    OP_FS_COPIAR = 0x2C,             // Copiar archivo
    OP_FS_MOVER  = 0x2D,             // Mover/Renombrar archivo
    OP_FS_TAMANO = 0x2E,             // A <- Tamaño archivo
    OP_CARGAR_BIBLIOTECA = 0x2F,     // A <- handle; ruta en data (offset B|C<<8)
    OP_FFI_OBTENER_SIMBOLO = 0x93,   // A <- GetProcAddress(handle en B, nombre en data offset reg C)
    OP_FFI_LLAMAR = 0x94,            // A <- llamar fn en reg B; args en regs 3,4,5,6; C = num_args (0-4)
    OP_STR_EXTRAER_CARACTER = 0xEC,  // A <- Extraer carácter
    OP_STR_CODIGO_CARACTER = 0xEE,   // A <- Código ASCII del primer carácter de B (0 si vacío)
    OP_STR_DESDE_CODIGO = 0x9A,      // A <- String de 1 carácter con código ASCII B
    OP_MEM_OBTENER_TODOS = 0xFE,      // A <- Obtener lista de todos los IDs
    OP_NOP = 0xFF,            // No operation
    OP_TRY_ENTER = 0x86,     // Apila desplazamiento u24 (A|B|C) del manejador atrapar/final (offset en seccion codigo)
    OP_TRY_LEAVE = 0x87      // Saca un nivel de intentar (exito del bloque try)
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

typedef struct {
    uint8_t version;       // Versión del bloque JASB-SEC
    uint8_t mode;          // 0=off, 1=warn, 2=strict
    uint16_t max_stack;    // Límite de stack en llamadas
    uint32_t max_jump;     // Límite de salto relativo (bytes), 0 = sin límite
} IRJasbSecPolicy;

// Funciones
IRFile* ir_file_create(void);
void ir_file_destroy(IRFile* ir);
int ir_file_write_header(IRFile* ir, FILE* f);
int ir_file_read_header(IRFile* ir, FILE* f);
int ir_file_add_instruction(IRFile* ir, IRInstruction* inst);
int ir_file_set_ia_metadata(IRFile* ir, const uint8_t* data, size_t size);
int ir_file_add_data(IRFile* ir, const uint8_t* data, size_t size, size_t* out_offset);
int ir_file_add_u64(IRFile* ir, uint64_t value, size_t* out_offset);
int ir_file_add_string(IRFile* ir, const char* text, size_t* out_offset);
int ir_file_write(IRFile* ir, const char* filename);
int ir_file_read(IRFile* ir, const char* filename);
/* Misma disposicion que archivo .jbo en disco (cabecera + IA opcional + codigo + datos). */
int ir_file_read_memory(IRFile* ir, const uint8_t* buf, size_t len);
int ir_file_serialize(IRFile* ir, uint8_t** out_buf, size_t* out_len);
int ir_build_ia_metadata(uint8_t** out, size_t* out_size,
                         const char* profile,
                         const char* build_id,
                         const IRJasbSecPolicy* policy);

#endif // IR_FORMAT_H
