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
        free(si->name);
        for (size_t j = 0; j < si->n_fields; j++) {
            free(si->fields[j].name);
            free(si->fields[j].type_name);
        }
        free(si->fields);
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
    if (!name || !field_types || !field_names) return;
    if (st->n_structs >= st->structs_cap) {
        size_t new_cap = st->structs_cap ? st->structs_cap * 2 : 8;
        StructInfo *p = realloc(st->structs, new_cap * sizeof(StructInfo));
        if (!p) return;
        st->structs = p;
        st->structs_cap = new_cap;
    }
    StructInfo *si = &st->structs[st->n_structs];
    si->name = strdup(name);
    si->fields = calloc(n_fields, sizeof(StructFieldInfo));
    if (!si->fields) return;
    si->n_fields = n_fields;
    size_t offset = 0;
    for (size_t i = 0; i < n_fields; i++) {
        si->fields[i].name = strdup_safe(field_names[i]);
        si->fields[i].type_name = strdup_safe(field_types[i]);
        si->fields[i].offset = offset;
        size_t fsize = get_field_size(st, field_types[i]);
        si->fields[i].size = fsize;
        offset += fsize;
    }
    si->total_size = offset;
    st->n_structs++;
}

int sym_register_struct_extends(SymbolTable *st, const char *name, const char *base_name,
                                const char **field_types, const char **field_names, size_t n_fields) {
    if (!st || !name || !base_name) return -1;
    StructInfo *base_si = NULL;
    for (size_t i = 0; i < st->n_structs; i++) {
        if (strcmp(st->structs[i].name, base_name) == 0) {
            base_si = &st->structs[i];
            break;
        }
    }
    if (!base_si) return -1;

    for (size_t i = 0; i < n_fields; i++) {
        if (!field_names[i]) continue;
        for (size_t j = 0; j < base_si->n_fields; j++) {
            if (base_si->fields[j].name && strcmp(field_names[i], base_si->fields[j].name) == 0)
                return -2;
        }
    }

    if (st->n_structs >= st->structs_cap) {
        size_t new_cap = st->structs_cap ? st->structs_cap * 2 : 8;
        StructInfo *p = realloc(st->structs, new_cap * sizeof(StructInfo));
        if (!p) return -1;
        st->structs = p;
        st->structs_cap = new_cap;
    }
    StructInfo *si = &st->structs[st->n_structs];
    memset(si, 0, sizeof(*si));
    si->name = strdup(name);
    if (!si->name) return -1;
    size_t n_total = base_si->n_fields + n_fields;
    si->fields = calloc(n_total, sizeof(StructFieldInfo));
    if (!si->fields) {
        free(si->name);
        return -1;
    }
    si->n_fields = n_total;
    size_t offset = 0;
    for (size_t j = 0; j < base_si->n_fields; j++) {
        StructFieldInfo *f = &si->fields[j];
        f->name = strdup_safe(base_si->fields[j].name);
        f->type_name = strdup_safe(base_si->fields[j].type_name);
        f->offset = offset;
        f->size = base_si->fields[j].size;
        offset += f->size;
    }
    for (size_t i = 0; i < n_fields; i++) {
        StructFieldInfo *f = &si->fields[base_si->n_fields + i];
        f->name = strdup_safe(field_names[i]);
        f->type_name = strdup_safe(field_types[i]);
        f->offset = offset;
        size_t fsize = get_field_size(st, field_types[i]);
        f->size = fsize;
        offset += fsize;
    }
    si->total_size = offset;
    st->n_structs++;
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
        return 0;
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
        StructFieldInfo *f = &si->fields[idx];
        if (out_name) *out_name = f->name;
        if (out_type) *out_type = f->type_name;
        if (out_offset) *out_offset = f->offset;
        if (out_size) *out_size = f->size;
        return 1;
    }
    return 0;
}
