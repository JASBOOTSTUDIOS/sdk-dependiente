/* Implementación tabla de símbolos - Nivel 3 */

#include "symbol_table.h"
#include "keywords.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#define DEFAULT_SIZE 8

static char *strdup_safe(const char *s) {
    return s ? strdup(s) : NULL;
}

static SymbolEntry *find_in_scope(SymbolEntry *head, const char *name) {
    for (SymbolEntry *e = head; e; e = e->next)
        if (strcmp(e->name, name) == 0)
            return e;
    return NULL;
}

void sym_init(SymbolTable *st) {
    memset(st, 0, sizeof(*st));
    st->next_global_offset = 0;  /* Cambiado de 0x0800 a 0 para que 'resultado' este en 0 */
    st->is_global = 1;
    st->scope_depth = 1;
    st->scopes[0] = NULL;
}

void sym_init_global(SymbolTable *st) {
    sym_init(st);
    /* Simbolo magico para resultados de operaciones (JMN, etc.) 
       Ahora quedara en la direccion 0x100000 global (1MB) para evitar conflictos con strings en programas masivos. */
    st->next_global_offset = 0x100000;
    sym_declare(st, "resultado", "elemento", 8, 0, 0, NULL, SYMDECL_FLAGS_ALLOW_RESERVED_NAME);
    
    /* Reajustar el offset para el resto de globales a 0x100008 para mantener consistencia */
    st->next_global_offset = 0x100008;
}

void sym_free(SymbolTable *st) {
    for (size_t i = 0; i < st->scope_depth; i++) {
        SymbolEntry *e = st->scopes[i];
        while (e) {
            SymbolEntry *next = e->next;
            free(e->type_name);
            free(e->lista_elem_type);
            free(e);
            e = next;
        }
        st->scopes[i] = NULL;
    }
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        free(si->name);
        for (size_t j = 0; j < si->n_fields; j++) {
            free(si->fields[j].name);
            free(si->fields[j].type_name);
        }
        free(si->fields);
        for (size_t j = 0; j < si->n_methods; j++) {
            free(si->methods[j].name);
            /* method_ast no se libera aqui, es parte del AST */
        }
        if (si->methods) free(si->methods);
        for (size_t j = 0; j < si->n_bases; j++) free(si->base_names[j]);
        if (si->base_names) free(si->base_names);
    }
    free(st->structs);
    st->structs = NULL;
    st->n_structs = 0;
}

/* 3.1 */
void sym_enter_scope(SymbolTable *st, int is_function) {
    if (st->scope_depth >= SCOPE_DEPTH_MAX) return;
    st->scopes[st->scope_depth] = NULL;
    st->scope_depth++;
    st->is_global = 0;
    if (is_function)
        st->next_local_offset = 0;  /* 3.3 */
}

int sym_exit_scope(SymbolTable *st) {
    if (st->scope_depth <= 1) return 0;
    SymbolEntry *e = st->scopes[st->scope_depth - 1];
    while (e) {
        SymbolEntry *next = e->next;
        free(e->type_name);
        free(e->lista_elem_type);
        free(e);
        e = next;
    }
    st->scopes[st->scope_depth - 1] = NULL;
    st->scope_depth--;
    if (st->scope_depth == 1)
        st->is_global = 1;
    return 0;
}

/* 3.2/3.3/3.4 */
SymResult sym_declare(SymbolTable *st, const char *name, const char *type_name, size_t size, int is_param, int is_const, const char *lista_elem_type, int sym_flags) {
    SymResult r = {0, 0, 0, 0, NULL, NULL};
    if (!name || strlen(name) >= SYM_ENTRY_NAME_MAX) return r;

    if (!(sym_flags & SYMDECL_FLAGS_ALLOW_RESERVED_NAME) && is_reserved_identifier(name))
        return r;

    size_t depth = st->scope_depth - 1;
    if (find_in_scope(st->scopes[depth], name))
        return r;  /* Ya declarada en este scope */

    SymbolEntry *e = calloc(1, sizeof(SymbolEntry));
    if (!e) return r;
    strncpy(e->name, name, SYM_ENTRY_NAME_MAX - 1);
    e->name[SYM_ENTRY_NAME_MAX - 1] = '\0';
    e->type_name = strdup_safe(type_name);
    e->lista_elem_type = (lista_elem_type && lista_elem_type[0]) ? strdup(lista_elem_type) : NULL;
    e->is_param = is_param ? 1 : 0;
    e->is_const = is_const ? 1 : 0;
    e->macro_ast = NULL;
    e->used = 0;

    if (size == 0) size = DEFAULT_SIZE;

    if (st->is_global) {
        e->addr = st->next_global_offset;
        st->next_global_offset += (uint32_t)size;
        e->is_relative = 0;
    } else {
        e->addr = st->next_local_offset;
        st->next_local_offset += (uint32_t)size;
        e->is_relative = 1;
    }

    r.addr = e->addr;
    r.is_relative = e->is_relative;
    r.found = 1;
    r.is_const = e->is_const;
    r.macro_ast = e->macro_ast;
    r.lista_elem_type = e->lista_elem_type;

    e->next = st->scopes[depth];
    st->scopes[depth] = e;
    return r;
}

