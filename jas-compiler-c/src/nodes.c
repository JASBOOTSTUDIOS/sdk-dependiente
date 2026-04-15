/* Implementación de nodos AST */

#include "nodes.h"
#include <stdlib.h>
#include <string.h>

static void free_str(char *s) { if (s) free(s); }

void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_PROGRAM: {
            ProgramNode *n = (ProgramNode*)node;
            for (size_t i = 0; i < n->n_funcs; i++) ast_free(n->functions[i]);
            free(n->functions);
            ast_free(n->main_block);
            for (size_t i = 0; i < n->n_globals; i++) ast_free(n->globals[i]);
            free(n->globals);
            break;
        }
        case NODE_BLOCK: {
            BlockNode *n = (BlockNode*)node;
            for (size_t i = 0; i < n->n; i++) ast_free(n->statements[i]);
            free(n->statements);
            break;
        }
        case NODE_FUNCTION: {
            FunctionNode *n = (FunctionNode*)node;
            free_str(n->name);
            free_str(n->return_type);
            free_str(n->return_task_elem);
            for (size_t i = 0; i < n->n_params; i++) ast_free(n->params[i]);
            free(n->params);
            ast_free(n->body);
            break;
        }
        case NODE_VAR_DECL: {
            VarDeclNode *n = (VarDeclNode*)node;
            free_str(n->type_name);
            free_str(n->name);
            free_str(n->list_element_type);
            ast_free(n->value);
            break;
        }
        case NODE_STRUCT_DEF: {
            StructDefNode *n = (StructDefNode*)node;
            free_str(n->name);
            free_str(n->extends_name);
            for (size_t i = 0; i < n->n_fields; i++) {
                free_str(n->field_types[i]);
                free_str(n->field_names[i]);
            }
            free(n->field_types);
            free(n->field_names);
            for (size_t i = 0; i < n->n_methods; i++) {
                ast_free(n->methods[i]);
            }
            if (n->methods) free(n->methods);
            for (size_t i = 0; i < n->n_nested_structs; i++) {
                ast_free(n->nested_structs[i]);
            }
            if (n->nested_structs) free(n->nested_structs);
            if (n->field_visibilities) free(n->field_visibilities);
            if (n->method_visibilities) free(n->method_visibilities);
            break;
        }
        case NODE_BINARY_OP:
            ast_free(((BinaryOpNode*)node)->left);
            free_str(((BinaryOpNode*)node)->operator);
            ast_free(((BinaryOpNode*)node)->right);
            break;
        case NODE_LITERAL: {
            LiteralNode *n = (LiteralNode*)node;
            if (n->type_name && strcmp(n->type_name, "texto") == 0 && n->value.str)
                free_str(n->value.str);
            free_str(n->type_name);
            break;
        }
        case NODE_IDENTIFIER:
            free_str(((IdentifierNode*)node)->name);
            break;
        case NODE_CALL: {
            CallNode *n = (CallNode*)node;
            free_str(n->name);
            ast_free(n->callee);
            for (size_t i = 0; i < n->n_args; i++) ast_free(n->args[i]);
            free(n->args);
            break;
        }
        case NODE_WHILE:
            ast_free(((WhileNode*)node)->condition);
            ast_free(((WhileNode*)node)->body);
            break;
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode*)node;
            free_str(fe->iter_type);
            free_str(fe->iter_name);
            ast_free(fe->collection);
            ast_free(fe->body);
            break;
        }
        case NODE_DO_WHILE:
            ast_free(((DoWhileNode*)node)->condition);
            ast_free(((DoWhileNode*)node)->body);
            break;
        case NODE_END_DO_WHILE:
            ast_free(((EndDoWhileNode*)node)->condition);
            break;
        case NODE_IF:
            ast_free(((IfNode*)node)->condition);
            ast_free(((IfNode*)node)->body);
            ast_free(((IfNode*)node)->else_body);
            break;
        case NODE_RETURN:
            ast_free(((ReturnNode*)node)->expression);
            break;
        case NODE_PRINT:
        case NODE_RESPONDER:
            ast_free(((PrintNode*)node)->expression);
            break;
        case NODE_INPUT:
            free_str(((InputNode*)node)->variable);
            break;
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode*)node;
            ast_free(sn->selector);
            for (size_t i = 0; i < sn->n_cases; i++) {
                for (size_t j = 0; j < sn->cases[i].n_values; j++) {
                    ast_free(sn->cases[i].values[j]);
                }
                free(sn->cases[i].values);
                if (sn->cases[i].is_range && sn->cases[i].range_end) {
                    ast_free(sn->cases[i].range_end);
                }
                ast_free(sn->cases[i].body);
            }
            free(sn->cases);
            ast_free(sn->default_body);
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode*)node;
            ast_free(tn->try_body);
            free_str(tn->catch_var);
            ast_free(tn->catch_body);
            ast_free(tn->final_body);
            break;
        }
        case NODE_THROW:
            ast_free(((ThrowNode*)node)->expression);
            break;
        case NODE_RECORDAR:
            ast_free(((RecordarNode*)node)->key);
            ast_free(((RecordarNode*)node)->value);
            break;
        case NODE_APRENDER:
            ast_free(((AprenderNode*)node)->concept);
            ast_free(((AprenderNode*)node)->weight);
            break;
        case NODE_BUSCAR_PESO:
            ast_free(((BuscarPesoNode*)node)->concept);
            break;
        case NODE_ASOCIAR:
            ast_free(((AsociarNode*)node)->concept1);
            ast_free(((AsociarNode*)node)->concept2);
            ast_free(((AsociarNode*)node)->weight);
            break;
        case NODE_ACTIVAR_MODULO: {
            ActivarModuloNode *am = (ActivarModuloNode *)node;
            ast_free(am->module_path);
            for (size_t i = 0; i < am->n_import_names; i++)
                free(am->import_names[i]);
            free(am->import_names);
            break;
        }
        case NODE_BIBLIOTECA:
            ast_free(((BibliotecaNode*)node)->library_path);
            break;
        case NODE_CREAR_MEMORIA: {
            CrearMemoriaNode *n = (CrearMemoriaNode*)node;
            ast_free(n->filename);
            ast_free(n->nodes_capacity);
            ast_free(n->connections_capacity);
            break;
        }
        case NODE_DEFINE_CONCEPTO:
            ast_free(((DefineConceptoNode*)node)->concepto);
            ast_free(((DefineConceptoNode*)node)->descripcion);
            break;
        case NODE_EXTRAER_TEXTO:
            ast_free(((ExtraerTextoNode*)node)->source);
            ast_free(((ExtraerTextoNode*)node)->pattern);
            free_str(((ExtraerTextoNode*)node)->target);
            break;
        case NODE_CONTIENE_TEXTO:
        case NODE_TERMINA_CON:
            ast_free(((TerminaConNode*)node)->source);
            ast_free(((TerminaConNode*)node)->suffix);
            break;
        case NODE_ULTIMA_PALABRA:
            ast_free(((UltimaPalabraNode*)node)->source);
            free_str(((UltimaPalabraNode*)node)->target);
            break;
        case NODE_COPIAR_TEXTO:
            ast_free(((CopiarTextoNode*)node)->source);
            free_str(((CopiarTextoNode*)node)->target);
            break;
        case NODE_LIST_LITERAL: {
            ListLiteralNode *n = (ListLiteralNode*)node;
            for (size_t i = 0; i < n->n; i++) ast_free(n->elements[i]);
            free(n->elements);
            break;
        }
        case NODE_MAP_LITERAL:
        case NODE_JSON_LITERAL: {
            MapLiteralNode *n = (MapLiteralNode*)node;
            for (size_t i = 0; i < n->n; i++) {
                ast_free(n->keys[i]);
                ast_free(n->values[i]);
            }
            free(n->keys);
            free(n->values);
            break;
        }
        case NODE_INDEX_ACCESS:
            ast_free(((IndexAccessNode*)node)->target);
            ast_free(((IndexAccessNode*)node)->index);
            break;
        case NODE_INDEX_ASSIGNMENT:
            ast_free(((IndexAssignmentNode*)node)->target);
            ast_free(((IndexAssignmentNode*)node)->index);
            ast_free(((IndexAssignmentNode*)node)->expression);
            break;
        case NODE_MEMBER_ACCESS:
            ast_free(((MemberAccessNode*)node)->target);
            free_str(((MemberAccessNode*)node)->member);
            break;
        case NODE_TERNARY:
            ast_free(((TernaryNode*)node)->condition);
            ast_free(((TernaryNode*)node)->true_expr);
            ast_free(((TernaryNode*)node)->false_expr);
            break;
        case NODE_UNARY_OP:
            free_str(((UnaryOpNode*)node)->operator);
            ast_free(((UnaryOpNode*)node)->expression);
            break;
        case NODE_POSTFIX_UPDATE:
            ast_free(((PostfixUpdateNode*)node)->target);
            break;
        case NODE_ASSIGNMENT:
            ast_free(((AssignmentNode*)node)->target);
            ast_free(((AssignmentNode*)node)->expression);
            break;
        case NODE_LAMBDA_DECL: {
            LambdaDeclNode *n = (LambdaDeclNode*)node;
            if (n->params) {
                for (size_t i = 0; i < n->n_params; i++) {
                    if (n->params[i]) free(n->params[i]);
                    if (n->types && n->types[i]) free(n->types[i]);
                }
                free(n->params);
                if (n->types) free(n->types);
            }
            if (n->body) ast_free(n->body);
            break;
        }
        case NODE_EXPORT_DIRECTIVE: {
            ExportDirectiveNode *n = (ExportDirectiveNode*)node;
            for (size_t i = 0; i < n->n_names; i++) {
                free(n->names[i]);
            }
            free(n->names);
            break;
        }
        case NODE_BREAK:
        case NODE_CONTINUE:
        case NODE_CERRAR_MEMORIA:
            break;
        default:
            break;
    }
    free(node);
}

ASTNode *ast_dup(const ASTNode *node) {
    (void)node;
    return NULL;  /* TODO si se necesita */
}
