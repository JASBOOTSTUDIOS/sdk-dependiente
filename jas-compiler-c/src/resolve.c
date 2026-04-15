/* Pase de resolución: popula la tabla de símbolos desde el AST */

#include "resolve.h"
#include "nodes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void resolve_block(ASTNode *node, SymbolTable *st);
static void resolve_statement(ASTNode *node, SymbolTable *st);

static size_t type_size(SymbolTable *st, const char *type_name) {
    if (!type_name) return 8;
    size_t s = sym_get_struct_size(st, type_name);
    if (s > 0) return s;
    if (strcmp(type_name, "u32") == 0 || strcmp(type_name, "u8") == 0 || strcmp(type_name, "byte") == 0) return 8;
    if (strcmp(type_name, "texto") == 0 || strcmp(type_name, "lista") == 0 || strcmp(type_name, "mapa") == 0 ||
        strcmp(type_name, "objeto") == 0)
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

static void register_struct_recursive(SymbolTable *st, ASTNode *node, int *errs) {
    if (!node || node->type != NODE_STRUCT_DEF) return;
    StructDefNode *sd = (StructDefNode *)node;

    const char **mnames = sd->n_methods ? malloc(sd->n_methods * sizeof(char*)) : NULL;
    void **masts = sd->n_methods ? malloc(sd->n_methods * sizeof(void*)) : NULL;
    for (size_t j = 0; j < sd->n_methods; j++) {
        mnames[j] = ((FunctionNode*)sd->methods[j])->name;
        masts[j] = sd->methods[j];
    }

    if (sd->extends_name && sd->extends_name[0]) {
        int er = sym_register_class_extends(st, sd->name, sd->extends_name,
            (const char **)sd->field_types, (const char **)sd->field_names, sd->field_visibilities, sd->n_fields,
            masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported);
        if (er == -1) {
            fprintf(stderr, "Error semantico: la clase/registro '%s' extiende '%s', pero el tipo base no esta registrado.\n",
                    sd->name ? sd->name : "?", sd->extends_name);
            (*errs)++;
        } else if (er == -2) {
            fprintf(stderr, "Error semantico: la clase '%s' redefine el campo de '%s'.\n",
                    sd->name ? sd->name : "?", sd->extends_name);
            (*errs)++;
        }
    } else {
        sym_register_class(st, sd->name, (const char **)sd->field_types,
                           (const char **)sd->field_names, sd->field_visibilities, sd->n_fields,
                           masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported);
    }
    if (mnames) free(mnames);
    if (masts) free(masts);

    for (size_t i = 0; i < sd->n_nested_structs; i++) {
        register_struct_recursive(st, sd->nested_structs[i], errs);
    }
}

static void resolve_struct_methods_recursive(SymbolTable *st, ASTNode *node) {
    if (!node || node->type != NODE_STRUCT_DEF) return;
    StructDefNode *sd = (StructDefNode *)node;
    
    for (size_t j = 0; j < sd->n_methods; j++) {
        FunctionNode *fn = (FunctionNode *)sd->methods[j];
        sym_enter_scope(st, 1);
        /* 'este' apunta a la instancia de la clase */
        sym_declare(st, "este", sd->name, 8, 1, 0, NULL);
        for (size_t k = 0; k < fn->n_params; k++) {
            VarDeclNode *vd = (VarDeclNode *)fn->params[k];
            if (vd)
                sym_declare(st, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type);
        }
        resolve_block(fn->body, st);
        sym_exit_scope(st);
    }

    for (size_t i = 0; i < sd->n_nested_structs; i++) {
        resolve_struct_methods_recursive(st, sd->nested_structs[i]);
    }
}

int resolve_program(ASTNode *ast, SymbolTable *st) {
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

    /* Registrar structs (3.7) y clases con extiende */
    for (size_t i = 0; i < p->n_globals; i++) {
        register_struct_recursive(st, p->globals[i], &resolve_errs);
    }

    /* Variables globales (VarDecl en globals) */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode *)g;
            size_t sz = type_size(st, vd->type_name);
            sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type);
        }
    }

    /* Principal: enter_scope (función), resolver bloque */
    sym_enter_scope(st, 1);
    resolve_block(p->main_block, st);
    int main_unused = sym_exit_scope(st);
    if (main_unused > 0) {} // we ignore count for now if just want warnings or let the return count act
    
    /* Funciones */
    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *fn = (FunctionNode *)p->functions[i];
        if (!fn) continue;
        sym_enter_scope(st, 1);
        for (size_t j = 0; j < fn->n_params; j++) {
            VarDeclNode *vd = (VarDeclNode *)fn->params[j];
            if (vd)
                sym_declare(st, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type);
        }
        resolve_block(fn->body, st);
        int func_unused = sym_exit_scope(st);
        if (func_unused > 0) {}
    }

    /* Metodos de clases */
    for (size_t i = 0; i < p->n_globals; i++) {
        resolve_struct_methods_recursive(st, p->globals[i]);
    }
    return resolve_errs;
}

static void resolve_block(ASTNode *node, SymbolTable *st) {
    if (!node || node->type != NODE_BLOCK) return;
    BlockNode *b = (BlockNode *)node;
    for (size_t i = 0; i < b->n; i++)
        resolve_statement(b->statements[i], st);
}

static void resolve_statement(ASTNode *node, SymbolTable *st) {
    if (!node) return;
    switch (node->type) {
        case NODE_INPUT: {
            InputNode *in = (InputNode *)node;
            if (in->variable)
                sym_declare(st, in->variable, "texto", 8, 0, 0, NULL);
            break;
        }
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode *)node;
            if (vd->value && vd->value->type == NODE_LAMBDA_DECL) {
                sym_declare_macro(st, vd->name, vd->value);
            } else {
                size_t sz = type_size(st, vd->type_name);
                sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type);
            }
            break;
        }
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode *)node;
            sym_enter_scope(st, 0);
            if (fe->iter_name && fe->iter_type)
                sym_declare(st, fe->iter_name, fe->iter_type, 8, 0, 0, NULL);
            resolve_block(fe->body, st);
            sym_exit_scope(st);
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode *)node;
            resolve_block(wn->body, st);
            break;
        }
        case NODE_DO_WHILE: {
            DoWhileNode *dn = (DoWhileNode *)node;
            resolve_block(dn->body, st);
            break;
        }
        case NODE_IF: {
            IfNode *in = (IfNode *)node;
            resolve_block(in->body, st);
            if (in->else_body) resolve_block(in->else_body, st);
            break;
        }
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode *)node;
            for (size_t i = 0; i < sn->n_cases; i++)
                resolve_block(sn->cases[i].body, st);
            if (sn->default_body) resolve_block(sn->default_body, st);
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode *)node;
            resolve_block(tn->try_body, st);
            if (tn->catch_body) resolve_block(tn->catch_body, st);
            if (tn->final_body) resolve_block(tn->final_body, st);
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
