/* Pase de resolución: popula la tabla de símbolos desde el AST */

#include "resolve.h"
#include "nodes.h"
#include "diagnostic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Mismo estilo que do_compile / lexer en main.c */
#define RES_ANSI_RED   "\x1b[31m"
#define RES_ANSI_RESET "\x1b[0m"

static void resolve_block(ASTNode *node, SymbolTable *st, int *errs, const char *source, const char *diag_path);
static void resolve_statement(ASTNode *node, SymbolTable *st, int *errs, const char *source, const char *diag_path);

/** Emite error semantico con fragmento de codigo (igual que diag_attach_snippet en lexer/parser). */
static void resolve_emit_semantic(const char *source, const char *diag_path, int line, int col, const char *detail) {
    char head[6144];
    const char *path = (diag_path && diag_path[0]) ? diag_path : "(sin ruta)";
    if (line >= 1 && col >= 1)
        snprintf(head, sizeof head, "Archivo %s, linea %d, columna %d: error semantico: %s", path, line, col, detail);
    else
        snprintf(head, sizeof head, "Archivo %s: error semantico: %s", path, detail);

    if (source && line >= 1 && col >= 1) {
        char *full = diag_attach_snippet(source, line, col, head);
        fprintf(stderr, "%s%s%s", RES_ANSI_RED, full ? full : head, RES_ANSI_RESET);
        if (full) free(full);
    } else
        fprintf(stderr, "%s%s%s", RES_ANSI_RED, head, RES_ANSI_RESET);
}

/* lista, lista?, mapa, mapa?, elemento */
static int type_is_foreach_collection(const char *type_name) {
    if (!type_name) return 0;
    return strcmp(type_name, "lista") == 0 || strcmp(type_name, "lista?") == 0 ||
           strcmp(type_name, "mapa") == 0 || strcmp(type_name, "mapa?") == 0 ||
           strcmp(type_name, "elemento") == 0 || strcmp(type_name, "json") == 0;
}

static void validate_foreach_types(ForEachNode *fe, SymbolTable *st, int *errs,
                                   const char *source, const char *diag_path) {
    if (!fe || !errs) return;
    if (!fe->collection || fe->collection->type != NODE_IDENTIFIER) return;
    if (!fe->iter_type || !fe->iter_name) return;

    IdentifierNode *coll_id = (IdentifierNode *)fe->collection;
    const char *coll_name = coll_id->name;
    const char *ct = sym_lookup_type(st, coll_name);
    int line = fe->base.line > 0 ? fe->base.line : coll_id->base.line;
    int col = fe->base.col > 0 ? fe->base.col : coll_id->base.col;

    if (!ct)
        return;

    if (!type_is_foreach_collection(ct)) {
        /* Caso especial: para cada caracter ch sobre texto */
        if (strcmp(ct, "texto") == 0 && strcmp(fe->iter_type, "caracter") == 0) {
            return;
        }
        char detail[1536];
        snprintf(detail, sizeof detail,
                 "en 'para cada' solo se puede iterar sobre lista o mapa.\n"
                 "  La variable \"%s\" tiene tipo \"%s\".\n"
                 "  Ayuda: use una coleccion (por ejemplo lista<entero> datos o mapa<texto> tabla).",
                 coll_name, ct);
        resolve_emit_semantic(source, diag_path, line, col, detail);
        (*errs)++;
        return;
    }

    if (strcmp(ct, "json") == 0)
        return;

    const char *elem_t = sym_lookup_collection_elem_type(st, coll_name);
    if (!elem_t)
        return;
    if (strcmp(fe->iter_type, "elemento") == 0)
        return;
    if (strcmp(fe->iter_type, elem_t) != 0) {
        char detail[1536];
        snprintf(detail, sizeof detail,
                 "el tipo del iterador no coincide con los elementos de la coleccion.\n"
                 "  Coleccion \"%s\": cada elemento es de tipo \"%s\". Iterador declarado: %s %s.\n"
                 "  Ayuda: use \"%s %s\" o \"elemento %s\". En mapa<T> cada paso entrega un valor de tipo T (no la clave).",
                 coll_name, elem_t, fe->iter_type, fe->iter_name, elem_t, fe->iter_name, fe->iter_name);
        resolve_emit_semantic(source, diag_path, line, col, detail);
        (*errs)++;
    }
}