SymResult sym_declare_macro(SymbolTable *st, const char *name, void *macro_ast) {
    SymResult empty = {0, 0, 0, 0, NULL, NULL};
    if (!name || strlen(name) >= SYM_ENTRY_NAME_MAX)
        return empty;
    if (is_reserved_identifier(name))
        return empty;
    SymResult r = sym_declare(st, name, "macro", 0, 0, 1, NULL, SYMDECL_FLAGS_NONE);
    if (r.found) {
        SymbolEntry *e = find_in_scope(st->scopes[st->scope_depth - 1], name);
        if (e) {
            e->macro_ast = macro_ast;
            r.macro_ast = macro_ast;
        }
    }
    return r;
}

SymResult sym_reserve_temp(SymbolTable *st, size_t size) {
    SymResult r = {0, 1, 1, 0, NULL, NULL};
    if (size == 0) size = DEFAULT_SIZE;
    r.addr = st->next_local_offset;
    st->next_local_offset += (uint32_t)size;
    return r;
}

int sym_is_parameter(SymbolTable *st, const char *name) {
    for (size_t i = st->scope_depth; i > 0; i--) {
        SymbolEntry *e = find_in_scope(st->scopes[i - 1], name);
        if (e) return e->is_param;
    }
    return 0;
}

/* 3.5 */
SymResult sym_lookup(SymbolTable *st, const char *name) {
    SymResult r = {0, 0, 0, 0, NULL, NULL};
    if (!name) return r;
    for (size_t i = st->scope_depth; i > 0; i--) {
        SymbolEntry *e = find_in_scope(st->scopes[i - 1], name);
        if (e) {
            e->used = 1; /* Mark as used */
            r.addr = e->addr;
            r.is_relative = e->is_relative;
            r.found = 1;
            r.is_const = e->is_const;
            r.macro_ast = e->macro_ast;
            r.lista_elem_type = e->lista_elem_type;
            return r;
        }
    }
    return r;
}

const char *sym_lookup_type(SymbolTable *st, const char *name) {
    if (!name) return NULL;
    for (size_t i = st->scope_depth; i > 0; i--) {
        SymbolEntry *e = find_in_scope(st->scopes[i - 1], name);
        if (e) return e->type_name;
    }
    return NULL;
}

const char *sym_lookup_lista_elem(SymbolTable *st, const char *name) {
    if (!name) return NULL;
    for (size_t i = st->scope_depth; i > 0; i--) {
        SymbolEntry *e = find_in_scope(st->scopes[i - 1], name);
        if (e && e->type_name && strcmp(e->type_name, "lista") == 0 && e->lista_elem_type)
            return e->lista_elem_type;
        if (e && e->type_name && strcmp(e->type_name, "lista?") == 0 && e->lista_elem_type)
            return e->lista_elem_type;
    }
    return NULL;
}

const char *sym_lookup_collection_elem_type(SymbolTable *st, const char *name) {
    if (!name) return NULL;
    for (size_t i = st->scope_depth; i > 0; i--) {
        SymbolEntry *e = find_in_scope(st->scopes[i - 1], name);
        if (!e || !e->type_name || !e->lista_elem_type)
            continue;
        if (strcmp(e->type_name, "lista") == 0 || strcmp(e->type_name, "lista?") == 0 ||
            strcmp(e->type_name, "mapa") == 0 || strcmp(e->type_name, "mapa?") == 0)
            return e->lista_elem_type;
    }
    return NULL;
}

