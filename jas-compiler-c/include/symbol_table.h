/* Tabla de símbolos - Nivel 3: Scopes, variables, structs */

#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stddef.h>
#include <stdint.h>

#define SCOPE_DEPTH_MAX 64
#define SYM_ENTRY_NAME_MAX 128

/* Resultado de lookup/declare: addr + is_relative (1=local, 0=global) */
typedef struct {
    uint32_t addr;
    int is_relative;
    int found;   /* 1 si lookup encontró, 0 si no */
    int is_const; /* 1 si es constante (no asignable) */
    void *macro_ast;
    const char *lista_elem_type; /* NULL o puntero al T de lista<T> en la entrada (vida = símbolo) */
} SymResult;

/* Entrada en un scope (3.5: nombre, tipo, addr; 3.4: is_param) */
typedef struct SymbolEntry {
    char name[SYM_ENTRY_NAME_MAX];
    uint32_t addr;
    int is_relative;
    char *type_name;
    char *lista_elem_type; /* si type_name es lista y hubo lista<T> */
    int is_param;
    int is_const;
    void *macro_ast; /* ASTNode* si es un macro (lambda) */
    int used;        /* 1 si la variable ha sido usada o referenciada */
    int is_exported; /* 1 si la variable ha sido marcada con enviar */
    struct SymbolEntry *next;
} SymbolEntry;

/* Registro de campo de struct (3.7) */
typedef struct StructFieldInfo {
    char *name;
    char *type_name;
    size_t offset;
    size_t size;
    int is_private;
} StructFieldInfo;

/* Registro de método de clase */
typedef struct StructMethodInfo {
    char *name;
    void *method_ast; /* FunctionNode* */
    int is_private;
} StructMethodInfo;

/* Definición de struct registrada (3.7) */
typedef struct StructInfo {
    char *name;
    char *base_name; /* NULL si no tiene base */
    StructFieldInfo *fields;
    size_t n_fields;
    size_t total_size;
    StructMethodInfo *methods;
    size_t n_methods;
    int is_exported;
} StructInfo;

/* Tabla de símbolos */
typedef struct SymbolTable {
    SymbolEntry *scopes[SCOPE_DEPTH_MAX];
    size_t scope_depth;
    uint32_t next_global_offset;   /* 3.2: desde 0x0800 */
    uint32_t next_local_offset;   /* 3.3: por función */
    int is_global;
    StructInfo *structs;          /* 3.7: registro de structs */
    size_t n_structs;
    size_t structs_cap;
} SymbolTable;

/* 3.1 Scopes anidados */
void sym_enter_scope(SymbolTable *st, int is_function);
int sym_exit_scope(SymbolTable *st);

/* 3.2/3.3 Variables y parámetros (3.4 is_param en declare) */
SymResult sym_declare(SymbolTable *st, const char *name, const char *type_name, size_t size, int is_param, int is_const, const char *lista_elem_type);
SymResult sym_declare_macro(SymbolTable *st, const char *name, void *macro_ast);
SymResult sym_reserve_temp(SymbolTable *st, size_t size);
int sym_is_parameter(SymbolTable *st, const char *name);

/* 3.5 lookup / declare */
SymResult sym_lookup(SymbolTable *st, const char *name);
const char *sym_lookup_type(SymbolTable *st, const char *name);
const char *sym_lookup_lista_elem(SymbolTable *st, const char *name);
const char *sym_lookup_tarea_elem(SymbolTable *st, const char *name);

/* 3.6 get_or_create */
SymResult sym_get_or_create(SymbolTable *st, const char *name, const char *type_name);

/* 3.7 Estructuras */
void sym_register_struct(SymbolTable *st, const char *name, const char **field_types, const char **field_names, size_t n_fields);
/* Versión extendida para clases con métodos y visibilidad */
void sym_register_class(SymbolTable *st, const char *name, const char **field_types, const char **field_names, const int *field_vis, size_t n_fields,
                        void **method_asts, const char **method_names, const int *method_vis, size_t n_methods, int is_exported);
/* Herencia de datos: campos de base_name primero, luego los propios. base_name debe estar ya registrado.
 * Devuelve 0 si ok; -1 base inexistente; -2 campo duplicado con la base. */
int sym_register_struct_extends(SymbolTable *st, const char *name, const char *base_name,
                                const char **field_types, const char **field_names, size_t n_fields);
/* Versión extendida para herencia de clases */
int sym_register_class_extends(SymbolTable *st, const char *name, const char *base_name,
                               const char **field_types, const char **field_names, const int *field_vis, size_t n_fields,
                               void **method_asts, const char **method_names, const int *method_vis, size_t n_methods, int is_exported);
int sym_get_struct_field(SymbolTable *st, const char *struct_name, const char *field_name, size_t *out_offset, const char **out_type, size_t *out_size);
int sym_get_struct_field_visibility(SymbolTable *st, const char *struct_name, const char *field_name, int *out_is_private);
int sym_get_struct_method(SymbolTable *st, const char *struct_name, const char *method_name, void **out_method_ast);
int sym_get_struct_method_visibility(SymbolTable *st, const char *struct_name, const char *method_name, int *out_is_private);
const char *sym_get_struct_lista_elem_type(SymbolTable *st, const char *struct_name, const char *field_name);
size_t sym_get_struct_size(SymbolTable *st, const char *struct_name);
size_t sym_struct_n_fields(SymbolTable *st, const char *struct_name);
int sym_struct_field_by_index(SymbolTable *st, const char *struct_name, size_t idx,
                              const char **out_name, const char **out_type, size_t *out_offset, size_t *out_size);

void sym_init(SymbolTable *st);
void sym_free(SymbolTable *st);

void sym_set_exported(SymbolTable *st, const char *name);
int sym_is_exported(SymbolTable *st, const char *name);

StructInfo *sym_get_struct_info(SymbolTable *st, const char *name);
#endif /* SYMBOL_TABLE_H */