static size_t type_size(SymbolTable *st, const char *type_name) {
    if (!type_name) return 8;
    size_t s = sym_get_struct_size(st, type_name);
    if (s > 0) return s;
    if (strcmp(type_name, "u32") == 0 || strcmp(type_name, "u8") == 0 || strcmp(type_name, "byte") == 0) return 8;
    if (strcmp(type_name, "texto") == 0 || strcmp(type_name, "lista") == 0 || strcmp(type_name, "mapa") == 0 ||
        strcmp(type_name, "objeto") == 0 || strcmp(type_name, "json") == 0)
        return 8;
    if (strcmp(type_name, "bytes") == 0 || strcmp(type_name, "socket") == 0 || strcmp(type_name, "tls") == 0 ||
        strcmp(type_name, "http_solicitud") == 0 || strcmp(type_name, "http_respuesta") == 0 || strcmp(type_name, "http_servidor") == 0)
        return 8;
    if (strcmp(type_name, "vec2") == 0) return 16;
    if (strcmp(type_name, "vec3") == 0) return 24;
    if (strcmp(type_name, "vec4") == 0) return 32;
    if (strcmp(type_name, "mat4") == 0) return 128;  /* 16 flotantes × 8 bytes, row-major */
    if (strcmp(type_name, "mat3") == 0) return 72;   /* 9 flotantes × 8 bytes, row-major */
    return 8;
}

static void register_struct_recursive(SymbolTable *st, ASTNode *node, int *errs, int report_errors,
                                     const char *source, const char *diag_path) {
    if (!node || node->type != NODE_STRUCT_DEF) return;
    StructDefNode *sd = (StructDefNode *)node;

    const char **mnames = sd->n_methods ? malloc(sd->n_methods * sizeof(char*)) : NULL;
    void **masts = sd->n_methods ? malloc(sd->n_methods * sizeof(void*)) : NULL;
    for (size_t j = 0; j < sd->n_methods; j++) {
        FunctionNode *fn = (FunctionNode*)sd->methods[j];
        mnames[j] = fn ? fn->name : "?";
        masts[j] = sd->methods[j];
    }

    if (sd->n_extends > 0) {
        int er = sym_register_class_extends(st, sd->name, (const char **)sd->extends_names, sd->n_extends,
            (const char **)sd->field_types, (const char **)sd->field_names, sd->field_visibilities, sd->n_fields,
            masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported, sd->is_clase);
        if (er != 0) {
            if (report_errors) {
                int sl = sd->base.line > 0 ? sd->base.line : 1;
                int sc = sd->base.col > 0 ? sd->base.col : 1;
                if (er == -1) {
                    char detail[384];
                    snprintf(detail, sizeof detail,
                             "la clase o registro \"%s\" extiende una base que no esta registrada.",
                             sd->name ? sd->name : "?");
                    resolve_emit_semantic(source, diag_path, sl, sc, detail);
                } else if (er == -2) {
                    char detail[384];
                    snprintf(detail, sizeof detail,
                             "la clase \"%s\" redefine un campo que ya existe en una clase base.",
                             sd->name ? sd->name : "?");
                    resolve_emit_semantic(source, diag_path, sl, sc, detail);
                }
            }
            (*errs)++;
        }
    } else {
        sym_register_class(st, sd->name, (const char **)sd->field_types,
                           (const char **)sd->field_names, sd->field_visibilities, sd->n_fields,
                           masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported, sd->is_clase);
    }
    if (mnames) free(mnames);
    if (masts) free(masts);

    for (size_t i = 0; i < sd->n_nested_structs; i++) {
        register_struct_recursive(st, sd->nested_structs[i], errs, report_errors, source, diag_path);
    }
}

