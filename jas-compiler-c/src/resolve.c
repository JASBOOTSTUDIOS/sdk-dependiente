/* Pase de resolución: popula la tabla de símbolos desde el AST */

#include "resolve.h"
#include "nodes.h"
#include "diagnostic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void set_error_at(SymbolTable *st, int line, int col, const char *fmt, ...) {
    st->has_error = 1;
    st->err_line = line;
    st->err_col = col;
    va_list args;
    va_start(args, fmt);
    vsnprintf(st->last_error, sizeof(st->last_error), fmt, args);
    va_end(args);
}

static void resolve_block(ASTNode *node, SymbolTable *st);
static void resolve_statement(ASTNode *node, SymbolTable *st);
static const char *resolve_expression(ASTNode *node, SymbolTable *st);

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

int resolve_program(ASTNode *ast, SymbolTable *st) {
    int resolve_errs = 0;
    if (!ast || ast->type != NODE_PROGRAM) return 0;
    ProgramNode *p = (ProgramNode *)ast;

    // fprintf(stderr, "[resolve] inicio\n");

    /* Structs predefinidos vec2, vec3, vec4; mat3 e0..e8, mat4 e0..e15 (row-major, coincide con VM) */
    {
        const char *v2_fields[] = {"x", "y"}, *v2_types[] = {"flotante", "flotante"};
        const char *v3_fields[] = {"x", "y", "z"}, *v3_types[] = {"flotante", "flotante", "flotante"};
        const char *v4_fields[] = {"x", "y", "z", "w"}, *v4_types[] = {"flotante", "flotante", "flotante", "flotante"};
        Visibility v2_vis[] = {VISIBILITY_PUBLIC, VISIBILITY_PUBLIC};
        sym_register_struct(st, "vec2", v2_types, v2_fields, 2, v2_vis, VISIBILITY_PUBLIC);
        Visibility v3_vis[] = {VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC};
        sym_register_struct(st, "vec3", v3_types, v3_fields, 3, v3_vis, VISIBILITY_PUBLIC);
        Visibility v4_vis[] = {VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC};
        sym_register_struct(st, "vec4", v4_types, v4_fields, 4, v4_vis, VISIBILITY_PUBLIC);
        const char *m3_types[] = {
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
        };
        const char *m3_fields[] = { "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8" };
        Visibility m3_vis[] = {VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC};
        sym_register_struct(st, "mat3", m3_types, m3_fields, 9, m3_vis, VISIBILITY_PUBLIC);
        const char *m4_types[] = {
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
        };
        const char *m4_fields[] = {
            "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7",
            "e8", "e9", "e10", "e11", "e12", "e13", "e14", "e15",
        };
        Visibility m4_vis[] = {VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC, VISIBILITY_PUBLIC};
        sym_register_struct(st, "mat4", m4_types, m4_fields, 16, m4_vis, VISIBILITY_PUBLIC);
    }

    
    /* Registrar structs (3.7) y clases con extiende */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_STRUCT_DEF) {
            StructDefNode *sd = (StructDefNode *)g;
            if (sd->extends_name && sd->extends_name[0]) {
                int er = sym_register_struct_extends(st, sd->name, sd->extends_name,
                    (const char **)sd->field_types, (const char **)sd->field_names, sd->n_fields,
                    sd->field_visibilities, sd->visibility);
                if (er == -1) {
                    fprintf(stderr, "Error semantico: la clase/registro '%s' extiende '%s', pero el tipo base no esta registrado (definalo antes en el archivo).\n",
                            sd->name ? sd->name : "?", sd->extends_name);
                    resolve_errs++;
                } else if (er == -2) {
                    fprintf(stderr, "Error semantico: la clase '%s' redefine el campo de '%s'.\n",
                            sd->name ? sd->name : "?", sd->extends_name);
                    resolve_errs++;
                }
            } else {
                sym_register_struct(st, sd->name, (const char **)sd->field_types,
                                   (const char **)sd->field_names, sd->n_fields,
                                   sd->field_visibilities, sd->visibility);
            }
        }
    }

    // fprintf(stderr, "[resolve] despues de registrar structs\n");

    /* Variables globales (VarDecl en globals) */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode *)g;
            size_t sz = type_size(st, vd->type_name);
            sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type, vd->visibility);
        }
    }

    // fprintf(stderr, "[resolve] despues de globals\n");

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
                sym_declare(st, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type, vd->visibility);
        }
        resolve_block(fn->body, st);
        int func_unused = sym_exit_scope(st);
        if (func_unused > 0) {}
    }

    // fprintf(stderr, "[resolve] despues de funciones libres\n");
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (!g || g->type != NODE_STRUCT_DEF) continue;
        StructDefNode *sd = (StructDefNode *)g;
        st->current_class_name = sd->name; // Set current class context
        for (size_t m = 0; m < sd->n_methods; m++) {
            FunctionNode *fn = (FunctionNode *)sd->methods[m];
            if (!fn) continue;
            sym_enter_scope(st, 1);
            for (size_t j = 0; j < fn->n_params; j++) {
                VarDeclNode *vd = (VarDeclNode *)fn->params[j];
            if (vd)
                sym_declare(st, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type, vd->visibility);
            }
            resolve_block(fn->body, st);
            (void)sym_exit_scope(st);
        }
        st->current_class_name = NULL; // Reset current class context
    }
    // fprintf(stderr, "[resolve] fin ok\n");
    return resolve_errs;
}