const char *sym_lookup_tarea_elem(SymbolTable *st, const char *name) {
    if (!name) return NULL;
    for (size_t i = st->scope_depth; i > 0; i--) {
        SymbolEntry *e = find_in_scope(st->scopes[i - 1], name);
        if (e && e->type_name && strcmp(e->type_name, "tarea") == 0 && e->lista_elem_type)
            return e->lista_elem_type;
    }
    return NULL;
}

/* 3.6 */
SymResult sym_get_or_create(SymbolTable *st, const char *name, const char *type_name) {
    SymResult r = sym_lookup(st, name);
    if (r.found)
        return r;
    if (is_reserved_identifier(name))
        return (SymResult){0, 0, 0, 0, NULL, NULL};
    return sym_declare(st, name, type_name, DEFAULT_SIZE, 0, 0, NULL, SYMDECL_FLAGS_NONE);
}

/* 3.7 */
static size_t get_field_size(SymbolTable *st, const char *type_name) {
    size_t s = sym_get_struct_size(st, type_name);
    if (s > 0) return s;
    if (strcmp(type_name, "u32") == 0 || strcmp(type_name, "u8") == 0 || strcmp(type_name, "byte") == 0) return 8;
    if (strcmp(type_name, "texto") == 0) return 8;  /* ptr */
    if (strcmp(type_name, "flotante") == 0) return 8;
    if (strcmp(type_name, "vec2") == 0) return 16;   /* 2 x 8 bytes */
    if (strcmp(type_name, "vec3") == 0) return 24;   /* 3 x 8 */
    if (strcmp(type_name, "vec4") == 0) return 32;   /* 4 x 8 */
    if (strcmp(type_name, "mat4") == 0) return 128;  /* 16 x 8 bytes, row-major */
    if (strcmp(type_name, "mat3") == 0) return 72;   /* 9 x 8 bytes, row-major */
    if (strcmp(type_name, "lista") == 0 || strcmp(type_name, "mapa") == 0 || strcmp(type_name, "tarea") == 0)
        return 8;
    if (strcmp(type_name, "funcion") == 0) return 8;  /* puntero de codigo (desplazamiento IR) */
    if (strcmp(type_name, "bytes") == 0 || strcmp(type_name, "socket") == 0 || strcmp(type_name, "tls") == 0 ||
        strcmp(type_name, "http_solicitud") == 0 || strcmp(type_name, "http_respuesta") == 0 || strcmp(type_name, "http_servidor") == 0)
        return 8;
    return 8;  /* entero, u64, bool, etc. */
}

void sym_register_struct(SymbolTable *st, const char *name, const char **field_types, const char **field_names, size_t n_fields) {
    sym_register_class(st, name, field_types, field_names, NULL, n_fields, NULL, NULL, NULL, 0, 0, 0);
}

void sym_register_class(SymbolTable *st, const char *name, const char **field_types, const char **field_names, const int *field_vis, size_t n_fields,
                        void **method_asts, const char **method_names, const int *method_vis, size_t n_methods, int is_exported, int is_class) {
    if (!name || (n_fields > 0 && (!field_types || !field_names))) return;
    if (st->n_structs >= st->structs_cap) {
        size_t new_cap = st->structs_cap ? st->structs_cap * 2 : 8;
        StructInfo *p = realloc(st->structs, new_cap * sizeof(StructInfo));
        if (!p) return;
        st->structs = p;
        st->structs_cap = new_cap;
    }
    StructInfo *si = &st->structs[st->n_structs];
    memset(si, 0, sizeof(StructInfo));
    si->name = strdup(name);
    si->fields = n_fields ? calloc(n_fields, sizeof(StructFieldInfo)) : NULL;
    si->n_fields = n_fields;
    si->is_exported = is_exported;
    si->is_class = is_class;
    
    size_t offset = is_class ? 8 : 0;
    for (size_t i = 0; i < n_fields; i++) {
        si->fields[i].name = strdup_safe(field_names[i]);
        si->fields[i].type_name = strdup_safe(field_types[i]);
        si->fields[i].size = get_field_size(st, si->fields[i].type_name);
        si->fields[i].offset = offset;
        si->fields[i].is_private = field_vis ? field_vis[i] : 0;
        offset += si->fields[i].size;
    }
    si->total_size = offset;
    
    si->methods = n_methods ? calloc(n_methods, sizeof(StructMethodInfo)) : NULL;
    si->n_methods = n_methods;
    for (size_t i = 0; i < n_methods; i++) {
        si->methods[i].name = strdup_safe(method_names[i]);
        si->methods[i].method_ast = method_asts[i];
        si->methods[i].is_private = method_vis ? method_vis[i] : 0;
    }
    st->n_structs++;
}