static void resolve_struct_methods_recursive(SymbolTable *st, ASTNode *node, int *errs,
                                            const char *source, const char *diag_path) {
    if (!node || node->type != NODE_STRUCT_DEF) return;
    StructDefNode *sd = (StructDefNode *)node;
    
    for (size_t j = 0; j < sd->n_methods; j++) {
        FunctionNode *fn = (FunctionNode *)sd->methods[j];
        if (!fn) continue;
        sym_enter_scope(st, 1);
        /* 'este' apunta a la instancia de la clase */
        sym_declare(st, "este", sd->name, 8, 1, 0, NULL, SYMDECL_FLAGS_ALLOW_RESERVED_NAME);
        if (sd->n_extends > 0) {
            /* 'padre' apunta a la misma instancia pero con el tipo de la primera clase base */
            sym_declare(st, "padre", sd->extends_names[0], 8, 1, 0, NULL, SYMDECL_FLAGS_ALLOW_RESERVED_NAME);
        }
        for (size_t k = 0; k < fn->n_params; k++) {
            VarDeclNode *vd = (VarDeclNode *)fn->params[k];
            if (vd)
                sym_declare(st, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type, SYMDECL_FLAGS_NONE);
        }
        resolve_block(fn->body, st, errs, source, diag_path);
        sym_exit_scope(st);
    }

    for (size_t i = 0; i < sd->n_nested_structs; i++) {
        resolve_struct_methods_recursive(st, sd->nested_structs[i], errs, source, diag_path);
    }
}

int resolve_program(ASTNode *ast, SymbolTable *st, const char *source, const char *diag_path) {
    int resolve_errs = 0;
    if (!ast || ast->type != NODE_PROGRAM) return 0;
    ProgramNode *p = (ProgramNode *)ast;

    /* Structs predefinidos vec2, vec3, vec4; mat3 e0..e8, mat4 e0..e15 (row-major, coincide con VM) */
    {
        const char *v2_fields[] = {"x", "y"}, *v2_types[] = {"flotante", "flotante"};
        const char *v3_fields[] = {"x", "y", "z"}, *v3_types[] = {"flotante", "flotante", "flotante"};
        const char *v4_fields[] = {"x", "y", "z", "w"}, *v4_types[] = {"flotante", "flotante", "flotante", "flotante"};
        sym_register_struct(st, "vec2", v2_types, v2_fields, 2);
        sym_register_struct(st, "vec3", v3_types, v3_fields, 3);
        sym_register_struct(st, "vec4", v4_types, v4_fields, 4);
        const char *m3_types[] = {
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
        };
        const char *m3_fields[] = { "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8" };
        sym_register_struct(st, "mat3", m3_types, m3_fields, 9);
        const char *m4_types[] = {
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
        };
        const char *m4_fields[] = {
            "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7",
            "e8", "e9", "e10", "e11", "e12", "e13", "e14", "e15",
        };
        sym_register_struct(st, "mat4", m4_types, m4_fields, 16);
    }

    /* Registrar structs (3.7) y clases con extiende (Multi-pasada para herencia) */
    int changed = 1;
    int structs_left = 0;
    while (changed) {
        changed = 0;
        structs_left = 0;
        for (size_t i = 0; i < p->n_globals; i++) {
            ASTNode *g = p->globals[i];
            if (g && g->type == NODE_STRUCT_DEF) {
                StructDefNode *sd = (StructDefNode*)g;
                if (sym_get_struct_info(st, sd->name)) continue; /* Ya registrado */
                
                int local_errs = 0;
                register_struct_recursive(st, g, &local_errs, 0, source, diag_path);
                if (local_errs == 0) {
                    changed = 1;
                } else {
                    structs_left++;
                }
            }
        }
    }
    if (structs_left > 0) {
        /* Intento final para reportar errores reales de base faltante */
        for (size_t i = 0; i < p->n_globals; i++) {
            if (p->globals[i] && p->globals[i]->type == NODE_STRUCT_DEF) {
                StructDefNode *sd = (StructDefNode*)p->globals[i];
                if (!sym_get_struct_info(st, sd->name)) {
                    register_struct_recursive(st, p->globals[i], &resolve_errs, 1, source, diag_path);
                }
            }
        }
    }

    /* Variables globales (VarDecl en globals) */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode *)g;
            size_t sz = type_size(st, vd->type_name);
            sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type, SYMDECL_FLAGS_NONE);
        }
    }

    /* Principal: enter_scope (función), resolver bloque */
    sym_enter_scope(st, 1);
    resolve_block(p->main_block, st, &resolve_errs, source, diag_path);
    int main_unused = sym_exit_scope(st);
    if (main_unused > 0) {} /* reservado */
    
    /* Funciones */
    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *fn = (FunctionNode *)p->functions[i];
        if (!fn) continue;
        sym_enter_scope(st, 1);
        for (size_t j = 0; j < fn->n_params; j++) {
            VarDeclNode *vd = (VarDeclNode *)fn->params[j];
            if (vd)
                sym_declare(st, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type, SYMDECL_FLAGS_NONE);
        }
        resolve_block(fn->body, st, &resolve_errs, source, diag_path);
        int func_unused = sym_exit_scope(st);
        if (func_unused > 0) {}
    }

    /* Metodos de clases */
    for (size_t i = 0; i < p->n_globals; i++) {
        resolve_struct_methods_recursive(st, p->globals[i], &resolve_errs, source, diag_path);
    }
    return resolve_errs;
}

