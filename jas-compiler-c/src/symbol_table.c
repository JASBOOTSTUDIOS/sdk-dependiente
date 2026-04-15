/* Implementación tabla de símbolos - Nivel 3 */

#include "symbol_table.h"
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
    st->next_global_offset = 0x0800;  /* 3.2 */
    st->is_global = 1;
    st->scope_depth = 1;
    st->scopes[0] = NULL;
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
        if (si->name) free(si->name);
        if (si->base_name) free(si->base_name);
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
SymResult sym_declare(SymbolTable *st, const char *name, const char *type_name, size_t size, int is_param, int is_const, const char *lista_elem_type) {
    SymResult r = {0, 0, 0, 0, NULL, NULL};
    if (!name || strlen(name) >= SYM_ENTRY_NAME_MAX) return r;

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
    SymResult r = sym_declare(st, name, "macro", 0, 0, 1, NULL);
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
    return sym_declare(st, name, type_name, DEFAULT_SIZE, 0, 0, NULL);
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
    sym_register_class(st, name, field_types, field_names, NULL, n_fields, NULL, NULL, NULL, 0, 0);
}

void sym_register_class(SymbolTable *st, const char *name, const char **field_types, const char **field_names, const int *field_vis, size_t n_fields,
                        void **method_asts, const char **method_names, const int *method_vis, size_t n_methods, int is_exported) {
    if (!name || (n_fields > 0 && (!field_types || !field_names))) return;
    if (st->n_structs >= st->structs_cap) {
        size_t new_cap = st->structs_cap ? st->structs_cap * 2 : 8;
        StructInfo *p = realloc(st->structs, new_cap * sizeof(StructInfo));
        if (!p) return;
        st->structs = p;
        st->structs_cap = new_cap;
    }
    StructInfo *si = &st->structs[st->n_structs++];
    si->name = strdup(name);
    si->base_name = NULL;
    si->fields = n_fields ? calloc(n_fields, sizeof(StructFieldInfo)) : NULL;
    si->n_fields = n_fields;
    si->is_exported = is_exported;
    
    size_t offset = 0;
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
}

int sym_register_struct_extends(SymbolTable *st, const char *name, const char *base_name,
                                const char **field_types, const char **field_names, size_t n_fields) {
    return sym_register_class_extends(st, name, base_name, field_types, field_names, NULL, n_fields, NULL, NULL, NULL, 0, 0);
}

int sym_register_class_extends(SymbolTable *st, const char *name, const char *base_name,
                               const char **field_types, const char **field_names, const int *field_vis, size_t n_fields,
                               void **method_asts, const char **method_names, const int *method_vis, size_t n_methods, int is_exported) {
    StructInfo *base_si = NULL;
    for (size_t i = 0; i < st->n_structs; i++) {
        if (strcmp(st->structs[i].name, base_name) == 0) {
            base_si = &st->structs[i];
            break;
        }
    }
    if (!base_si) {
        // printf("Base class NOT FOUND: %s\n", base_name);
        return -1;
    }

    /* Campos: heredar de la base */
    size_t total_f = base_si->n_fields + n_fields;
    const char **tf = malloc(total_f * sizeof(char*));
    const char **nf = malloc(total_f * sizeof(char*));
    int *vf = malloc(total_f * sizeof(int));
    
    for (size_t i = 0; i < base_si->n_fields; i++) {
        tf[i] = base_si->fields[i].type_name;
        nf[i] = base_si->fields[i].name;
        vf[i] = base_si->fields[i].is_private;
    }
    for (size_t i = 0; i < n_fields; i++) {
        tf[base_si->n_fields + i] = field_types[i];
        nf[base_si->n_fields + i] = field_names[i];
        vf[base_si->n_fields + i] = field_vis ? field_vis[i] : 0;
    }

    /* Métodos: heredar de la base (excepto sobreescritos) */
    size_t inherited_m_count = 0;
    for (size_t i = 0; i < base_si->n_methods; i++) {
        int overridden = 0;
        for (size_t j = 0; j < n_methods; j++) {
            if (strcmp(base_si->methods[i].name, method_names[j]) == 0) {
                overridden = 1;
                break;
            }
        }
        if (!overridden) inherited_m_count++;
    }

    size_t total_m = inherited_m_count + n_methods;
    void **tm_asts = malloc(total_m * sizeof(void*));
    const char **tm_names = malloc(total_m * sizeof(char*));
    int *tm_vis = malloc(total_m * sizeof(int));

    size_t m_idx = 0;
    /* Primero los heredados no sobreescritos */
    for (size_t i = 0; i < base_si->n_methods; i++) {
        int overridden = 0;
        for (size_t j = 0; j < n_methods; j++) {
            if (strcmp(base_si->methods[i].name, method_names[j]) == 0) {
                overridden = 1;
                break;
            }
        }
        if (!overridden) {
            tm_asts[m_idx] = base_si->methods[i].method_ast;
            tm_names[m_idx] = base_si->methods[i].name;
            tm_vis[m_idx] = base_si->methods[i].is_private;
            m_idx++;
        }
    }
    /* Luego los nuevos / sobreescritos */
    for (size_t i = 0; i < n_methods; i++) {
        tm_asts[m_idx] = method_asts[i];
        tm_names[m_idx] = method_names[i];
        tm_vis[m_idx] = method_vis ? method_vis[i] : 0;
        m_idx++;
    }
    
    sym_register_class(st, name, tf, nf, vf, total_f, tm_asts, tm_names, tm_vis, total_m, is_exported);
    st->structs[st->n_structs - 1].base_name = strdup(base_name);
    
    free(tf); free(nf); free(vf);
    free(tm_asts); free(tm_names); free(tm_vis);
    return 0;
}