static void resolve_block(ASTNode *node, SymbolTable *st) {
    if (!node || node->type != NODE_BLOCK) return;
    BlockNode *b = (BlockNode *)node;
    for (size_t i = 0; i < b->n; i++)
        resolve_statement(b->statements[i], st);
}

static const char *resolve_expression(ASTNode *node, SymbolTable *st) {
    if (!node) return NULL;

    switch (node->type) {
        case NODE_IDENTIFIER: {
            IdentifierNode *idn = (IdentifierNode *)node;
            SymResult res = sym_lookup(st, idn->name);
            if (!res.found) {
                set_error_at(st, node->line, node->col, "Variable '%s' no declarada.", idn->name);
                return NULL;
            }
            return res.type_name;
        }
        case NODE_MEMBER_ACCESS: {
            MemberAccessNode *man = (MemberAccessNode *)node;
            const char *base_type = resolve_expression(man->target, st);
            if (!base_type) return NULL;

            size_t offset, size;
            const char *field_type;
            Visibility visibility;
            int found = sym_get_struct_field(st, base_type, man->member, &offset, &field_type, &size, &visibility);

            if (!found) {
                set_error_at(st, node->line, node->col, "Miembro '%s' no encontrado en '%s'.", man->member, base_type);
                return NULL;
            }

            if (visibility == VISIBILITY_PRIVATE) {
                if (!st->current_class_name || strcmp(st->current_class_name, base_type) != 0) {
                    fprintf(stderr,
                            "Error semantico: acceso a miembro privado '%s' de la clase '%s' desde fuera de la clase (linea %d, columna %d).\n",
                            man->member, base_type, node->line, node->col);
                    return NULL;
                }
            }
            return field_type;
        }
        case NODE_LITERAL: {
            LiteralNode *ln = (LiteralNode *)node;
            return ln->type_name;
        }
        case NODE_BINARY_OP: {
            BinaryOpNode *bon = (BinaryOpNode *)node;
            resolve_expression(bon->left, st);
            resolve_expression(bon->right, st);
            return "desconocido"; // TODO: Implement type checking for binary ops
        }
        case NODE_UNARY_OP: {
            UnaryOpNode *uon = (UnaryOpNode *)node;
            resolve_expression(uon->expression, st);
            return "desconocido"; // TODO: Implement type checking for unary ops
        }
        case NODE_CALL: {
            CallNode *cn = (CallNode *)node;
            for (size_t i = 0; i < cn->n_args; i++) {
                resolve_expression(cn->args[i], st);
            }
            return "desconocido"; // TODO: Implement type checking for function calls
        }
        case NODE_INDEX_ACCESS: {
            IndexAccessNode *ian = (IndexAccessNode *)node;
            resolve_expression(ian->target, st);
            resolve_expression(ian->index, st);
            return "desconocido"; // TODO: Implement type checking for index access
        }
        case NODE_TERNARY: {
            TernaryNode *tn = (TernaryNode *)node;
            resolve_expression(tn->condition, st);
            resolve_expression(tn->true_expr, st);
            resolve_expression(tn->false_expr, st);
            return "desconocido"; // TODO: Implement type checking for ternary operator
        }
        default:
            return "desconocido";
    }
}

