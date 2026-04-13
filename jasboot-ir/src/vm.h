#ifndef VM_H
#define VM_H

// #define VM_TRACE_EXECUTION 0

// Fast fetch macro for operand values to avoid function calls
#define GET_OPERAND(vm_ptr, inst_ptr, flag, op_val) \
    (((inst_ptr)->flags & (flag)) ? (uint64_t)(op_val) : (vm_ptr)->registers[op_val])
#define VM_MAX_RECURSION 10000
#define VM_TRY_STACK_MAX 64
#define VM_TEXT_PREVIEW_MAX 128
#include "ir_format.h"
#include <stdint.h>
#include <stddef.h>

typedef enum VMTextKind {
    VM_TEXT_RAW = 0,
    VM_TEXT_CONCAT = 1
} VMTextKind;

typedef struct VMTextCacheEntry {
    uint32_t id;
    char* text; /* heap, NUL-terminated */
    size_t text_len; /* strlen(text); evita O(n) repetidos en VM */
    char* preview; /* prefijo corto para subtexto/operaciones frecuentes */
    uint16_t preview_len;
    uint16_t preview_id_len; /* Longitud asociada a preview_id para fast path de subtexto */
    uint32_t preview_id;
    uint32_t left_id; /* Para representacion perezosa de concatenaciones grandes */
    uint32_t right_id;
    uint8_t kind;
    struct VMTextCacheEntry* next;
} VMTextCacheEntry;

typedef struct VMListSizeCacheEntry {
    uint32_t id;
    uint32_t size;
    struct VMListSizeCacheEntry* next;
} VMListSizeCacheEntry;

typedef struct VMSubstringCacheEntry {
    uint32_t text_id;
    uint32_t start;
    uint32_t len;
    uint32_t result_id;
    struct VMSubstringCacheEntry* next;
} VMSubstringCacheEntry;

typedef enum VMJsonKind {
    VM_JSON_NULL = 0,
    VM_JSON_BOOL = 1,
    VM_JSON_INT = 2,
    VM_JSON_FLOAT = 3,
    VM_JSON_STRING = 4,
    VM_JSON_ARRAY = 5,
    VM_JSON_OBJECT = 6
} VMJsonKind;

typedef struct VMJsonValue {
    uint8_t kind;
    uint8_t bool_value;
    int64_t int_value;
    double float_value;
    uint32_t text_id;
    uint32_t *items;
    uint32_t *keys;
    uint32_t count;
    uint32_t cap;
} VMJsonValue;

typedef struct VMClosureEntry {
    size_t target_pc;
    uint32_t env_list_id;
} VMClosureEntry;

typedef struct VMBytesEntry {
    uint8_t* data;
    uint32_t len;
    uint32_t cap;
} VMBytesEntry;

typedef struct VMSocketEntry {
    intptr_t native_handle;
    uint8_t is_listener;
    uint8_t is_open;
} VMSocketEntry;

typedef struct VMTlsEntry {
    uint32_t socket_id;
    uint8_t mode;
    uint8_t is_open;
    uint8_t handshake_ok;
    uint8_t owns_ctx;
    void* ctx;
    void* ssl;
} VMTlsEntry;

typedef struct VMHttpServerEntry {
    uint32_t socket_id;
    uint8_t is_tls;
    uint8_t is_open;
} VMHttpServerEntry;

/** Entrada en la ventana circular de percepción (Fase C IA). */
typedef struct VMPercepcionEntrada {
    uint32_t id;
    uint64_t ts_ms;
} VMPercepcionEntrada;

/** Entrada en el rastro de activación (Fase D IA): orden FIFO de primer descubrimiento. */
typedef struct VMRastroActivacionEntrada {
    uint32_t id;
    float activacion;
} VMRastroActivacionEntrada;