int sym_get_struct_field(SymbolTable *st, const char *struct_name, const char *field_name, size_t *out_offset, const char **out_type, size_t *out_size) {
    if (!struct_name || !field_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        if (strcmp(si->name, struct_name) != 0) continue;
        for (size_t j = 0; j < si->n_fields; j++) {
            if (strcmp(si->fields[j].name, field_name) == 0) {
                if (out_offset) *out_offset = si->fields[j].offset;
                if (out_type) *out_type = si->fields[j].type_name;
                if (out_size) *out_size = si->fields[j].size;
                return 1;
            }
        }
    }
    return 0;
}

size_t sym_get_struct_size(SymbolTable *st, const char *struct_name) {
    if (!struct_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        // printf("Checking struct: '%s' vs '%s'\n", st->structs[i].name, struct_name);
        if (st->structs[i].name && strcmp(st->structs[i].name, struct_name) == 0)
            return st->structs[i].total_size;
    }
    return 0;
}

int sym_get_struct_field_visibility(SymbolTable *st, const char *struct_name, const char *field_name, int *out_is_private) {
    if (!struct_name || !field_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        if (strcmp(si->name, struct_name) != 0) continue;
        for (size_t j = 0; j < si->n_fields; j++) {
            if (strcmp(si->fields[j].name, field_name) == 0) {
                if (out_is_private) *out_is_private = si->fields[j].is_private;
                return 1;
            }
        }
    }
    return 0;
}

int sym_get_struct_method(SymbolTable *st, const char *struct_name, const char *method_name, void **out_method_ast) {
    if (!struct_name || !method_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        if (strcmp(si->name, struct_name) != 0) continue;
        for (size_t j = 0; j < si->n_methods; j++) {
            if (strcmp(si->methods[j].name, method_name) == 0) {
                if (out_method_ast) *out_method_ast = si->methods[j].method_ast;
                return 1;
            }
        }
    }
    return 0;
}

int sym_get_struct_method_visibility(SymbolTable *st, const char *struct_name, const char *method_name, int *out_is_private) {
    if (!struct_name || !method_name) return 0;
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        if (strcmp(si->name, struct_name) != 0) continue;
        for (size_t j = 0; j < si->n_methods; j++) {
            if (strcmp(si->methods[j].name, method_name) == 0) {
                if (out_is_private) *out_is_private = si->methods[j].is_private;
                return 1;
            }
        }
    }
    return 0;
}

const char *sym_get_struct_lista_elem_type(SymbolTable *st, const char *struct_name, const char *field_name) {
    if (!struct_name || !field_name) return NULL;
    for (size_t i = 0; i < st->n_structs; i++) {
        StructInfo *si = &st->structs[i];
        if (strcmp(si->name, struct_name) != 0) continue;
        for (size_t j = 0; j < si->n_fields; j++) {
            if (strcmp(si->fields[j].name, field_name) == 0) {
                /* Extraer T de lista<T> */
                const char *tn = si->fields[j].type_name;
                if (tn && strncmp(tn, "lista<", 6) == 0) {
                    char *p = strchr(tn, '<');
                    if (p) {
                        static char buf[128];
                        strncpy(buf, p + 1, 127);
                        char *q = strrchr(buf, '>');
                        if (q) *q = '\0';
                        return buf;
                    }
                }
                return NULL;
            }
        }
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
