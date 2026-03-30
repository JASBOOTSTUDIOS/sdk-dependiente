#ifndef VM_H
#define VM_H

// #define VM_TRACE_EXECUTION 0

// Fast fetch macro for operand values to avoid function calls
#define GET_OPERAND(vm_ptr, inst_ptr, flag, op_val) \
    (((inst_ptr)->flags & (flag)) ? (uint64_t)(op_val) : (vm_ptr)->registers[op_val])
#define VM_MAX_RECURSION 10000
#define VM_TRY_STACK_MAX 64
#include "ir_format.h"
#include <stdint.h>
#include <stddef.h>

typedef struct VMTextCacheEntry {
    uint32_t id;
    char* text; /* heap, NUL-terminated */
    size_t text_len; /* strlen(text); evita O(n) repetidos en VM */
    struct VMTextCacheEntry* next;
} VMTextCacheEntry;

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
    void* mem_colecciones;                  // Memoria RAM para colecciones de programa (lista, mapa)
    VMTextCacheEntry** text_cache_buckets;  // Tabla Hash: Array de punteros a entradas
    size_t text_cache_size;                 // Número de buckets
    size_t text_cache_count;                // Número de elementos ingresados
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
    int try_depth;
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