int sym_register_struct_extends(SymbolTable *st, const char *name, const char **base_names, size_t n_bases,
                                const char **field_types, const char **field_names, size_t n_fields) {
    return sym_register_class_extends(st, name, base_names, n_bases, field_types, field_names, NULL, n_fields, NULL, NULL, NULL, 0, 0, 0);
}

int sym_register_class_extends(SymbolTable *st, const char *name, const char **base_names, size_t n_bases,
                               const char **field_types, const char **field_names, const int *field_vis, size_t n_fields,
                               void **method_asts, const char **method_names, const int *method_vis, size_t n_methods, int is_exported, int is_class) {
    
    size_t total_f_bases = 0;
    size_t total_m_bases = 0;
    StructInfo **base_sis = malloc(n_bases * sizeof(StructInfo *));
    
    for (size_t b = 0; b < n_bases; b++) {
        StructInfo *base_si = NULL;
        for (size_t i = 0; i < st->n_structs; i++) {
            if (strcmp(st->structs[i].name, base_names[b]) == 0) {
                base_si = &st->structs[i];
                break;
            }
        }
        if (!base_si) {
            free(base_sis);
            return -1;
        }
        base_sis[b] = base_si;
        total_f_bases += base_si->n_fields;
        total_m_bases += base_si->n_methods;
    }

    /* 1. Campos: heredar de todas las bases */
    size_t total_f = total_f_bases + n_fields;
    const char **tf = malloc(total_f * sizeof(char*));
    const char **nf = malloc(total_f * sizeof(char*));
    int *vf = malloc(total_f * sizeof(int));
    
    size_t actual_f = 0;
    for (size_t b = 0; b < n_bases; b++) {
        for (size_t i = 0; i < base_sis[b]->n_fields; i++) {
            /* Verificar duplicados en campos de bases anteriores */
            int dup = 0;
            for (size_t k = 0; k < actual_f; k++) {
                if (strcmp(nf[k], base_sis[b]->fields[i].name) == 0) {
                    dup = 1; break;
                }
            }
            if (!dup) {
                tf[actual_f] = base_sis[b]->fields[i].type_name;
                nf[actual_f] = base_sis[b]->fields[i].name;
                vf[actual_f] = base_sis[b]->fields[i].is_private;
                actual_f++;
            }
        }
    }
    
    for (size_t i = 0; i < n_fields; i++) {
        /* Verificar duplicados con bases */
        for (size_t k = 0; k < actual_f; k++) {
            if (strcmp(nf[k], field_names[i]) == 0) {
                /* Error: campo duplicado */
                free(base_sis); free(tf); free(nf); free(vf);
                return -2;
            }
        }
        tf[actual_f] = field_types[i];
        nf[actual_f] = field_names[i];
        vf[actual_f] = field_vis ? field_vis[i] : 0;
        actual_f++;
    }

    /* 2. Metodos: heredar de bases y permitir sobrescritura */
    size_t total_m_alloc = total_m_bases + n_methods;
    void **tm = malloc(total_m_alloc * sizeof(void*));
    const char **nm = malloc(total_m_alloc * sizeof(char*));
    int *vm = malloc(total_m_alloc * sizeof(int));
    size_t actual_m = 0;

    /* Primero los metodos de las bases */
    for (size_t b = 0; b < n_bases; b++) {
        for (size_t i = 0; i < base_sis[b]->n_methods; i++) {
            int overriden = 0;
            for (size_t k = 0; k < actual_m; k++) {
                if (strcmp(nm[k], base_sis[b]->methods[i].name) == 0) {
                    overriden = 1; break;
                }
            }
            if (!overriden) {
                tm[actual_m] = base_sis[b]->methods[i].method_ast;
                nm[actual_m] = base_sis[b]->methods[i].name;
                vm[actual_m] = base_sis[b]->methods[i].is_private;
                actual_m++;
            }
        }
    }

    /* Luego los metodos de la propia clase (pueden sobrescribir) */
    for (size_t i = 0; i < n_methods; i++) {
        int overriden = 0;
        for (size_t j = 0; j < actual_m; j++) {
            if (strcmp(method_names[i], nm[j]) == 0) {
                tm[j] = method_asts[i];
                vm[j] = method_vis ? method_vis[i] : 0;
                overriden = 1;
                break;
            }
        }
        if (!overriden) {
            tm[actual_m] = method_asts[i];
            nm[actual_m] = method_names[i];
            vm[actual_m] = method_vis ? method_vis[i] : 0;
            actual_m++;
        }
    }
    
    sym_register_class(st, name, tf, nf, vf, actual_f, tm, nm, vm, actual_m, is_exported, is_class);
    
    /* Registrar los nombres de las bases */
    if (st->n_structs > 0) {
        StructInfo *new_si = &st->structs[st->n_structs - 1];
        new_si->base_names = malloc(n_bases * sizeof(char *));
        new_si->n_bases = n_bases;
        for (size_t b = 0; b < n_bases; b++) {
            new_si->base_names[b] = strdup(base_names[b]);
        }
    }
    
    free(base_sis);
    free(tf); free(nf); free(vf);
    free(tm); free(nm); free(vm);
    return 0;
}