static void resolve_block(ASTNode *node, SymbolTable *st, int *errs, const char *source, const char *diag_path) {
    if (!node || node->type != NODE_BLOCK) return;
    BlockNode *b = (BlockNode *)node;
    for (size_t i = 0; i < b->n; i++)
        resolve_statement(b->statements[i], st, errs, source, diag_path);
}

static void resolve_statement(ASTNode *node, SymbolTable *st, int *errs, const char *source, const char *diag_path) {
    if (!node) return;
    switch (node->type) {
        case NODE_INPUT: {
            InputNode *in = (InputNode *)node;
            if (in->variable)
                sym_declare(st, in->variable, "texto", 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
            break;
        }
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode *)node;
            if (vd->value && vd->value->type == NODE_LAMBDA_DECL) {
                sym_declare_macro(st, vd->name, vd->value);
            } else {
                size_t sz = type_size(st, vd->type_name);
                sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type, SYMDECL_FLAGS_NONE);
            }
            break;
        }
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode *)node;
            if (errs)
                validate_foreach_types(fe, st, errs, source, diag_path);
            sym_enter_scope(st, 0);
            if (fe->iter_name && fe->iter_type)
                sym_declare(st, fe->iter_name, fe->iter_type, 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
            if (fe->key_name)
                sym_declare(st, fe->key_name, "entero", 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
            resolve_block(fe->body, st, errs, source, diag_path);
            sym_exit_scope(st);
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode *)node;
            resolve_block(wn->body, st, errs, source, diag_path);
            break;
        }
        case NODE_DO_WHILE: {
            DoWhileNode *dn = (DoWhileNode *)node;
            resolve_block(dn->body, st, errs, source, diag_path);
            break;
        }
        case NODE_IF: {
            IfNode *in = (IfNode *)node;
            resolve_block(in->body, st, errs, source, diag_path);
            if (in->else_body) resolve_block(in->else_body, st, errs, source, diag_path);
            break;
        }
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode *)node;
            for (size_t i = 0; i < sn->n_cases; i++)
                resolve_block(sn->cases[i].body, st, errs, source, diag_path);
            if (sn->default_body) resolve_block(sn->default_body, st, errs, source, diag_path);
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode *)node;
            resolve_block(tn->try_body, st, errs, source, diag_path);
            if (tn->catch_body) resolve_block(tn->catch_body, st, errs, source, diag_path);
            if (tn->final_body) resolve_block(tn->final_body, st, errs, source, diag_path);
            break;
        }
        case NODE_EXPORT_DIRECTIVE: {
            ExportDirectiveNode *en = (ExportDirectiveNode *)node;
            for (size_t i = 0; i < en->n_names; i++) {
                sym_set_exported(st, en->names[i]);
            }
            break;
        }
        default:
            break;
    }
}