static void resolve_statement(ASTNode *node, SymbolTable *st) {
    if (!node) return;
    switch (node->type) {
        case NODE_INPUT: {
            InputNode *in = (InputNode *)node;
            if (in->variable)
                sym_declare(st, in->variable, "texto", 8, 0, 0, NULL, VISIBILITY_PUBLIC);
            break;
        }
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode *)node;
            if (vd->value) {
                if (vd->value->type == NODE_LAMBDA_DECL) {
                    sym_declare_macro(st, vd->name, vd->value);
                } else {
                    resolve_expression(vd->value, st);
                    size_t sz = type_size(st, vd->type_name);
                    sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type, vd->visibility);
                }
            } else {
                size_t sz = type_size(st, vd->type_name);
                sym_declare(st, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0, vd->list_element_type, vd->visibility);
            }
            break;
        }
        case NODE_ASSIGNMENT: {
            AssignmentNode *an = (AssignmentNode *)node;
            resolve_expression(an->target, st);
            resolve_expression(an->expression, st);
            break;
        }
        case NODE_CALL: {
            CallNode *cn = (CallNode *)node;
            for (size_t i = 0; i < cn->n_args; i++) {
                resolve_expression(cn->args[i], st);
            }
            break;
        }
        case NODE_RETURN: {
            ReturnNode *rn = (ReturnNode *)node;
            if (rn->expression) {
                resolve_expression(rn->expression, st);
            }
            break;
        }
        case NODE_PRINT: {
            PrintNode *pn = (PrintNode *)node;
            resolve_expression(pn->expression, st); // Corrected from pn->expr to pn->expression
            break;
        }
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode *)node;
            resolve_expression(fe->collection, st);
            sym_enter_scope(st, 0);
            if (fe->iter_name && fe->iter_type)
                sym_declare(st, fe->iter_name, fe->iter_type, 8, 0, 0, NULL, VISIBILITY_PUBLIC); // For loop iterators are always public
            resolve_block(fe->body, st);
            sym_exit_scope(st);
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode *)node;
            resolve_expression(wn->condition, st);
            sym_enter_scope(st, 0);
            resolve_block(wn->body, st);
            sym_exit_scope(st);
            break;
        }
        case NODE_DO_WHILE: {
            DoWhileNode *dwn = (DoWhileNode *)node;
            resolve_expression(dwn->condition, st);
            sym_enter_scope(st, 0);
            resolve_block(dwn->body, st);
            sym_exit_scope(st);
            break;
        }
        case NODE_IF: {
            IfNode *in = (IfNode *)node;
            resolve_expression(in->condition, st);
            sym_enter_scope(st, 0);
            resolve_block(in->body, st);
            sym_exit_scope(st);
            if (in->else_body) {
                sym_enter_scope(st, 0);
                resolve_block(in->else_body, st);
                sym_exit_scope(st);
            }
            break;
        }
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode*)node;
            resolve_expression(sn->selector, st);
            for (size_t i = 0; i < sn->n_cases; i++) {
            for (size_t j = 0; j < sn->cases[i].n_values; j++) {
                resolve_expression(sn->cases[i].values[j], st);
            }
                sym_enter_scope(st, 0);
                resolve_block(sn->cases[i].body, st);
                sym_exit_scope(st);
            }
            if (sn->default_body) {
                sym_enter_scope(st, 0);
                resolve_block(sn->default_body, st);
                sym_exit_scope(st);
            }
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode*)node;
            sym_enter_scope(st, 0);
            resolve_block(tn->try_body, st);
            sym_exit_scope(st);
            if (tn->catch_body) {
                sym_enter_scope(st, 0);
                if (tn->catch_var) sym_declare(st, tn->catch_var, "texto", 8, 0, 0, NULL, VISIBILITY_PUBLIC); // Catch variables are always public
                resolve_block(tn->catch_body, st);
                sym_exit_scope(st);
            }
            if (tn->final_body) {
                sym_enter_scope(st, 0);
                resolve_block(tn->final_body, st);
                sym_exit_scope(st);
            }
            break;
        }
        default:
            break;
    }
}