int sym_get_struct_field(SymbolTable *st, const char *struct_name, const char *field_name, size_t *out_offset, const char **out_type, size_t *out_size) {
    if (!struct_name || !field_name) return 0;
    StructInfo *si = sym_get_struct_info(st, struct_name);
    if (!si) return 0;

    for (size_t j = 0; j < si->n_fields; j++) {
        if (strcmp(si->fields[j].name, field_name) == 0) {
            if (out_offset) {
                *out_offset = si->fields[j].offset;
                /* fprintf(stderr, "DEBUG: field %s.%s offset=%zu\n", struct_name, field_name, *out_offset); */
            }
            if (out_type) *out_type = si->fields[j].type_name;
            if (out_size) *out_size = si->fields[j].size;
            return 1;
        }
    }
    
    /* Buscar en las bases si no se encontro aqui */
    for (size_t b = 0; b < si->n_bases; b++) {
        if (sym_get_struct_field(st, si->base_names[b], field_name, out_offset, out_type, out_size))
            return 1;
    }
    
    return 0;
}

size_t sym_get_struct_size(SymbolTable *st, const char *struct_name) {
    if (!struct_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        if (strcmp(st->structs[i].name, struct_name) == 0)
            return st->structs[i].total_size;
    }
    return 0;
}

int sym_get_struct_field_visibility(SymbolTable *st, const char *struct_name, const char *field_name, int *out_is_private) {
    if (!struct_name || !field_name) return 0;
    StructInfo *si = sym_get_struct_info(st, struct_name);
    if (!si) return 0;

    for (size_t j = 0; j < si->n_fields; j++) {
        if (strcmp(si->fields[j].name, field_name) == 0) {
            if (out_is_private) *out_is_private = si->fields[j].is_private;
            return 1;
        }
    }
    
    /* Buscar en las bases si no se encontro aqui */
    for (size_t b = 0; b < si->n_bases; b++) {
        if (sym_get_struct_field_visibility(st, si->base_names[b], field_name, out_is_private))
            return 1;
    }
    
    return 0;
}

int sym_get_struct_method(SymbolTable *st, const char *struct_name, const char *method_name, void **out_method_ast) {
    if (!struct_name || !method_name) return 0;
    StructInfo *si = sym_get_struct_info(st, struct_name);
    if (!si) return 0;
    
    for (size_t j = 0; j < si->n_methods; j++) {
        if (strcmp(si->methods[j].name, method_name) == 0) {
            if (out_method_ast) *out_method_ast = si->methods[j].method_ast;
            return 1;
        }
    }
    
    /* Buscar en las bases si no se encontro aqui */
    for (size_t b = 0; b < si->n_bases; b++) {
        if (sym_get_struct_method(st, si->base_names[b], method_name, out_method_ast))
            return 1;
    }
    
    return 0;
}

int sym_get_struct_method_visibility(SymbolTable *st, const char *struct_name, const char *method_name, int *out_is_private) {
    if (!struct_name || !method_name) return 0;
    StructInfo *si = sym_get_struct_info(st, struct_name);
    if (!si) return 0;

    for (size_t j = 0; j < si->n_methods; j++) {
        if (strcmp(si->methods[j].name, method_name) == 0) {
            if (out_is_private) *out_is_private = si->methods[j].is_private;
            return 1;
        }
    }
    
    /* Buscar en las bases si no se encontro aqui */
    for (size_t b = 0; b < si->n_bases; b++) {
        if (sym_get_struct_method_visibility(st, si->base_names[b], method_name, out_is_private))
            return 1;
    }
    
    return 0;
}