// Estado de la VM
typedef struct {
    uint64_t registers[IR_REGISTER_COUNT];  // Registros virtuales
    uint8_t* memory;                         // Memoria de datos
    size_t memory_size;                      // Tamaño de memoria
    size_t stack[VM_MAX_RECURSION];          // Stack (direcciones de retorno)
    size_t stack_ptr;                        // Stack pointer
    size_t pc;                              // Program counter (offset en bytes)
    uint32_t fp;                            // Frame Pointer (offset en memoria de datos)
    uint32_t sp;                            // Stack Pointer (offset en memoria de datos)
    uint32_t fp_stack[VM_MAX_RECURSION];    // Stack de Frame Pointers para recursividad
    uint32_t fp_stack_ptr;                  // Pointer del stack de FPs
    IRFile* ir;                             // Archivo IR cargado
    int running;                            // Si la VM está ejecutando
    int exit_code;                          // Código de salida
    void* mem_neuronal;                     // Memoria neuronal (opcional, puede ser cerebro.jmn)
    uint32_t mem_neuronal_owner_depth;      // Profundidad de llamada donde se abrio la memoria persistente
    int mem_neuronal_open_line;             // Linea donde se abrio la memoria persistente
    void* mem_colecciones;                  // Memoria RAM para colecciones de programa (lista, mapa)
    VMTextCacheEntry** text_cache_buckets;  // Tabla Hash: Array de punteros a entradas
    size_t text_cache_size;                 // Número de buckets
    size_t text_cache_count;                // Número de elementos ingresados
    uint32_t next_runtime_text_id;         // IDs unicos para textos construidos en runtime
    VMListSizeCacheEntry** list_size_buckets; // Cache de tamaños de lista para evitar consultas JMN repetidas
    size_t list_size_cache_size;
    VMSubstringCacheEntry** substring_cache_buckets; // Memoización de extraer_subtexto(id,start,len)
    size_t substring_cache_size;
    VMJsonValue* json_values;                 // Heap simple de valores JSON (id = índice + 1)
    uint32_t json_count;
    uint32_t json_cap;
    VMClosureEntry* closures;
    uint32_t closure_count;
    uint32_t closure_cap;
    uint32_t current_closure_env;
    uint32_t closure_env_stack[VM_MAX_RECURSION];
    VMBytesEntry* bytes_values;
    uint32_t bytes_count;
    uint32_t bytes_cap;
    VMSocketEntry* sockets;
    uint32_t socket_count;
    uint32_t socket_cap;
    VMTlsEntry* tls_entries;
    uint32_t tls_count;
    uint32_t tls_cap;
    VMHttpServerEntry* http_servers;
    uint32_t http_server_count;
    uint32_t http_server_cap;
    void* tls_server_ctx_cached;
    char* tls_server_cert_cached;
    char* tls_server_key_cached;
    int net_initialized;
    char* context;                          // Contexto actual (para hashing)
    char* self_path;                        // Ruta al ejecutable jasboot
    FILE* current_file;                     // Archivo actual abierto (para sistema de archivos)
    int argc;                               // Número de argumentos de sistema
    char** argv;                            // Argumentos de sistema
    char* ir_path;                          // Ruta del archivo IR cargado (para Hot-Reload)
    int modo_continuo;                      // 1: EOF stdin no detiene VM; retorna "" (7.1)
    /* FFI: handles de bibliotecas cargadas (para liberar en destroy) */
    void** ffi_handles;
    size_t ffi_handles_cap;
    size_t ffi_handles_count;
    /* Heap: punteros reservados (para liberar en destroy si el programa no lo hace) */
    void** heap_ptrs;
    size_t heap_cap;
    size_t heap_count;
    /* Ventana de percepción temporal (FIFO circular, configurable) */
    VMPercepcionEntrada* percepcion_buf;
    uint32_t percepcion_cap;
    uint32_t percepcion_head;
    uint32_t percepcion_count;
    /* Rastro de activación (introspección BFS / pensar_respuesta / asociados) */
    VMRastroActivacionEntrada* rastro_buf;
    uint32_t rastro_cap;
    uint32_t rastro_head;
    uint32_t rastro_count;
    /* Fase E: desempate opcional en elegir_por_peso (0 = id de candidato + índice) */
    uint32_t elegir_por_peso_seed;
    int current_line;                       // Línea actual en ejecución
    /* intentar/atrapar: manejador = offset dentro de la seccion de codigo (.jbo) */
    uint32_t try_code_off[VM_TRY_STACK_MAX];
    uint32_t try_fp[VM_TRY_STACK_MAX];
    uint32_t try_sp[VM_TRY_STACK_MAX];
    size_t try_stack_ptr[VM_TRY_STACK_MAX];
    uint32_t try_fp_stack_ptr[VM_TRY_STACK_MAX];
    uint32_t try_closure_env[VM_TRY_STACK_MAX];
    int try_depth;
    int ir_validated;                      // 1 si el IR ya pasó validación estructural
    uint64_t opcode_hits[256];             // Perfil simple por opcode para guiar futuras rutas AOT/JIT
    uint64_t* pc_hits;                     // Perfil simple por instrucción
    size_t pc_hits_len;                    // Longitud de pc_hits en número de instrucciones
    int profile_enabled;                   // Activado por entorno para imprimir resumen al final
} VM;

// Crear y destruir VM
VM* vm_create(void);
void vm_destroy(VM* vm);

// Cargar IR en VM
int vm_load(VM* vm, IRFile* ir);
int vm_load_file(VM* vm, const char* filename);

// Ejecutar VM
int vm_run(VM* vm);
int vm_run_with_limit(VM* vm, uint64_t max_steps);
int vm_step(VM* vm);  // Ejecutar una instrucción

// Obtener estado
uint64_t vm_get_register(VM* vm, int reg);
void vm_set_register(VM* vm, int reg, uint64_t value);
size_t vm_get_pc(VM* vm);
int vm_is_running(VM* vm);
int vm_get_exit_code(VM* vm);
void vm_set_modo_continuo(VM* vm, int activo);

#endif // VM_H