const char *sym_get_struct_lista_elem_type(SymbolTable *st, const char *struct_name, const char *field_name) {
    if (!struct_name || !field_name) return NULL;
    StructInfo *si = sym_get_struct_info(st, struct_name);
    if (!si) return NULL;

    for (size_t j = 0; j < si->n_fields; j++) {
        if (strcmp(si->fields[j].name, field_name) == 0) {
            const char *tn = si->fields[j].type_name;
            if (!tn) return NULL;
            
            /* Caso 1: T[] */
            size_t len = strlen(tn);
            if (len > 2 && tn[len-2] == '[' && tn[len-1] == ']') {
                static char buf[128];
                size_t n = len - 2;
                if (n >= sizeof(buf)) n = sizeof(buf) - 1;
                memcpy(buf, tn, n);
                buf[n] = '\0';
                return buf;
            }
            
            /* Caso 2: lista<T> */
            if (strncmp(tn, "lista<", 6) == 0) {
                char *p = strchr(tn, '<');
                if (p) {
                    static char buf[128];
                    size_t n = strlen(p + 1);
                    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
                    memcpy(buf, p + 1, n);
                    buf[n] = '\0';
                    char *q = strrchr(buf, '>');
                    if (q) *q = '\0';
                    return buf;
                }
            }
            return NULL;
        }
    }
    
    /* Buscar en las bases si no se encontro aqui */
    for (size_t b = 0; b < si->n_bases; b++) {
        const char *t = sym_get_struct_lista_elem_type(st, si->base_names[b], field_name);
        if (t) return t;
    }
    
    return NULL;
}

size_t sym_struct_n_fields(SymbolTable *st, const char *struct_name) {
    if (!struct_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        if (strcmp(st->structs[i].name, struct_name) == 0)
            return st->structs[i].n_fields;
    }
    return 0;
}

int sym_struct_field_by_index(SymbolTable *st, const char *struct_name, size_t idx,
                              const char **out_name, const char **out_type, size_t *out_offset, size_t *out_size) {
    if (!struct_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        if (strcmp(si->name, struct_name) != 0) continue;
        if (idx >= si->n_fields) return 0;
        if (out_name) *out_name = si->fields[idx].name;
        if (out_type) *out_type = si->fields[idx].type_name;
        if (out_offset) *out_offset = si->fields[idx].offset;
        if (out_size) *out_size = si->fields[idx].size;
        return 1;
    }
    return 0;
}

StructInfo *sym_get_struct_info(SymbolTable *st, const char *name) {
    if (!st || !name) return NULL;
    for (size_t i = 0; i < st->n_structs; i++) {
        if (st->structs[i].name && strcmp(st->structs[i].name, name) == 0) {
            return &st->structs[i];
        }
    }
    return NULL;
}

void sym_set_exported(SymbolTable *st, const char *name) {
    if (!st || !name) return;
    /* Primero buscar en structs */
    for (size_t i = 0; i < st->n_structs; i++) {
        if (strcmp(st->structs[i].name, name) == 0) {
            st->structs[i].is_exported = 1;
            return;
        }
    }
    /* Luego buscar en variables globales */
    for (SymbolEntry *e = st->scopes[0]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            e->is_exported = 1;
            return;
        }
    }
}

int sym_is_exported(SymbolTable *st, const char *name) {
    if (!st || !name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        if (strcmp(st->structs[i].name, name) == 0) {
            return st->structs[i].is_exported;
        }
    }
    for (SymbolEntry *e = st->scopes[0]; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e->is_exported;
        }
    }
    return 0;
}

int sym_is_subclass_of(SymbolTable *st, const char *derived, const char *base) {
    if (!st || !derived || !base) return 0;
    if (strcmp(derived, base) == 0) return 1;
    StructInfo *si = sym_get_struct_info(st, derived);
    if (si) {
        for (size_t b = 0; b < si->n_bases; b++) {
            if (sym_is_subclass_of(st, si->base_names[b], base))
                return 1;
        }
    }
    return 0;
}
