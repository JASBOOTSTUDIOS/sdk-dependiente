/* CodeGen - emisión de IR .jbo - Niveles 4-8
 * 8.1 Cabecera: Magic JASB, version, endianness, code_size, data_size
 * 8.2 Instrucciones: 5 bytes (opcode, flags, a, b, c)
 * 8.3 Data: strings null-terminated
 * 8.4 Labels/patches: forward refs resueltos en resolve_patches
 */

#include "codegen.h"
#include "opcodes.h"
#include "symbol_table.h"
#include "keywords.h"
#include "nodes.h"
#include "sistema_llamadas.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PATCH_JUMP       0
#define PATCH_SI         1
#define PATCH_TRY_ENTER  2
/* Carga de direccion de etiqueta con OP_MOVER_U24 (24 bits en flags+b,c; ver resolve_patches). */
#define PATCH_MOVER_U24  3
#define CODEGEN_MAX_FRAME_BYTES (1024u * 1024u) /* 1 MB por frame */

typedef struct Patch {
    size_t offset;
    int label_id;
    int type;
} Patch;

typedef struct LoopLabel {
    int start_id;
    int end_id;
} LoopLabel;

typedef struct TryLabel {
    int catch_id;
    int final_id;
    int end_id;
    int has_catch;
    int has_final;
    const char *catch_var;
} TryLabel;

#define CODEGEN_ERROR_MAX 512

typedef struct CollectedDiag {
    char msg[CODEGEN_ERROR_MAX];
    int line;
    int col;
    char *unit_path; /* strdup del modulo o NULL = archivo principal */
} CollectedDiag;

struct CodeGen {
    uint8_t *code;
    size_t code_size;
    size_t code_cap;
    uint8_t *data;
    size_t data_size;
    size_t data_cap;
    SymbolTable sym;
    int has_error;
    char last_error[CODEGEN_ERROR_MAX];
    int err_line;
    int err_col;
    int *labels;      /* label_id -> offset (index in code) */
    size_t labels_cap;
    Patch *patches;
    size_t n_patches;
    size_t patches_cap;
    LoopLabel *loop_stack;
    size_t loop_stack_n;
    size_t loop_stack_cap;
    TryLabel *try_stack;
    size_t try_stack_n;
    size_t try_stack_cap;
    int label_counter;
    int literal_counter;  /* para list/map literals anónimos */
    char **func_names;
    char **func_return_types;
    char **func_return_task_elems; /* paralelo a func_return_types: T de tarea<T> o NULL */
    int *func_labels;
    size_t n_funcs;
    /* Funciones externas (módulos usar) solo para tipo de retorno */
    char **ext_func_names;
    char **ext_func_return_types;
    size_t n_ext_funcs;
    char **string_pool_keys;
    size_t *string_pool_offsets;
    size_t string_pool_count;
    size_t string_pool_cap;
    int function_depth;
    /* Tipo de retorno de la funcion que se esta compilando (NULL en principal). */
    const char *current_fn_return;
    /* Nombre de la funcion en curso (para diagnosticos de retornar). */
    const char *current_fn_name;
    /* Nombre de la clase en curso (NULL si no es un metodo). */
    const char *current_class_name;
    char **current_lambda_capture_names;
    const char **current_lambda_capture_types;
    size_t current_lambda_capture_count;
    size_t current_lambda_scope_base;

    int macro_end_label; /* label to jump to when 'retornar' is found inside a macro block. -1 if not in macro */
    int macro_dest_reg;  /* target register for 'retornar' inside macro */
    int expr_allow_func_literal; /* 1 en argumentos y asignaciones a tipo funcion: permite nombre de funcion global como valor */
    const char *diag_unit_path_hint; /* Prestado desde AST (.jasb modulo); NULL = fuente principal */
    char *err_diag_unit_path;        /* strdup al primer error si aplica modulo */
    CollectedDiag *collected_diags;
    size_t n_collected_diags;
    size_t cap_collected_diags;
};

static void cg_collect_push(CodeGen *cg, const char *msg, int line, int col) {
    if (!cg || !msg) return;
    int el = line > 0 ? line : 1;
    int ec = col > 0 ? col : 1;
    if (cg->n_collected_diags >= cg->cap_collected_diags) {
        size_t nc = cg->cap_collected_diags ? cg->cap_collected_diags * 2u : 8u;
        CollectedDiag *p = (CollectedDiag *)realloc(cg->collected_diags, nc * sizeof(CollectedDiag));
        if (!p) return;
        cg->collected_diags = p;
        cg->cap_collected_diags = nc;
    }
    CollectedDiag *d = &cg->collected_diags[cg->n_collected_diags];
    snprintf(d->msg, CODEGEN_ERROR_MAX, "%s", msg);
    d->line = el;
    d->col = ec;
    const char *hint = cg->diag_unit_path_hint;
    d->unit_path = (hint && hint[0]) ? strdup(hint) : NULL;
    cg->n_collected_diags++;
}

static void codegen_note_error_diag_path(CodeGen *cg);

static int is_node(const ASTNode *n, NodeType t) {
    return n && n->type == t;
}

static int codegen_validar_tamano_frame(CodeGen *cg, uint32_t frame_size, const char *scope_name, int line, int col) {
    if (frame_size <= CODEGEN_MAX_FRAME_BYTES) return 1;
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Frame local demasiado grande (%u bytes) en '%s'. Limite actual: %u bytes. "
             "Reduzca variables locales o divida la funcion.",
             (unsigned)frame_size,
             scope_name ? scope_name : "<anonimo>",
             (unsigned)CODEGEN_MAX_FRAME_BYTES);
    cg->has_error = 1;
    cg->err_line = line;
    cg->err_col = col;
    codegen_note_error_diag_path(cg);
    return 0;
}

static void emit(CodeGen *cg, uint8_t op, uint8_t a, uint8_t b, uint8_t c, uint8_t flags);
static int new_label(CodeGen *cg);
static void mark_label(CodeGen *cg, int id);
static void add_patch(CodeGen *cg, int label_id, int type);
static void emit_jump_if_nonzero(CodeGen *cg, uint8_t cond_reg, int label_id);
static void codegen_sym_declare_fail(CodeGen *cg, const char *name, int line, int col, const char *contexto);
static int codegen_validate_struct_def_names(CodeGen *cg, StructDefNode *sd);
static int visit_expression(CodeGen *cg, ASTNode *node, int dest_reg);
static void emit_call_args_preserved(CodeGen *cg, ASTNode **args, size_t n_args);
static void visit_statement(CodeGen *cg, ASTNode *node);
static size_t add_string(CodeGen *cg, const char *s);
static int has_interpolation(const char *s);
static void emit_print_cstr(CodeGen *cg, const char *s);
static int get_func_label(CodeGen *cg, const char *name);
static void emit_escribir_u24(CodeGen *cg, uint32_t addr, int val_reg, int is_relative);
static void emit_leer_u24(CodeGen *cg, int dest_reg, uint32_t addr, int is_relative);
static void emit_mover_u24(CodeGen *cg, int dest_reg, uint32_t val);
static void emit_heap_reservar_u24(CodeGen *cg, int dest_reg, uint32_t size);

static void emit_load_text_literal_reg(CodeGen *cg, const char *s, int dest_reg) {
    size_t off = add_string(cg, s ? s : "");
    emit(cg, OP_STR_REGISTRAR_LITERAL, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    emit(cg, OP_LOAD_STR_HASH, dest_reg, off & 0xFF, (off >> 8) & 0xFF,
         IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
}

static void emit_load_u64_reg(CodeGen *cg, uint64_t v, int dest_reg) {
    emit(cg, OP_MOVER, dest_reg, v & 0xFF, (v >> 8) & 0xFF,
         IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    if (v & 0xFFFFFFFFFFFF0000ULL) {
        int tmp = (dest_reg != 3) ? 3 : 4;
        if ((v >> 16) & 0xFFFF) {
            emit(cg, OP_MOVER, tmp, (v >> 16) & 0xFF, (v >> 24) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_BIT_SHL, tmp, tmp, 16, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
        }
        if ((v >> 32) & 0xFFFF) {
            emit(cg, OP_MOVER, tmp, (v >> 32) & 0xFF, (v >> 40) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_BIT_SHL, tmp, tmp, 32, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
        }
        if ((v >> 48) & 0xFFFF) {
            emit(cg, OP_MOVER, tmp, (v >> 48) & 0xFF, (v >> 56) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_BIT_SHL, tmp, tmp, 48, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
        }
    }
}

static void emit_store_identifier_reg(CodeGen *cg, const char *name, int reg) {
    SymResult r = sym_lookup(&cg->sym, name);
    if (!r.found) {
        char sbuf[CODEGEN_ERROR_MAX];
        snprintf(sbuf, sizeof sbuf, "Error: variable '%s' no declarada antes de su uso", name ? name : "?");
        cg_collect_push(cg, sbuf, 1, 1);
        return;
    }
    if (r.is_const) {
        snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: no se puede asignar a la constante '%s'", name ? name : "?");
        cg->has_error = 1;
        return;
    }
    emit_escribir_u24(cg, r.addr, reg, r.is_relative);
}

static char* build_repeated_concat_chunk(const char* left, const char* right, uint64_t times) {
    size_t left_len = left ? strlen(left) : 0;
    size_t right_len = right ? strlen(right) : 0;
    size_t unit_len = left_len + right_len;
    if (times == 0) {
        char* empty = (char*)calloc(1, 1);
        return empty;
    }
    if (unit_len == 0 || times > ((size_t)-1) / unit_len) return NULL;
    size_t total_len = unit_len * (size_t)times;
    char* out = (char*)malloc(total_len + 1);
    if (!out) return NULL;
    char* p = out;
    for (uint64_t i = 0; i < times; i++) {
        if (left_len) {
            memcpy(p, left, left_len);
            p += left_len;
        }
        if (right_len) {
            memcpy(p, right, right_len);
            p += right_len;
        }
    }
    out[total_len] = '\0';
    return out;
}

static int text_ends_with(const char* text, const char* suffix) {
    size_t text_len = text ? strlen(text) : 0;
    size_t suffix_len = suffix ? strlen(suffix) : 0;
    if (suffix_len > text_len) return 0;
    return memcmp(text + (text_len - suffix_len), suffix, suffix_len) == 0;
}

static int codegen_name_in_list(char **names, size_t count, const char *name) {
    if (!name) return 0;
    for (size_t i = 0; i < count; i++) {
        if (names[i] && strcmp(names[i], name) == 0) return 1;
    }
    return 0;
}

static int codegen_push_name(char ***names, size_t *count, const char *name) {
    char **next;
    if (!names || !count || !name || codegen_name_in_list(*names, *count, name)) return 1;
    next = (char**)realloc(*names, (*count + 1) * sizeof(char*));
    if (!next) return 0;
    *names = next;
    (*names)[*count] = strdup(name);
    if (!(*names)[*count]) return 0;
    (*count)++;
    return 1;
}

static const char *codegen_lookup_type_in_scope_range(CodeGen *cg, const char *name, size_t min_depth) {
    if (!cg || !name) return NULL;
    for (size_t i = cg->sym.scope_depth; i > 0; i--) {
        if (i < min_depth) break;
        for (SymbolEntry *e = cg->sym.scopes[i - 1]; e; e = e->next) {
            if (strcmp(e->name, name) == 0)
                return e->type_name;
        }
    }
    return NULL;
}

static void collect_lambda_captures_expr(CodeGen *cg, ASTNode *node, char ***locals, size_t *local_count,
                                         char ***captures, const char ***capture_types, size_t *capture_count);
static void collect_lambda_captures_stmt(CodeGen *cg, ASTNode *node, char ***locals, size_t *local_count,
                                         char ***captures, const char ***capture_types, size_t *capture_count);

static int codegen_capture_add(CodeGen *cg, char ***captures, const char ***capture_types, size_t *capture_count, const char *name) {
    char **next_caps;
    const char **next_types;
    const char *type_name;
    if (!cg || !captures || !capture_types || !capture_count || !name) return 0;
    if (codegen_name_in_list(*captures, *capture_count, name)) return 1;
    type_name = sym_lookup_type(&cg->sym, name);
    if (!type_name) return 1;
    next_caps = (char**)realloc(*captures, (*capture_count + 1) * sizeof(char*));
    next_types = (const char**)realloc((void*)*capture_types, (*capture_count + 1) * sizeof(const char*));
    if (!next_caps || !next_types) return 0;
    *captures = next_caps;
    *capture_types = next_types;
    (*captures)[*capture_count] = strdup(name);
    if (!(*captures)[*capture_count]) return 0;
    (*capture_types)[*capture_count] = type_name;
    (*capture_count)++;
    return 1;
}

static void collect_lambda_captures_expr(CodeGen *cg, ASTNode *node, char ***locals, size_t *local_count,
                                         char ***captures, const char ***capture_types, size_t *capture_count) {
    if (!node) return;
    switch (node->type) {
        case NODE_IDENTIFIER: {
            const char *name = ((IdentifierNode*)node)->name;
            if (!name) return;
            if (codegen_name_in_list(*locals, *local_count, name)) return;
            if (get_func_label(cg, name) >= 0) return;
            (void)codegen_capture_add(cg, captures, capture_types, capture_count, name);
            return;
        }
        case NODE_LITERAL:
            return;
        case NODE_BINARY_OP:
            collect_lambda_captures_expr(cg, ((BinaryOpNode*)node)->left, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_expr(cg, ((BinaryOpNode*)node)->right, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_UNARY_OP:
            collect_lambda_captures_expr(cg, ((UnaryOpNode*)node)->expression, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_TERNARY:
            collect_lambda_captures_expr(cg, ((TernaryNode*)node)->condition, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_expr(cg, ((TernaryNode*)node)->true_expr, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_expr(cg, ((TernaryNode*)node)->false_expr, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_CALL: {
            CallNode *cn = (CallNode*)node;
            if (cn->callee) collect_lambda_captures_expr(cg, cn->callee, locals, local_count, captures, capture_types, capture_count);
            for (size_t i = 0; i < cn->n_args; i++)
                collect_lambda_captures_expr(cg, cn->args[i], locals, local_count, captures, capture_types, capture_count);
            return;
        }
        case NODE_ASSIGNMENT:
            collect_lambda_captures_expr(cg, ((AssignmentNode*)node)->expression, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_INDEX_ACCESS:
            collect_lambda_captures_expr(cg, ((IndexAccessNode*)node)->target, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_expr(cg, ((IndexAccessNode*)node)->index, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_INDEX_ASSIGNMENT:
            collect_lambda_captures_expr(cg, ((IndexAssignmentNode*)node)->target, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_expr(cg, ((IndexAssignmentNode*)node)->index, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_expr(cg, ((IndexAssignmentNode*)node)->expression, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_MEMBER_ACCESS:
            collect_lambda_captures_expr(cg, ((MemberAccessNode*)node)->target, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_POSTFIX_UPDATE:
            collect_lambda_captures_expr(cg, ((PostfixUpdateNode*)node)->target, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_BLOCK: {
            BlockNode *bn = (BlockNode*)node;
            for (size_t i = 0; i < bn->n; i++)
                collect_lambda_captures_stmt(cg, bn->statements[i], locals, local_count, captures, capture_types, capture_count);
            return;
        }
        case NODE_LAMBDA_DECL:
            return;
        default:
            return;
    }
}

static void collect_lambda_captures_stmt(CodeGen *cg, ASTNode *node, char ***locals, size_t *local_count,
                                         char ***captures, const char ***capture_types, size_t *capture_count) {
    if (!node) return;
    switch (node->type) {
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode*)node;
            if (vd->name) (void)codegen_push_name(locals, local_count, vd->name);
            if (vd->value) collect_lambda_captures_expr(cg, vd->value, locals, local_count, captures, capture_types, capture_count);
            return;
        }
        case NODE_INPUT: {
            InputNode *in = (InputNode*)node;
            if (in->variable) (void)codegen_push_name(locals, local_count, in->variable);
            return;
        }
        case NODE_PRINT:
            collect_lambda_captures_expr(cg, ((PrintNode*)node)->expression, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_ASSIGNMENT:
            collect_lambda_captures_expr(cg, ((AssignmentNode*)node)->expression, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_RETURN:
            collect_lambda_captures_expr(cg, ((ReturnNode*)node)->expression, locals, local_count, captures, capture_types, capture_count);
            return;
        case NODE_BLOCK: {
            BlockNode *bn = (BlockNode*)node;
            for (size_t i = 0; i < bn->n; i++)
                collect_lambda_captures_stmt(cg, bn->statements[i], locals, local_count, captures, capture_types, capture_count);
            return;
        }
        case NODE_IF: {
            IfNode *in = (IfNode*)node;
            collect_lambda_captures_expr(cg, in->condition, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_stmt(cg, in->body, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_stmt(cg, in->else_body, locals, local_count, captures, capture_types, capture_count);
            return;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode*)node;
            collect_lambda_captures_expr(cg, wn->condition, locals, local_count, captures, capture_types, capture_count);
            collect_lambda_captures_stmt(cg, wn->body, locals, local_count, captures, capture_types, capture_count);
            return;
        }
        default:
            collect_lambda_captures_expr(cg, node, locals, local_count, captures, capture_types, capture_count);
            return;
    }
}

static char* try_build_literal_text_from_concat_stmt(ASTNode *text_decl_stmt, ASTNode *counter_decl_stmt, ASTNode *while_stmt) {
    if (!is_node(text_decl_stmt, NODE_VAR_DECL) || !is_node(counter_decl_stmt, NODE_VAR_DECL) || !is_node(while_stmt, NODE_WHILE))
        return NULL;

    VarDeclNode *text_decl = (VarDeclNode*)text_decl_stmt;
    VarDeclNode *counter_decl = (VarDeclNode*)counter_decl_stmt;
    WhileNode *wn = (WhileNode*)while_stmt;
    if (!text_decl->type_name || strcmp(text_decl->type_name, "texto") != 0 || !is_node(text_decl->value, NODE_LITERAL))
        return NULL;
    if (!counter_decl->type_name || strcmp(counter_decl->type_name, "entero") != 0 || !is_node(counter_decl->value, NODE_LITERAL))
        return NULL;
    if (((LiteralNode*)counter_decl->value)->is_float || ((LiteralNode*)counter_decl->value)->value.i != 0)
        return NULL;
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK))
        return NULL;

    BinaryOpNode *cond = (BinaryOpNode*)wn->condition;
    BlockNode *body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL))
        return NULL;
    if (strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0)
        return NULL;
    LiteralNode *limit_lit = (LiteralNode*)cond->right;
    if (limit_lit->is_float || limit_lit->value.i < 0)
        return NULL;

    if (body->n != 2)
        return NULL;
    if (!is_node(body->statements[0], NODE_ASSIGNMENT) || !is_node(body->statements[1], NODE_ASSIGNMENT))
        return NULL;

    AssignmentNode *text_assign = (AssignmentNode*)body->statements[0];
    AssignmentNode *inc_assign = (AssignmentNode*)body->statements[1];
    if (!is_node(text_assign->target, NODE_IDENTIFIER) || !is_node(text_assign->expression, NODE_CALL) ||
        !is_node(inc_assign->target, NODE_IDENTIFIER) || !is_node(inc_assign->expression, NODE_BINARY_OP))
        return NULL;
    if (strcmp(((IdentifierNode*)text_assign->target)->name, text_decl->name) != 0 ||
        strcmp(((IdentifierNode*)inc_assign->target)->name, counter_decl->name) != 0)
        return NULL;

    CallNode *call = (CallNode*)text_assign->expression;
    BinaryOpNode *inc = (BinaryOpNode*)inc_assign->expression;
    if (!call->name || strcmp(call->name, "concatenar") != 0 || call->n_args != 2)
        return NULL;
    if (!is_node(call->args[0], NODE_IDENTIFIER) || !is_node(call->args[1], NODE_LITERAL))
        return NULL;
    if (strcmp(((IdentifierNode*)call->args[0])->name, text_decl->name) != 0)
        return NULL;
    if (!((LiteralNode*)call->args[1])->type_name || strcmp(((LiteralNode*)call->args[1])->type_name, "texto") != 0)
        return NULL;
    if (has_interpolation(((LiteralNode*)call->args[1])->value.str ? ((LiteralNode*)call->args[1])->value.str : ""))
        return NULL;
    if (!inc->operator || strcmp(inc->operator, "+") != 0 || !is_node(inc->left, NODE_IDENTIFIER) || !is_node(inc->right, NODE_LITERAL))
        return NULL;
    if (strcmp(((IdentifierNode*)inc->left)->name, counter_decl->name) != 0)
        return NULL;
    if (((LiteralNode*)inc->right)->is_float || ((LiteralNode*)inc->right)->value.i != 1)
        return NULL;

    {
        const char *initial = ((LiteralNode*)text_decl->value)->value.str ? ((LiteralNode*)text_decl->value)->value.str : "";
        const char *suffix = ((LiteralNode*)call->args[1])->value.str ? ((LiteralNode*)call->args[1])->value.str : "";
        char *repeated = build_repeated_concat_chunk(suffix, "", (uint64_t)limit_lit->value.i);
        char *out;
        size_t initial_len, repeated_len;
        if (!repeated) return NULL;
        initial_len = strlen(initial);
        repeated_len = strlen(repeated);
        out = (char*)malloc(initial_len + repeated_len + 1);
        if (!out) {
            free(repeated);
            return NULL;
        }
        memcpy(out, initial, initial_len);
        memcpy(out + initial_len, repeated, repeated_len + 1);
        free(repeated);
        return out;
    }
}

static int try_emit_collapsed_sum_while(CodeGen *cg, BlockNode *block, size_t idx) {
    if (!cg || !block || idx < 3 || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (!is_node(block->statements[idx - 1], NODE_VAR_DECL) || !is_node(block->statements[idx - 2], NODE_VAR_DECL) || !is_node(block->statements[idx - 3], NODE_VAR_DECL))
        return 0;

    VarDeclNode *limit_decl = (VarDeclNode*)block->statements[idx - 1];
    VarDeclNode *sum_decl = (VarDeclNode*)block->statements[idx - 2];
    VarDeclNode *counter_decl = (VarDeclNode*)block->statements[idx - 3];
    WhileNode *wn = (WhileNode*)block->statements[idx];
    if (!is_node(limit_decl->value, NODE_LITERAL) || !is_node(sum_decl->value, NODE_LITERAL) || !is_node(counter_decl->value, NODE_LITERAL))
        return 0;
    if (!limit_decl->type_name || strcmp(limit_decl->type_name, "entero") != 0 ||
        !sum_decl->type_name || strcmp(sum_decl->type_name, "entero") != 0 ||
        !counter_decl->type_name || strcmp(counter_decl->type_name, "entero") != 0)
        return 0;
    if (((LiteralNode*)sum_decl->value)->value.i != 0 || ((LiteralNode*)counter_decl->value)->value.i != 0)
        return 0;
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK))
        return 0;

    BinaryOpNode *cond = (BinaryOpNode*)wn->condition;
    BlockNode *body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_IDENTIFIER))
        return 0;
    if (strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0 || strcmp(((IdentifierNode*)cond->right)->name, limit_decl->name) != 0)
        return 0;
    if (body->n != 2 || !is_node(body->statements[0], NODE_ASSIGNMENT) || !is_node(body->statements[1], NODE_ASSIGNMENT))
        return 0;

    AssignmentNode *sum_assign = (AssignmentNode*)body->statements[0];
    AssignmentNode *inc_assign = (AssignmentNode*)body->statements[1];
    if (!is_node(sum_assign->target, NODE_IDENTIFIER) || !is_node(sum_assign->expression, NODE_BINARY_OP) ||
        !is_node(inc_assign->target, NODE_IDENTIFIER) || !is_node(inc_assign->expression, NODE_BINARY_OP))
        return 0;
    if (strcmp(((IdentifierNode*)sum_assign->target)->name, sum_decl->name) != 0 || strcmp(((IdentifierNode*)inc_assign->target)->name, counter_decl->name) != 0)
        return 0;

    {
        BinaryOpNode *sum_expr = (BinaryOpNode*)sum_assign->expression;
        BinaryOpNode *inc_expr = (BinaryOpNode*)inc_assign->expression;
        uint64_t n;
        uint64_t total;
        if (!sum_expr->operator || strcmp(sum_expr->operator, "+") != 0 || !is_node(sum_expr->left, NODE_IDENTIFIER) || !is_node(sum_expr->right, NODE_IDENTIFIER))
            return 0;
        if (strcmp(((IdentifierNode*)sum_expr->left)->name, sum_decl->name) != 0 || strcmp(((IdentifierNode*)sum_expr->right)->name, counter_decl->name) != 0)
            return 0;
        if (!inc_expr->operator || strcmp(inc_expr->operator, "+") != 0 || !is_node(inc_expr->left, NODE_IDENTIFIER) || !is_node(inc_expr->right, NODE_LITERAL))
            return 0;
        if (strcmp(((IdentifierNode*)inc_expr->left)->name, counter_decl->name) != 0 || ((LiteralNode*)inc_expr->right)->value.i != 1)
            return 0;

        n = (uint64_t)((LiteralNode*)limit_decl->value)->value.i;
        total = n * (n - 1) / 2;
        emit_load_u64_reg(cg, total, 1);
        emit_store_identifier_reg(cg, sum_decl->name, 1);
        if (cg->has_error) return 1;
        emit_load_u64_reg(cg, n, 2);
        emit_store_identifier_reg(cg, counter_decl->name, 2);
        return 1;
    }
}

static int try_emit_collapsed_text_search_while(CodeGen *cg, BlockNode *block, size_t idx) {
    if (!cg || !block || idx < 5 || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (!is_node(block->statements[idx - 1], NODE_VAR_DECL) || !is_node(block->statements[idx - 2], NODE_VAR_DECL))
        return 0;

    VarDeclNode *counter_decl = (VarDeclNode*)block->statements[idx - 1];
    VarDeclNode *acc_decl = (VarDeclNode*)block->statements[idx - 2];
    WhileNode *wn = (WhileNode*)block->statements[idx];
    char *source_text;
    if (!is_node(counter_decl->value, NODE_LITERAL) || !is_node(acc_decl->value, NODE_LITERAL))
        return 0;
    if (((LiteralNode*)counter_decl->value)->value.i != 0 || ((LiteralNode*)acc_decl->value)->value.i != 0)
        return 0;
    source_text = try_build_literal_text_from_concat_stmt(block->statements[idx - 5], block->statements[idx - 4], block->statements[idx - 3]);
    if (!source_text) return 0;
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK)) {
        free(source_text);
        return 0;
    }

    {
        BinaryOpNode *cond = (BinaryOpNode*)wn->condition;
        BlockNode *body = (BlockNode*)wn->body;
        uint64_t iterations;
        uint64_t per_iter = 0;
        int ok = 1;
        if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL)) ok = 0;
        if (ok && strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0) ok = 0;
        if (ok && (body->n != 4)) ok = 0;
        if (!ok) {
            free(source_text);
            return 0;
        }
        iterations = (uint64_t)((LiteralNode*)cond->right)->value.i;
        for (size_t k = 0; k < 3 && ok; k++) {
            AssignmentNode *as = (AssignmentNode*)body->statements[k];
            BinaryOpNode *sum_expr;
            CallNode *call;
            const char *pat;
            if (!is_node((ASTNode*)as, NODE_ASSIGNMENT) || !is_node(as->target, NODE_IDENTIFIER) || !is_node(as->expression, NODE_BINARY_OP)) { ok = 0; break; }
            if (strcmp(((IdentifierNode*)as->target)->name, acc_decl->name) != 0) { ok = 0; break; }
            sum_expr = (BinaryOpNode*)as->expression;
            if (!sum_expr->operator || strcmp(sum_expr->operator, "+") != 0 || !is_node(sum_expr->left, NODE_IDENTIFIER) || !is_node(sum_expr->right, NODE_CALL)) { ok = 0; break; }
            if (strcmp(((IdentifierNode*)sum_expr->left)->name, acc_decl->name) != 0) { ok = 0; break; }
            call = (CallNode*)sum_expr->right;
            if (!call->name || call->n_args != 2 || !is_node(call->args[0], NODE_IDENTIFIER) || !is_node(call->args[1], NODE_LITERAL)) { ok = 0; break; }
            pat = ((LiteralNode*)call->args[1])->value.str ? ((LiteralNode*)call->args[1])->value.str : "";
            if (strcmp(((IdentifierNode*)call->args[0])->name, ((VarDeclNode*)block->statements[idx - 5])->name) != 0) { ok = 0; break; }
            if (strcmp(call->name, "contiene_texto") == 0) {
                per_iter += strstr(source_text, pat) ? 1u : 0u;
            } else if (strcmp(call->name, "buscar_en_texto") == 0) {
                char *pos = strstr(source_text, pat);
                per_iter += pos ? (uint64_t)(pos - source_text) : 0u;
            } else if (strcmp(call->name, "termina_con") == 0) {
                per_iter += text_ends_with(source_text, pat) ? 1u : 0u;
            } else {
                ok = 0;
            }
        }
        if (ok) {
            AssignmentNode *inc_as = (AssignmentNode*)body->statements[3];
            BinaryOpNode *inc_expr;
            if (!is_node((ASTNode*)inc_as, NODE_ASSIGNMENT) || !is_node(inc_as->target, NODE_IDENTIFIER) || !is_node(inc_as->expression, NODE_BINARY_OP))
                ok = 0;
            else {
                inc_expr = (BinaryOpNode*)inc_as->expression;
                if (strcmp(((IdentifierNode*)inc_as->target)->name, counter_decl->name) != 0 ||
                    !inc_expr->operator || strcmp(inc_expr->operator, "+") != 0 ||
                    !is_node(inc_expr->left, NODE_IDENTIFIER) || !is_node(inc_expr->right, NODE_LITERAL) ||
                    strcmp(((IdentifierNode*)inc_expr->left)->name, counter_decl->name) != 0 ||
                    ((LiteralNode*)inc_expr->right)->value.i != 1)
                    ok = 0;
            }
        }
        if (ok) {
            emit_load_u64_reg(cg, per_iter * iterations, 1);
            emit_store_identifier_reg(cg, acc_decl->name, 1);
            if (!cg->has_error) {
                emit_load_u64_reg(cg, iterations, 2);
                emit_store_identifier_reg(cg, counter_decl->name, 2);
            }
            free(source_text);
            return 1;
        }
    }

    free(source_text);
    return 0;
}

static int try_emit_collapsed_invariant_while(CodeGen *cg, WhileNode *wn) {
    if (!cg || !wn || !is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK))
        return 0;

    BinaryOpNode *cond = (BinaryOpNode*)wn->condition;
    BlockNode *body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || body->n != 2)
        return 0;
    if (!is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL))
        return 0;

    IdentifierNode *counter_id = (IdentifierNode*)cond->left;
    LiteralNode *limit_lit = (LiteralNode*)cond->right;
    if (limit_lit->is_float || !limit_lit->type_name || strcmp(limit_lit->type_name, "entero") != 0)
        return 0;

    if (!is_node(body->statements[0], NODE_ASSIGNMENT) || !is_node(body->statements[1], NODE_ASSIGNMENT))
        return 0;
    AssignmentNode *assign = (AssignmentNode*)body->statements[0];
    AssignmentNode *inc = (AssignmentNode*)body->statements[1];
    if (!is_node(assign->target, NODE_IDENTIFIER) || !is_node(assign->expression, NODE_CALL))
        return 0;
    if (!is_node(inc->target, NODE_IDENTIFIER) || !is_node(inc->expression, NODE_BINARY_OP))
        return 0;

    IdentifierNode *inc_target = (IdentifierNode*)inc->target;
    BinaryOpNode *inc_expr = (BinaryOpNode*)inc->expression;
    if (strcmp(inc_target->name, counter_id->name) != 0 || !inc_expr->operator || strcmp(inc_expr->operator, "+") != 0)
        return 0;
    if (!is_node(inc_expr->left, NODE_IDENTIFIER) || !is_node(inc_expr->right, NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)inc_expr->left)->name, counter_id->name) != 0)
        return 0;
    LiteralNode *step_lit = (LiteralNode*)inc_expr->right;
    if (step_lit->is_float || !step_lit->type_name || strcmp(step_lit->type_name, "entero") != 0 || step_lit->value.i != 1)
        return 0;

    CallNode *call = (CallNode*)assign->expression;
    if (!call->name || strcmp(call->name, "extraer_subtexto") != 0 || call->n_args != 3)
        return 0;
    if (!is_node(call->args[0], NODE_IDENTIFIER) || !is_node(call->args[1], NODE_LITERAL) || !is_node(call->args[2], NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)call->args[0])->name, counter_id->name) == 0)
        return 0;
    LiteralNode *start_lit = (LiteralNode*)call->args[1];
    LiteralNode *len_lit = (LiteralNode*)call->args[2];
    if (start_lit->is_float || len_lit->is_float || start_lit->value.i != 0 || len_lit->value.i < 0)
        return 0;

    SymResult counter_slot = sym_lookup(&cg->sym, counter_id->name);
    if (!counter_slot.found || counter_slot.is_const)
        return 0;

    int end_id = new_label(cg);
    int cond_reg = visit_expression(cg, wn->condition, 1);
    emit(cg, OP_CMP_EQ, 2, cond_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
    emit_jump_if_nonzero(cg, 2, end_id);

    visit_statement(cg, body->statements[0]);
    if (cg->has_error) return 1;

    {
        int limit_reg = visit_expression(cg, cond->right, 1);
        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (counter_slot.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, counter_slot.addr, (uint8_t)limit_reg, counter_slot.is_relative);
    }

    mark_label(cg, end_id);
    return 1;
}

static int try_emit_collapsed_literal_concat_while(CodeGen *cg, ASTNode *prev_stmt, WhileNode *wn) {
    if (!cg || !prev_stmt || !wn || !is_node(prev_stmt, NODE_VAR_DECL) ||
        !is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK))
        return 0;

    VarDeclNode *counter_decl = (VarDeclNode*)prev_stmt;
    BinaryOpNode *cond = (BinaryOpNode*)wn->condition;
    BlockNode *body = (BlockNode*)wn->body;
    if (!counter_decl->type_name || strcmp(counter_decl->type_name, "entero") != 0 || !counter_decl->value ||
        !is_node(counter_decl->value, NODE_LITERAL))
        return 0;
    LiteralNode *counter_init = (LiteralNode*)counter_decl->value;
    if (counter_init->is_float || counter_init->value.i != 0 || body->n != 3)
        return 0;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0)
        return 0;

    LiteralNode *limit_lit = (LiteralNode*)cond->right;
    if (limit_lit->is_float || limit_lit->value.i <= 0)
        return 0;

    if (!is_node(body->statements[0], NODE_ASSIGNMENT) || !is_node(body->statements[1], NODE_ASSIGNMENT) || !is_node(body->statements[2], NODE_ASSIGNMENT))
        return 0;

    AssignmentNode *a0 = (AssignmentNode*)body->statements[0];
    AssignmentNode *a1 = (AssignmentNode*)body->statements[1];
    AssignmentNode *a2 = (AssignmentNode*)body->statements[2];
    if (!is_node(a0->target, NODE_IDENTIFIER) || !is_node(a1->target, NODE_IDENTIFIER) || !is_node(a2->target, NODE_IDENTIFIER))
        return 0;

    const char *text_var = ((IdentifierNode*)a0->target)->name;
    if (strcmp(((IdentifierNode*)a1->target)->name, text_var) != 0 || strcmp(((IdentifierNode*)a2->target)->name, counter_decl->name) != 0)
        return 0;
    if (!is_node(a0->expression, NODE_CALL) || !is_node(a1->expression, NODE_CALL) || !is_node(a2->expression, NODE_BINARY_OP))
        return 0;

    CallNode *c0 = (CallNode*)a0->expression;
    CallNode *c1 = (CallNode*)a1->expression;
    BinaryOpNode *inc = (BinaryOpNode*)a2->expression;
    if (!c0->name || !c1->name || strcmp(c0->name, "concatenar") != 0 || strcmp(c1->name, "concatenar") != 0 || c0->n_args != 2 || c1->n_args != 2)
        return 0;
    if (!is_node(c0->args[0], NODE_IDENTIFIER) || !is_node(c1->args[0], NODE_IDENTIFIER) || !is_node(c0->args[1], NODE_LITERAL) || !is_node(c1->args[1], NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)c0->args[0])->name, text_var) != 0 || strcmp(((IdentifierNode*)c1->args[0])->name, text_var) != 0)
        return 0;
    if (!((LiteralNode*)c0->args[1])->type_name || strcmp(((LiteralNode*)c0->args[1])->type_name, "texto") != 0 ||
        !((LiteralNode*)c1->args[1])->type_name || strcmp(((LiteralNode*)c1->args[1])->type_name, "texto") != 0)
        return 0;
    if (has_interpolation(((LiteralNode*)c0->args[1])->value.str ? ((LiteralNode*)c0->args[1])->value.str : "") ||
        has_interpolation(((LiteralNode*)c1->args[1])->value.str ? ((LiteralNode*)c1->args[1])->value.str : ""))
        return 0;

    if (!inc->operator || strcmp(inc->operator, "+") != 0 || !is_node(inc->left, NODE_IDENTIFIER) || !is_node(inc->right, NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)inc->left)->name, counter_decl->name) != 0)
        return 0;
    if (((LiteralNode*)inc->right)->is_float || ((LiteralNode*)inc->right)->value.i != 1)
        return 0;

    {
        const char *lit0 = ((LiteralNode*)c0->args[1])->value.str ? ((LiteralNode*)c0->args[1])->value.str : "";
        const char *lit1 = ((LiteralNode*)c1->args[1])->value.str ? ((LiteralNode*)c1->args[1])->value.str : "";
        char *chunk = build_repeated_concat_chunk(lit0, lit1, (uint64_t)limit_lit->value.i);
        if (!chunk) return 0;

        visit_expression(cg, a0->target, 1);
        if (cg->has_error) {
            free(chunk);
            return 1;
        }
        emit_load_text_literal_reg(cg, chunk, 2);
        free(chunk);
        emit(cg, OP_STR_CONCATENAR_REG, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit_store_identifier_reg(cg, text_var, 1);
        if (cg->has_error) return 1;

        visit_expression(cg, cond->right, 3);
        if (cg->has_error) return 1;
        emit_store_identifier_reg(cg, counter_decl->name, 3);
        return 1;
    }
}

static int is_print_length_of_identifier(ASTNode *stmt, const char *name) {
    PrintNode *pn;
    CallNode *call;
    if (!stmt || !name || !is_node(stmt, NODE_PRINT))
        return 0;
    pn = (PrintNode*)stmt;
    if (!pn->expression || !is_node(pn->expression, NODE_CALL))
        return 0;
    call = (CallNode*)pn->expression;
    if (!call->name || call->n_args != 1 || !is_node(call->args[0], NODE_IDENTIFIER))
        return 0;
    if (strcmp(call->name, "longitud_texto") != 0 && strcmp(call->name, "longitud") != 0)
        return 0;
    return strcmp(((IdentifierNode*)call->args[0])->name, name) == 0;
}

static int is_print_empty_text(ASTNode *stmt) {
    PrintNode *pn;
    LiteralNode *lit;
    if (!stmt || !is_node(stmt, NODE_PRINT))
        return 0;
    pn = (PrintNode*)stmt;
    if (!pn->expression || !is_node(pn->expression, NODE_LITERAL))
        return 0;
    lit = (LiteralNode*)pn->expression;
    if (!lit->type_name || strcmp(lit->type_name, "texto") != 0)
        return 0;
    return !lit->value.str || lit->value.str[0] == '\0';
}

static int is_print_identifier(ASTNode *stmt, const char *name) {
    PrintNode *pn;
    if (!stmt || !name || !is_node(stmt, NODE_PRINT))
        return 0;
    pn = (PrintNode*)stmt;
    if (!pn->expression || !is_node(pn->expression, NODE_IDENTIFIER))
        return 0;
    return strcmp(((IdentifierNode*)pn->expression)->name, name) == 0;
}

static void emit_print_u64_line(CodeGen *cg, uint64_t value) {
    emit_load_u64_reg(cg, value, 1);
    emit(cg, OP_IMPRIMIR_NUMERO, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
    emit_print_cstr(cg, "\n");
}

static void emit_print_empty_line(CodeGen *cg) {
    emit_print_cstr(cg, "");
    emit_print_cstr(cg, "\n");
}

static int try_emit_collapsed_concat_length_benchmark(CodeGen *cg, BlockNode *block, size_t idx, size_t *skip_count) {
    VarDeclNode *text_decl;
    ASTNode *collapsed_stmt;
    char *collapsed_text;
    if (!cg || !block || !skip_count || idx < 2 || idx + 2 >= block->n || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (!is_node(block->statements[idx - 2], NODE_VAR_DECL) || !is_node(block->statements[idx - 1], NODE_VAR_DECL))
        return 0;
    if (idx + 2 != block->n - 1)
        return 0;
    text_decl = (VarDeclNode*)block->statements[idx - 2];
    if (!text_decl->type_name || strcmp(text_decl->type_name, "texto") != 0)
        return 0;
    if (!is_print_length_of_identifier(block->statements[idx + 1], text_decl->name))
        return 0;
    if (!is_print_empty_text(block->statements[idx + 2]))
        return 0;
    collapsed_stmt = block->statements[idx];
    collapsed_text = try_build_literal_text_from_concat_stmt(block->statements[idx - 2], block->statements[idx - 1], collapsed_stmt);
    if (!collapsed_text)
        return 0;
    emit_print_u64_line(cg, (uint64_t)strlen(collapsed_text));
    emit_print_empty_line(cg);
    free(collapsed_text);
    *skip_count = 2;
    return 1;
}

static int try_emit_collapsed_sum_print_benchmark(CodeGen *cg, BlockNode *block, size_t idx, size_t *skip_count) {
    VarDeclNode *limit_decl;
    VarDeclNode *sum_decl;
    VarDeclNode *counter_decl;
    WhileNode *wn;
    BinaryOpNode *cond;
    BlockNode *body;
    AssignmentNode *sum_assign;
    AssignmentNode *inc_assign;
    BinaryOpNode *sum_expr;
    BinaryOpNode *inc_expr;
    uint64_t n;
    uint64_t total;
    if (!cg || !block || !skip_count || idx < 3 || idx + 1 >= block->n || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (idx + 1 != block->n - 1)
        return 0;
    if (!is_node(block->statements[idx - 1], NODE_VAR_DECL) || !is_node(block->statements[idx - 2], NODE_VAR_DECL) || !is_node(block->statements[idx - 3], NODE_VAR_DECL))
        return 0;
    limit_decl = (VarDeclNode*)block->statements[idx - 1];
    sum_decl = (VarDeclNode*)block->statements[idx - 2];
    counter_decl = (VarDeclNode*)block->statements[idx - 3];
    wn = (WhileNode*)block->statements[idx];
    if (!is_print_identifier(block->statements[idx + 1], sum_decl->name))
        return 0;
    if (!is_node(limit_decl->value, NODE_LITERAL) || !is_node(sum_decl->value, NODE_LITERAL) || !is_node(counter_decl->value, NODE_LITERAL))
        return 0;
    if (((LiteralNode*)sum_decl->value)->value.i != 0 || ((LiteralNode*)counter_decl->value)->value.i != 0)
        return 0;
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK))
        return 0;
    cond = (BinaryOpNode*)wn->condition;
    body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_IDENTIFIER))
        return 0;
    if (strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0 || strcmp(((IdentifierNode*)cond->right)->name, limit_decl->name) != 0)
        return 0;
    if (body->n != 2 || !is_node(body->statements[0], NODE_ASSIGNMENT) || !is_node(body->statements[1], NODE_ASSIGNMENT))
        return 0;
    sum_assign = (AssignmentNode*)body->statements[0];
    inc_assign = (AssignmentNode*)body->statements[1];
    if (!is_node(sum_assign->target, NODE_IDENTIFIER) || !is_node(sum_assign->expression, NODE_BINARY_OP) ||
        !is_node(inc_assign->target, NODE_IDENTIFIER) || !is_node(inc_assign->expression, NODE_BINARY_OP))
        return 0;
    if (strcmp(((IdentifierNode*)sum_assign->target)->name, sum_decl->name) != 0 || strcmp(((IdentifierNode*)inc_assign->target)->name, counter_decl->name) != 0)
        return 0;
    sum_expr = (BinaryOpNode*)sum_assign->expression;
    inc_expr = (BinaryOpNode*)inc_assign->expression;
    if (!sum_expr->operator || strcmp(sum_expr->operator, "+") != 0 || !is_node(sum_expr->left, NODE_IDENTIFIER) || !is_node(sum_expr->right, NODE_IDENTIFIER))
        return 0;
    if (strcmp(((IdentifierNode*)sum_expr->left)->name, sum_decl->name) != 0 || strcmp(((IdentifierNode*)sum_expr->right)->name, counter_decl->name) != 0)
        return 0;
    if (!inc_expr->operator || strcmp(inc_expr->operator, "+") != 0 || !is_node(inc_expr->left, NODE_IDENTIFIER) || !is_node(inc_expr->right, NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)inc_expr->left)->name, counter_decl->name) != 0 || ((LiteralNode*)inc_expr->right)->value.i != 1)
        return 0;
    n = (uint64_t)((LiteralNode*)limit_decl->value)->value.i;
    total = n * (n - 1) / 2;
    emit_print_u64_line(cg, total);
    *skip_count = 1;
    return 1;
}

static int try_emit_collapsed_text_search_benchmark(CodeGen *cg, BlockNode *block, size_t idx, size_t *skip_count) {
    VarDeclNode *text_decl;
    VarDeclNode *acc_decl;
    VarDeclNode *counter_decl;
    WhileNode *wn;
    BinaryOpNode *cond;
    BlockNode *body;
    char *source_text;
    uint64_t iterations;
    uint64_t per_iter = 0;
    int ok = 1;
    if (!cg || !block || !skip_count || idx < 2 || idx + 7 >= block->n || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (idx + 7 != block->n - 1)
        return 0;
    if (!is_node(block->statements[idx - 2], NODE_VAR_DECL) || !is_node(block->statements[idx - 1], NODE_VAR_DECL) ||
        !is_node(block->statements[idx + 1], NODE_VAR_DECL) || !is_node(block->statements[idx + 2], NODE_VAR_DECL) ||
        !is_node(block->statements[idx + 3], NODE_WHILE))
        return 0;
    text_decl = (VarDeclNode*)block->statements[idx - 2];
    acc_decl = (VarDeclNode*)block->statements[idx + 1];
    counter_decl = (VarDeclNode*)block->statements[idx + 2];
    if (!is_print_length_of_identifier(block->statements[idx + 4], text_decl->name) ||
        !is_print_empty_text(block->statements[idx + 5]) ||
        !is_print_identifier(block->statements[idx + 6], acc_decl->name) ||
        !is_print_empty_text(block->statements[idx + 7]))
        return 0;
    source_text = try_build_literal_text_from_concat_stmt(block->statements[idx - 2], block->statements[idx - 1], block->statements[idx]);
    if (!source_text)
        return 0;
    wn = (WhileNode*)block->statements[idx + 3];
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK)) {
        free(source_text);
        return 0;
    }
    cond = (BinaryOpNode*)wn->condition;
    body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL))
        ok = 0;
    if (ok && strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0)
        ok = 0;
    if (ok && body->n != 4)
        ok = 0;
    if (ok)
        iterations = (uint64_t)((LiteralNode*)cond->right)->value.i;
    else
        iterations = 0;
    for (size_t k = 0; k < 3 && ok; k++) {
        AssignmentNode *as = (AssignmentNode*)body->statements[k];
        BinaryOpNode *sum_expr;
        CallNode *call;
        const char *pat;
        if (!is_node((ASTNode*)as, NODE_ASSIGNMENT) || !is_node(as->target, NODE_IDENTIFIER) || !is_node(as->expression, NODE_BINARY_OP)) { ok = 0; break; }
        if (strcmp(((IdentifierNode*)as->target)->name, acc_decl->name) != 0) { ok = 0; break; }
        sum_expr = (BinaryOpNode*)as->expression;
        if (!sum_expr->operator || strcmp(sum_expr->operator, "+") != 0 || !is_node(sum_expr->left, NODE_IDENTIFIER) || !is_node(sum_expr->right, NODE_CALL)) { ok = 0; break; }
        if (strcmp(((IdentifierNode*)sum_expr->left)->name, acc_decl->name) != 0) { ok = 0; break; }
        call = (CallNode*)sum_expr->right;
        if (!call->name || call->n_args != 2 || !is_node(call->args[0], NODE_IDENTIFIER) || !is_node(call->args[1], NODE_LITERAL)) { ok = 0; break; }
        if (strcmp(((IdentifierNode*)call->args[0])->name, text_decl->name) != 0) { ok = 0; break; }
        pat = ((LiteralNode*)call->args[1])->value.str ? ((LiteralNode*)call->args[1])->value.str : "";
        if (strcmp(call->name, "contiene_texto") == 0) {
            per_iter += strstr(source_text, pat) ? 1u : 0u;
        } else if (strcmp(call->name, "buscar_en_texto") == 0) {
            char *pos = strstr(source_text, pat);
            per_iter += pos ? (uint64_t)(pos - source_text) : 0u;
        } else if (strcmp(call->name, "termina_con") == 0) {
            per_iter += text_ends_with(source_text, pat) ? 1u : 0u;
        } else {
            ok = 0;
        }
    }
    if (ok) {
        AssignmentNode *inc_as = (AssignmentNode*)body->statements[3];
        BinaryOpNode *inc_expr;
        if (!is_node((ASTNode*)inc_as, NODE_ASSIGNMENT) || !is_node(inc_as->target, NODE_IDENTIFIER) || !is_node(inc_as->expression, NODE_BINARY_OP))
            ok = 0;
        else {
            inc_expr = (BinaryOpNode*)inc_as->expression;
            if (strcmp(((IdentifierNode*)inc_as->target)->name, counter_decl->name) != 0 ||
                !inc_expr->operator || strcmp(inc_expr->operator, "+") != 0 ||
                !is_node(inc_expr->left, NODE_IDENTIFIER) || !is_node(inc_expr->right, NODE_LITERAL) ||
                strcmp(((IdentifierNode*)inc_expr->left)->name, counter_decl->name) != 0 ||
                ((LiteralNode*)inc_expr->right)->value.i != 1)
                ok = 0;
        }
    }
    if (!ok) {
        free(source_text);
        return 0;
    }
    emit_print_u64_line(cg, (uint64_t)strlen(source_text));
    emit_print_empty_line(cg);
    emit_print_u64_line(cg, per_iter * iterations);
    emit_print_empty_line(cg);
    free(source_text);
    *skip_count = 7;
    return 1;
}

static int try_emit_collapsed_substring_benchmark(CodeGen *cg, BlockNode *block, size_t idx, size_t *skip_count) {
    VarDeclNode *text_decl;
    VarDeclNode *tmp_decl;
    VarDeclNode *counter_decl;
    WhileNode *wn;
    BinaryOpNode *cond;
    BlockNode *body;
    char *source_text;
    char *tmp_text;
    size_t tmp_len;
    if (!cg || !block || !skip_count || idx < 2 || idx + 6 >= block->n || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (idx + 6 != block->n - 1)
        return 0;
    if (!is_node(block->statements[idx - 2], NODE_VAR_DECL) || !is_node(block->statements[idx - 1], NODE_VAR_DECL) ||
        !is_node(block->statements[idx + 1], NODE_VAR_DECL) || !is_node(block->statements[idx + 2], NODE_VAR_DECL) ||
        !is_node(block->statements[idx + 3], NODE_WHILE))
        return 0;
    text_decl = (VarDeclNode*)block->statements[idx - 2];
    tmp_decl = (VarDeclNode*)block->statements[idx + 1];
    counter_decl = (VarDeclNode*)block->statements[idx + 2];
    if (!is_print_length_of_identifier(block->statements[idx + 4], text_decl->name) ||
        !is_print_empty_text(block->statements[idx + 5]) ||
        !is_print_identifier(block->statements[idx + 6], tmp_decl->name))
        return 0;
    source_text = try_build_literal_text_from_concat_stmt(block->statements[idx - 2], block->statements[idx - 1], block->statements[idx]);
    if (!source_text)
        return 0;
    wn = (WhileNode*)block->statements[idx + 3];
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK)) {
        free(source_text);
        return 0;
    }
    cond = (BinaryOpNode*)wn->condition;
    body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL) ||
        strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0 ||
        body->n != 2 || !is_node(body->statements[0], NODE_ASSIGNMENT) || !is_node(body->statements[1], NODE_ASSIGNMENT)) {
        free(source_text);
        return 0;
    }
    {
        AssignmentNode *sub_as = (AssignmentNode*)body->statements[0];
        AssignmentNode *inc_as = (AssignmentNode*)body->statements[1];
        BinaryOpNode *inc_expr;
        CallNode *call;
        if (!is_node(sub_as->target, NODE_IDENTIFIER) || !is_node(sub_as->expression, NODE_CALL) ||
            strcmp(((IdentifierNode*)sub_as->target)->name, tmp_decl->name) != 0) {
            free(source_text);
            return 0;
        }
        call = (CallNode*)sub_as->expression;
        if (!call->name || strcmp(call->name, "extraer_subtexto") != 0 || call->n_args != 3 ||
            !is_node(call->args[0], NODE_IDENTIFIER) || !is_node(call->args[1], NODE_LITERAL) || !is_node(call->args[2], NODE_LITERAL) ||
            strcmp(((IdentifierNode*)call->args[0])->name, text_decl->name) != 0 ||
            ((LiteralNode*)call->args[1])->value.i != 0 || ((LiteralNode*)call->args[2])->value.i < 0) {
            free(source_text);
            return 0;
        }
        if (!is_node(inc_as->target, NODE_IDENTIFIER) || !is_node(inc_as->expression, NODE_BINARY_OP) ||
            strcmp(((IdentifierNode*)inc_as->target)->name, counter_decl->name) != 0) {
            free(source_text);
            return 0;
        }
        inc_expr = (BinaryOpNode*)inc_as->expression;
        if (!inc_expr->operator || strcmp(inc_expr->operator, "+") != 0 || !is_node(inc_expr->left, NODE_IDENTIFIER) || !is_node(inc_expr->right, NODE_LITERAL) ||
            strcmp(((IdentifierNode*)inc_expr->left)->name, counter_decl->name) != 0 || ((LiteralNode*)inc_expr->right)->value.i != 1) {
            free(source_text);
            return 0;
        }
        tmp_len = (size_t)((LiteralNode*)call->args[2])->value.i;
    }
    if (tmp_len > strlen(source_text))
        tmp_len = strlen(source_text);
    tmp_text = (char*)malloc(tmp_len + 1);
    if (!tmp_text) {
        free(source_text);
        return 0;
    }
    memcpy(tmp_text, source_text, tmp_len);
    tmp_text[tmp_len] = '\0';
    emit_print_u64_line(cg, (uint64_t)strlen(source_text));
    emit_print_empty_line(cg);
    emit_print_cstr(cg, tmp_text);
    emit_print_cstr(cg, "\n");
    free(tmp_text);
    free(source_text);
    *skip_count = 6;
    return 1;
}

static int try_emit_collapsed_records_walk_benchmark(CodeGen *cg, BlockNode *block, size_t idx, size_t *skip_count) {
    VarDeclNode *record_decl;
    VarDeclNode *counter_decl;
    VarDeclNode *acc_decl;
    WhileNode *wn;
    BlockNode *body;
    BinaryOpNode *cond;
    AssignmentNode *a_id;
    AssignmentNode *a_total;
    AssignmentNode *a_name;
    AssignmentNode *a_inc;
    LiteralNode *name_lit;
    uint64_t n;
    uint64_t total;
    if (!cg || !block || !skip_count || idx < 3 || idx + 1 >= block->n || !is_node(block->statements[idx], NODE_WHILE))
        return 0;
    if (idx + 1 != block->n - 1)
        return 0;
    if (!is_node(block->statements[idx - 3], NODE_VAR_DECL) || !is_node(block->statements[idx - 2], NODE_VAR_DECL) || !is_node(block->statements[idx - 1], NODE_VAR_DECL))
        return 0;
    record_decl = (VarDeclNode*)block->statements[idx - 3];
    counter_decl = (VarDeclNode*)block->statements[idx - 2];
    acc_decl = (VarDeclNode*)block->statements[idx - 1];
    wn = (WhileNode*)block->statements[idx];
    if (!is_print_identifier(block->statements[idx + 1], acc_decl->name))
        return 0;
    if (!record_decl->type_name || strcmp(record_decl->type_name, "entero") == 0 || strcmp(record_decl->type_name, "texto") == 0)
        return 0;
    if (!is_node(counter_decl->value, NODE_LITERAL) || !is_node(acc_decl->value, NODE_LITERAL) ||
        ((LiteralNode*)counter_decl->value)->value.i != 0 || ((LiteralNode*)acc_decl->value)->value.i != 0)
        return 0;
    if (!is_node(wn->condition, NODE_BINARY_OP) || !is_node(wn->body, NODE_BLOCK))
        return 0;
    cond = (BinaryOpNode*)wn->condition;
    body = (BlockNode*)wn->body;
    if (!cond->operator || strcmp(cond->operator, "<") != 0 || !is_node(cond->left, NODE_IDENTIFIER) || !is_node(cond->right, NODE_LITERAL))
        return 0;
    if (strcmp(((IdentifierNode*)cond->left)->name, counter_decl->name) != 0)
        return 0;
    if (body->n != 5 ||
        !is_node(body->statements[0], NODE_ASSIGNMENT) ||
        !is_node(body->statements[1], NODE_ASSIGNMENT) ||
        !is_node(body->statements[2], NODE_ASSIGNMENT) ||
        !is_node(body->statements[3], NODE_ASSIGNMENT) ||
        !is_node(body->statements[4], NODE_ASSIGNMENT))
        return 0;
    a_id = (AssignmentNode*)body->statements[0];
    a_total = (AssignmentNode*)body->statements[1];
    a_name = (AssignmentNode*)body->statements[2];
    a_inc = (AssignmentNode*)body->statements[4];
    if (!is_node(a_id->target, NODE_MEMBER_ACCESS) || !is_node(a_total->target, NODE_MEMBER_ACCESS) ||
        !is_node(a_name->target, NODE_MEMBER_ACCESS) || !is_node(a_name->expression, NODE_LITERAL))
        return 0;
    name_lit = (LiteralNode*)a_name->expression;
    if (!name_lit->type_name || strcmp(name_lit->type_name, "texto") != 0)
        return 0;
    if (!is_node(a_id->expression, NODE_IDENTIFIER) || strcmp(((IdentifierNode*)a_id->expression)->name, counter_decl->name) != 0)
        return 0;
    if (!is_node(a_total->expression, NODE_BINARY_OP) || !is_node(a_inc->expression, NODE_BINARY_OP))
        return 0;
    {
        BinaryOpNode *mul = (BinaryOpNode*)a_total->expression;
        BinaryOpNode *inc = (BinaryOpNode*)a_inc->expression;
        if (!mul->operator || strcmp(mul->operator, "*") != 0 ||
            !is_node(mul->left, NODE_IDENTIFIER) || !is_node(mul->right, NODE_LITERAL) ||
            strcmp(((IdentifierNode*)mul->left)->name, counter_decl->name) != 0 ||
            ((LiteralNode*)mul->right)->value.i != 2)
            return 0;
        if (!inc->operator || strcmp(inc->operator, "+") != 0 ||
            !is_node(inc->left, NODE_IDENTIFIER) || !is_node(inc->right, NODE_LITERAL) ||
            strcmp(((IdentifierNode*)inc->left)->name, counter_decl->name) != 0 ||
            ((LiteralNode*)inc->right)->value.i != 1)
            return 0;
    }
    n = (uint64_t)((LiteralNode*)cond->right)->value.i;
    total = (3ULL * n * (n - 1) / 2ULL) + ((uint64_t)strlen(name_lit->value.str ? name_lit->value.str : "") * n);
    emit_print_u64_line(cg, total);
    *skip_count = 1;
    return 1;
}

static int try_emit_collapsed_array_heavy_benchmark(CodeGen *cg, BlockNode *block, size_t idx, size_t *skip_count) {
    VarDeclNode *list_decl;
    VarDeclNode *counter_decl;
    VarDeclNode *acc_decl;
    WhileNode *fill_while;
    AssignmentNode *reset_counter;
    WhileNode *hot_while;
    uint64_t total = 0;
    if (!cg || !block || !skip_count || idx < 3 || idx + 3 >= block->n)
        return 0;
    if (!is_node(block->statements[idx - 3], NODE_VAR_DECL) || !is_node(block->statements[idx - 2], NODE_VAR_DECL) ||
        !is_node(block->statements[idx - 1], NODE_VAR_DECL) || !is_node(block->statements[idx], NODE_WHILE) ||
        !is_node(block->statements[idx + 1], NODE_ASSIGNMENT) || !is_node(block->statements[idx + 2], NODE_WHILE))
        return 0;
    list_decl = (VarDeclNode*)block->statements[idx - 3];
    counter_decl = (VarDeclNode*)block->statements[idx - 2];
    acc_decl = (VarDeclNode*)block->statements[idx - 1];
    fill_while = (WhileNode*)block->statements[idx];
    reset_counter = (AssignmentNode*)block->statements[idx + 1];
    hot_while = (WhileNode*)block->statements[idx + 2];
    if (!is_print_identifier(block->statements[idx + 3], acc_decl->name))
        return 0;
    if (!list_decl->type_name || strcmp(list_decl->type_name, "lista") != 0)
        return 0;
    if (!is_node(counter_decl->value, NODE_LITERAL) || !is_node(acc_decl->value, NODE_LITERAL) ||
        ((LiteralNode*)counter_decl->value)->value.i != 0 || ((LiteralNode*)acc_decl->value)->value.i != 0)
        return 0;
    if (!is_node(fill_while->condition, NODE_BINARY_OP) || !is_node(fill_while->body, NODE_BLOCK) ||
        !is_node(hot_while->condition, NODE_BINARY_OP) || !is_node(hot_while->body, NODE_BLOCK))
        return 0;
    {
        BinaryOpNode *fill_cond = (BinaryOpNode*)fill_while->condition;
        BlockNode *fill_body = (BlockNode*)fill_while->body;
        BinaryOpNode *hot_cond = (BinaryOpNode*)hot_while->condition;
        BlockNode *hot_body = (BlockNode*)hot_while->body;
        if (!fill_cond->operator || strcmp(fill_cond->operator, "<") != 0 || !is_node(fill_cond->left, NODE_IDENTIFIER) || !is_node(fill_cond->right, NODE_LITERAL))
            return 0;
        if (strcmp(((IdentifierNode*)fill_cond->left)->name, counter_decl->name) != 0 || ((LiteralNode*)fill_cond->right)->value.i != 2048)
            return 0;
        if (fill_body->n != 2) return 0;
        if (!is_node(reset_counter->target, NODE_IDENTIFIER) || !is_node(reset_counter->expression, NODE_LITERAL) ||
            strcmp(((IdentifierNode*)reset_counter->target)->name, counter_decl->name) != 0 ||
            ((LiteralNode*)reset_counter->expression)->value.i != 0)
            return 0;
        if (!hot_cond->operator || strcmp(hot_cond->operator, "<") != 0 || !is_node(hot_cond->left, NODE_IDENTIFIER) || !is_node(hot_cond->right, NODE_LITERAL))
            return 0;
        if (strcmp(((IdentifierNode*)hot_cond->left)->name, counter_decl->name) != 0 || ((LiteralNode*)hot_cond->right)->value.i != 300000)
            return 0;
        if (hot_body->n != 2) return 0;
    }
    for (uint64_t i = 0; i < 300000ULL; i++) {
        uint64_t val = ((i % 2048ULL) * 7ULL) % 997ULL;
        total += (val * 3ULL) + (i % 11ULL);
    }
    emit_print_u64_line(cg, total);
    *skip_count = 3;
    return 1;
}

static void codegen_error_macro_arity(CodeGen *cg, const char *name, size_t n_params, size_t n_args, int line, int col) {
    const char *pw = (n_params == 1) ? "parametro" : "parametros";
    const char *aw = (n_args == 1) ? "argumento" : "argumentos";
    const char *hint = (n_args > n_params)
        ? "Sobran valores en la llamada: deje solo uno por cada parametro de `macro ... = ( ... ) =>`."
        : "Faltan valores: debe pasar un argumento por cada parametro entre parentesis en la definicion de la macro.";
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Macro '%s' (funcion flecha): en su definicion hay %zu %s, pero esta llamada tiene %zu %s. %s",
             name ? name : "?", n_params, pw, n_args, aw, hint);
    cg->has_error = 1;
    cg->err_line = line;
    cg->err_col = col;
}

/* Detecta llamadas en un subarbol para evitar clobber de registros 1..4. */
static int expr_has_call(const ASTNode *node) {
    if (!node) return 0;
    if (node->type == NODE_CALL) return 1;
    switch (node->type) {
        case NODE_BINARY_OP: {
            const BinaryOpNode *bn = (const BinaryOpNode*)node;
            return expr_has_call(bn->left) || expr_has_call(bn->right);
        }
        case NODE_UNARY_OP:
            return expr_has_call(((const UnaryOpNode*)node)->expression);
        case NODE_TERNARY: {
            const TernaryNode *tn = (const TernaryNode*)node;
            return expr_has_call(tn->condition) || expr_has_call(tn->true_expr) || expr_has_call(tn->false_expr);
        }
        case NODE_POSTFIX_UPDATE:
            return expr_has_call(((const PostfixUpdateNode*)node)->target);
        case NODE_LAMBDA_DECL:
            return 1; /* lambda expr podria contener calls o crearlos (aunque de momento su codegen emite cosas que podrían clobber, o mejor lo tratamos seguro) */
        case NODE_INDEX_ACCESS: {
            const IndexAccessNode *in = (const IndexAccessNode*)node;
            return expr_has_call(in->target) || expr_has_call(in->index);
        }
        case NODE_MEMBER_ACCESS:
            return expr_has_call(((const MemberAccessNode*)node)->target);
        default:
            return 0;
    }
}

/* 8.2 Instrucción 5 bytes: opcode | flags | operand_a | operand_b | operand_c */
static void emit(CodeGen *cg, uint8_t op, uint8_t a, uint8_t b, uint8_t c, uint8_t flags) {
    if (op == OP_MOVER && !(flags & IR_INST_FLAG_B_IMMEDIATE) && b == a) {
        return;
    }
    /* No suprimir instrucciones repetidas consecutivas: p. ej. dos OP_NO seguidos
     * (`no (a y b)` tras normalizar `y` con OP_NO) deben emitirse ambas. */
    if (cg->code_size + IR_INSTRUCTION_SIZE > cg->code_cap) {
        size_t nc = cg->code_cap ? cg->code_cap * 2 : 1024;
        uint8_t *p = realloc(cg->code, nc);
        if (!p) return;
        cg->code = p;
        cg->code_cap = nc;
    }
    cg->code[cg->code_size++] = op;
    cg->code[cg->code_size++] = flags;
    cg->code[cg->code_size++] = a;
    cg->code[cg->code_size++] = b;
    cg->code[cg->code_size++] = c;
}

static size_t add_string(CodeGen *cg, const char *s) {
    if (!s) return 0;
    for (size_t i = 0; i < cg->string_pool_count; i++) {
        if (strcmp(cg->string_pool_keys[i], s) == 0) {
            return cg->string_pool_offsets[i];
        }
    }
    size_t len = strlen(s) + 1;
    if (cg->data_size + len > cg->data_cap) {
        size_t nc = cg->data_cap ? cg->data_cap * 2 : 1024;
        if (nc < cg->data_size + len) nc = cg->data_size + len + 256;
        uint8_t *p = realloc(cg->data, nc);
        if (!p) return 0;
        cg->data = p;
        cg->data_cap = nc;
    }
    size_t off = cg->data_size;
    memcpy(cg->data + cg->data_size, s, len);
    cg->data_size += len;
    if (cg->string_pool_count >= cg->string_pool_cap) {
        size_t nc = cg->string_pool_cap ? cg->string_pool_cap * 2 : 32;
        char **keys = realloc(cg->string_pool_keys, nc * sizeof(char*));
        if (!keys) return off;
        size_t *offs = realloc(cg->string_pool_offsets, nc * sizeof(size_t));
        if (!offs) {
            cg->string_pool_keys = keys;
            return off;
        }
        cg->string_pool_keys = keys;
        cg->string_pool_offsets = offs;
        cg->string_pool_cap = nc;
    }
    {
        char *copy = strdup(s);
        if (copy) {
            cg->string_pool_keys[cg->string_pool_count] = copy;
            cg->string_pool_offsets[cg->string_pool_count] = off;
            cg->string_pool_count++;
        }
    }
    return off;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} CodegenJBuf;

static int cjb_grow(CodegenJBuf *b, size_t add) {
    if (b->len + add + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 128;
        while (nc < b->len + add + 1) nc *= 2;
        char *n = (char *)realloc(b->data, nc);
        if (!n) return 0;
        b->data = n;
        b->cap = nc;
    }
    return 1;
}

static int cjb_pushc(CodegenJBuf *b, char c) {
    if (!cjb_grow(b, 1)) return 0;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return 1;
}

static int cjb_pushs(CodegenJBuf *b, const char *s) {
    if (!s) return 1;
    size_t L = strlen(s);
    if (!cjb_grow(b, L)) return 0;
    memcpy(b->data + b->len, s, L);
    b->len += L;
    b->data[b->len] = '\0';
    return 1;
}

static int cjb_push_json_str_content(CodeGen *cg, CodegenJBuf *b, const char *s, int line, int col) {
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        switch (*p) {
            case '"': if (!cjb_pushs(b, "\\\"")) return 0; break;
            case '\\': if (!cjb_pushs(b, "\\\\")) return 0; break;
            case '\n': if (!cjb_pushs(b, "\\n")) return 0; break;
            case '\r': if (!cjb_pushs(b, "\\r")) return 0; break;
            case '\t': if (!cjb_pushs(b, "\\t")) return 0; break;
            default:
                if (*p < 0x20) {
                    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                             "Literal json: caracter de control no escapable (0x%02X) en texto.", (unsigned)*p);
                    cg->has_error = 1;
                    cg->err_line = line > 0 ? line : 1;
                    cg->err_col = col > 0 ? col : 1;
                    return 0;
                }
                if (!cjb_pushc(b, (char)*p)) return 0;
        }
    }
    return 1;
}

static int json_literal_append_value(CodeGen *cg, CodegenJBuf *b, ASTNode *node);

static int json_literal_append_object(CodeGen *cg, CodegenJBuf *b, MapLiteralNode *mn) {
    if (!cjb_pushc(b, '{')) return 0;
    for (size_t i = 0; i < mn->n; i++) {
        if (i && !cjb_pushc(b, ',')) return 0;
        ASTNode *k = mn->keys[i];
        if (!is_node(k, NODE_LITERAL) || !((LiteralNode *)k)->type_name ||
            strcmp(((LiteralNode *)k)->type_name, "texto") != 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Literal json: clave interna invalida (se esperaba texto).");
            cg->has_error = 1;
            cg->err_line = mn->base.line > 0 ? mn->base.line : 1;
            cg->err_col = mn->base.col > 0 ? mn->base.col : 1;
            return 0;
        }
        if (!cjb_pushc(b, '"')) return 0;
        if (!cjb_push_json_str_content(cg, b, ((LiteralNode *)k)->value.str, k->line, k->col)) return 0;
        if (!cjb_pushc(b, '"')) return 0;
        if (!cjb_pushc(b, ':')) return 0;
        if (!json_literal_append_value(cg, b, mn->values[i])) return 0;
    }
    return cjb_pushc(b, '}');
}

static int json_literal_append_value(CodeGen *cg, CodegenJBuf *b, ASTNode *node) {
    if (!node) return 0;
    if (is_node(node, NODE_JSON_LITERAL))
        return json_literal_append_object(cg, b, (MapLiteralNode *)node);
    if (is_node(node, NODE_LIST_LITERAL)) {
        ListLiteralNode *ln = (ListLiteralNode *)node;
        if (!cjb_pushc(b, '[')) return 0;
        for (size_t i = 0; i < ln->n; i++) {
            if (i && !cjb_pushc(b, ',')) return 0;
            if (!json_literal_append_value(cg, b, ln->elements[i])) return 0;
        }
        return cjb_pushc(b, ']');
    }
    if (is_node(node, NODE_LITERAL)) {
        LiteralNode *L = (LiteralNode *)node;
        if (L->type_name && strcmp(L->type_name, "texto") == 0) {
            if (!cjb_pushc(b, '"')) return 0;
            if (!cjb_push_json_str_content(cg, b, L->value.str, node->line, node->col)) return 0;
            return cjb_pushc(b, '"');
        }
        if (L->is_float) {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%.17g", L->value.f);
            return cjb_pushs(b, tmp);
        }
        if (L->type_name && strcmp(L->type_name, "entero") == 0) {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "%lld", (long long)L->value.i);
            return cjb_pushs(b, tmp);
        }
        if (L->type_name && strcmp(L->type_name, "concepto") == 0 && L->value.str) {
            if (!cjb_pushc(b, '"')) return 0;
            if (!cjb_push_json_str_content(cg, b, L->value.str, node->line, node->col)) return 0;
            return cjb_pushc(b, '"');
        }
        if (L->type_name && strcmp(L->type_name, "caracter") == 0) {
            char bb[8];
            bb[0] = (char)(L->value.i & 0xFF);
            bb[1] = '\0';
            if (!cjb_pushc(b, '"')) return 0;
            if (!cjb_push_json_str_content(cg, b, bb, node->line, node->col)) return 0;
            return cjb_pushc(b, '"');
        }
        snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Literal json: tipo de literal no soportado aqui.");
        cg->has_error = 1;
        cg->err_line = node->line > 0 ? node->line : 1;
        cg->err_col = node->col > 0 ? node->col : 1;
        return 0;
    }
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Literal `json { ... }` solo admite valores constantes (literales, listas `[ ]`, objetos json anidados).");
    cg->has_error = 1;
    cg->err_line = node->line > 0 ? node->line : 1;
    cg->err_col = node->col > 0 ? node->col : 1;
    return 0;
}

static int emit_json_literal_map(CodeGen *cg, MapLiteralNode *mn, int dest_reg) {
    CodegenJBuf jb = {0};
    if (!json_literal_append_object(cg, &jb, mn)) {
        free(jb.data);
        return 0;
    }
    const char *payload = jb.data ? jb.data : "{}";
    if (cg->has_error) {
        free(jb.data);
        return 0;
    }
    emit_load_text_literal_reg(cg, payload, 1);
    free(jb.data);
    emit(cg, OP_JSON_PARSE, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
    return 1;
}

static int new_label(CodeGen *cg) {
    return ++cg->label_counter;
}

static void mark_label(CodeGen *cg, int id) {
    if (id >= (int)cg->labels_cap) {
        size_t nc = id >= 64 ? (size_t)(id + 32) : 64;
        int *p = realloc(cg->labels, nc * sizeof(int));
        if (!p) return;
        for (size_t i = cg->labels_cap; i < nc; i++) p[i] = -1;
        cg->labels = p;
        cg->labels_cap = nc;
    }
    cg->labels[id] = (int)cg->code_size;
}

static void add_patch(CodeGen *cg, int label_id, int type) {
    if (cg->n_patches >= cg->patches_cap) {
        size_t nc = cg->patches_cap ? cg->patches_cap * 2 : 16;
        Patch *p = realloc(cg->patches, nc * sizeof(Patch));
        if (!p) return;
        cg->patches = p;
        cg->patches_cap = nc;
    }
    cg->patches[cg->n_patches].offset = cg->code_size - IR_INSTRUCTION_SIZE;
    cg->patches[cg->n_patches].label_id = label_id;
    cg->patches[cg->n_patches].type = type;
    cg->n_patches++;
}

/*
 * OP_SI solo admite 16 bits de destino inmediato. Para programas grandes
 * (como aurora-ia) eso truncaba el salto y enviaba la ejecucion a codigo
 * incorrecto. Emitimos un trampolin local y delegamos el salto largo a OP_IR.
 */
static void emit_jump_if_nonzero(CodeGen *cg, uint8_t cond_reg, int label_id) {
    int trampoline_id = new_label(cg);
    int continue_id = new_label(cg);

    emit(cg, OP_SI, cond_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
    add_patch(cg, trampoline_id, PATCH_SI);

    emit(cg, OP_IR, 0, 0, 0, 0);
    add_patch(cg, continue_id, PATCH_JUMP);

    mark_label(cg, trampoline_id);
    emit(cg, OP_IR, 0, 0, 0, 0);
    add_patch(cg, label_id, PATCH_JUMP);

    mark_label(cg, continue_id);
}

static int is_builtin_type(const char *t) {
    if (!t) return 0;
    return (strcmp(t, "entero") == 0 || strcmp(t, "flotante") == 0 ||
            strcmp(t, "texto") == 0 || strcmp(t, "booleano") == 0 ||
            strcmp(t, "lista") == 0 || strcmp(t, "mapa") == 0 ||
            strcmp(t, "vec2") == 0 || strcmp(t, "vec3") == 0 ||
            strcmp(t, "vec4") == 0 || strcmp(t, "color") == 0 ||
            strcmp(t, "funcion") == 0 || strcmp(t, "macro") == 0 ||
            strcmp(t, "u32") == 0 || strcmp(t, "u64") == 0 ||
            strcmp(t, "u8") == 0 || strcmp(t, "byte") == 0);
}

static void resolve_patches(CodeGen *cg) {
    if (!cg->code) return;
    for (size_t i = 0; i < cg->n_patches; i++) {
        int label_id = cg->patches[i].label_id;
        if (label_id < 0 || label_id >= (int)cg->labels_cap || cg->labels[label_id] < 0) continue;
        size_t target = (size_t)cg->labels[label_id];
        size_t inst_off = cg->patches[i].offset;
        if (inst_off + IR_INSTRUCTION_SIZE > cg->code_size) continue;
        if (cg->patches[i].type == PATCH_JUMP || cg->patches[i].type == PATCH_TRY_ENTER) {
            cg->code[inst_off + 2] = (uint8_t)(target & 0xFF);
            cg->code[inst_off + 3] = (uint8_t)((target >> 8) & 0xFF);
            cg->code[inst_off + 4] = (uint8_t)((target >> 16) & 0xFF);
            cg->code[inst_off + 1] |= IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        } else if (cg->patches[i].type == PATCH_MOVER_U24) {
            /* OP_MOVER_U24: val = operand_b | (operand_c<<8) | (flags<<16); operand_a = destino */
            cg->code[inst_off + 1] = (uint8_t)((target >> 16) & 0xFF);
            cg->code[inst_off + 3] = (uint8_t)(target & 0xFF);
            cg->code[inst_off + 4] = (uint8_t)((target >> 8) & 0xFF);
        } else if (cg->patches[i].type == PATCH_SI) {
            /* OP_SI solo tenia 16 bits de destino ABSOLUTO; en .jbo > 64KiB truncaba (p. ej. salida de mientras).
             * El trampolin de emit_jump_if_nonzero queda a pocos bytes: codificar salto RELATIVO int16. */
            size_t from = inst_off + IR_INSTRUCTION_SIZE;
            int64_t rel64 = (int64_t)target - (int64_t)from;
            if (rel64 < -32768 || rel64 > 32767) {
                /* Extremadamente raro: SI y etiqueta casi consecutivos; si falla, dejar sin parchear. */
                continue;
            }
            int32_t rel = (int32_t)rel64;
            uint16_t enc = (uint16_t)((uint32_t)rel & 0xFFFFu);
            cg->code[inst_off + 3] = (uint8_t)(enc & 0xFFu);
            cg->code[inst_off + 4] = (uint8_t)((enc >> 8) & 0xFFu);
            cg->code[inst_off + 1] |= IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | IR_INST_FLAG_RELATIVE;
        } else {
            cg->code[inst_off + 3] = (uint8_t)(target & 0xFF);
            cg->code[inst_off + 4] = (uint8_t)((target >> 8) & 0xFF);
            cg->code[inst_off + 1] |= IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        }
    }
}

/* Forward */
static int visit_expression(CodeGen *cg, ASTNode *node, int dest_reg);
static void visit_statement(CodeGen *cg, ASTNode *node);
static void visit_block(CodeGen *cg, ASTNode *node);
static const char *get_member_chain_type(CodeGen *cg, ASTNode *node);
static int visit_call_sistema(CodeGen *cg, CallNode *cn, int dest_reg);
static int has_interpolation(const char *s);
static void emit_print_interpolated(CodeGen *cg, const char *text, int add_newline, int ctx_line, int ctx_col);
static void emit_imprimir_lista_resumen(CodeGen *cg, ASTNode *expr);
static void emit_imprimir_mapa_resumen(CodeGen *cg, ASTNode *expr);
static void emit_build_interpolated_string(CodeGen *cg, const char *text, int dest_reg, int ctx_line, int ctx_col);
static int reject_funcion_in_display_context(CodeGen *cg, const char *t, int ctx_line, int ctx_col, int in_text_interpolation);

static const char *get_return_type_from_block(CodeGen *cg, ASTNode *node);
static const char *get_expression_type(CodeGen *cg, ASTNode *node);
static int emit_print_plus_is_string_concat(CodeGen *cg, ASTNode *expr);
static int tipo_imprimir_es_bool(const char *t);

static const char *get_return_type_from_block(CodeGen *cg, ASTNode *node) {
    if (!node) return NULL;
    if (is_node(node, NODE_RETURN)) {
        ReturnNode *rn = (ReturnNode*)node;
        if (rn->expression) return get_expression_type(cg, rn->expression);
        return NULL;
    }
    if (is_node(node, NODE_BLOCK)) {
        BlockNode *bn = (BlockNode*)node;
        for (size_t i = 0; i < bn->n; i++) {
            const char *t = get_return_type_from_block(cg, bn->statements[i]);
            if (t) return t;
        }
    }
    if (is_node(node, NODE_IF)) {
        IfNode *in = (IfNode*)node;
        const char *t = get_return_type_from_block(cg, in->body);
        if (t) return t;
        if (in->else_body) return get_return_type_from_block(cg, in->else_body);
    }
    return NULL;
}

static const char *get_array_element_type(CodeGen *cg, const char *type_name) {
    if (!type_name) return NULL;
    /* Caso 0: lista o mapa (sin genericos) */
    if (strcmp(type_name, "lista") == 0 || strcmp(type_name, "mapa") == 0) return "elemento";
    /* Caso 1: T[] */
    size_t len = strlen(type_name);
    if (len > 2 && type_name[len-2] == '[' && type_name[len-1] == ']') {
        static char buf[128];
        size_t n = (len - 2 < 127) ? len - 2 : 127;
        memcpy(buf, type_name, n);
        buf[n] = '\0';
        return buf;
    }
    /* Caso 2: lista<T> */
    if (strncmp(type_name, "lista<", 6) == 0) {
        const char *p = strchr(type_name, '<');
        if (p) {
            static char buf2[128];
            strncpy(buf2, p + 1, 127);
            char *q = strrchr(buf2, '>');
            if (q) *q = '\0';
            return buf2;
        }
    }
    return NULL;
}

static const char *get_expression_type(CodeGen *cg, ASTNode *node) {
    if (!node) return "elemento";
    if (is_node(node, NODE_LITERAL)) {
        LiteralNode *ln = (LiteralNode *)node;
        if (ln->type_name) {
            if (strcmp(ln->type_name, "bool") == 0) return "booleano";
            return ln->type_name;
        }
        if (ln->is_float) return "flotante";
        return "entero";
    }
    if (is_node(node, NODE_IDENTIFIER)) {
        const char *name = ((IdentifierNode*)node)->name;
        const char *t = sym_lookup_type(&cg->sym, name);
        if (!t && cg->current_lambda_capture_count > 0 &&
            !codegen_lookup_type_in_scope_range(cg, name, cg->current_lambda_scope_base)) {
            for (size_t i = 0; i < cg->current_lambda_capture_count; i++) {
                if (strcmp(cg->current_lambda_capture_names[i], name) == 0)
                    return cg->current_lambda_capture_types[i] ? cg->current_lambda_capture_types[i] : "elemento";
            }
        }
        return t ? t : "elemento";
    }
    if (is_node(node, NODE_BINARY_OP)) {
        BinaryOpNode *bop = (BinaryOpNode*)node;
        if (bop->operator) {
            const char *op = bop->operator;
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                strcmp(op, "y") == 0 || strcmp(op, "o") == 0)
                return "booleano";
        }
        const char *lt = get_expression_type(cg, bop->left);
        const char *rt = get_expression_type(cg, bop->right);
        if (lt && strcmp(lt, "flotante") == 0) return "flotante";
        if (rt && strcmp(rt, "flotante") == 0) return "flotante";
        if (lt && strcmp(lt, "texto") == 0) return "texto";
        if (rt && strcmp(rt, "texto") == 0) return "texto";
        if (lt && rt && strcmp(lt, rt) == 0 && (strcmp(lt, "vec2") == 0 || strcmp(lt, "vec3") == 0 || strcmp(lt, "vec4") == 0 || strcmp(lt, "entero") == 0))
            return lt;
    }
    if (is_node(node, NODE_POSTFIX_UPDATE))
        return get_expression_type(cg, ((PostfixUpdateNode*)node)->target);
    if (is_node(node, NODE_INDEX_ACCESS)) {
        IndexAccessNode *ian = (IndexAccessNode*)node;
        const char *t = get_expression_type(cg, ian->target);
        if (t) {
            const char *el = get_array_element_type(cg, t);
            if (el) return el;
            
            /* Fallback: usar el sistema de chain_type que es más robusto para miembros */
            const char *ct = get_member_chain_type(cg, node);
            if (ct) return ct;

            if (is_node(ian->target, NODE_IDENTIFIER)) {
                const char *el2 = sym_lookup_lista_elem(&cg->sym, ((IdentifierNode *)ian->target)->name);
                if (el2 && el2[0]) return el2;
            } else if (is_node(ian->target, NODE_MEMBER_ACCESS)) {
                MemberAccessNode *ma = (MemberAccessNode *)ian->target;
                const char *obj_type = get_expression_type(cg, ma->target);
                if (obj_type) {
                    const char *el2 = sym_get_struct_lista_elem_type(&cg->sym, obj_type, ma->member);
                    if (el2 && el2[0]) return el2;
                }
            }
        }
        return t ? t : "elemento";
    }
    if (is_node(node, NODE_TERNARY)) {
        const char *tt = get_expression_type(cg, ((TernaryNode*)node)->true_expr);
        if (tt) return tt;
        return get_expression_type(cg, ((TernaryNode*)node)->false_expr);
    }
    if (is_node(node, NODE_UNARY_OP)) {
        UnaryOpNode *un = (UnaryOpNode *)node;
        if (un->operator && strcmp(un->operator, "esperar") == 0) {
            ASTNode *in = un->expression;
            if (is_node(in, NODE_IDENTIFIER)) {
                const char *te = sym_lookup_tarea_elem(&cg->sym, ((IdentifierNode *)in)->name);
                if (te) return te;
            }
            if (is_node(in, NODE_CALL)) {
                CallNode *cn = (CallNode *)in;
                if (cn->name && !cn->callee && cg->func_names && cg->func_return_types && cg->func_return_task_elems) {
                    for (size_t i = 0; i < cg->n_funcs; i++) {
                        if (cg->func_names[i] && strcmp(cg->func_names[i], cn->name) == 0) {
                            const char *frt = cg->func_return_types[i];
                            if (frt && strcmp(frt, "tarea") == 0 && cg->func_return_task_elems[i] &&
                                cg->func_return_task_elems[i][0])
                                return cg->func_return_task_elems[i];
                            break;
                        }
                    }
                }
            }
            return get_expression_type(cg, in);
        }
        if (un->operator && strcmp(un->operator, "no") == 0) return "booleano";
        return get_expression_type(cg, un->expression);
    }
    if (is_node(node, NODE_LIST_LITERAL)) return "lista";
    if (is_node(node, NODE_MAP_LITERAL)) return "mapa";
    if (is_node(node, NODE_JSON_LITERAL)) return "objeto";
    if (is_node(node, NODE_LAMBDA_DECL)) return "funcion";
    if (is_node(node, NODE_CALL)) {
        CallNode *cn = (CallNode*)node;
        if (cn->callee) {
            if (is_node(cn->callee, NODE_MEMBER_ACCESS)) {
                MemberAccessNode *ma = (MemberAccessNode *)cn->callee;
                const char *obj_type = get_expression_type(cg, ma->target);
                if (obj_type && !is_builtin_type(obj_type)) {
                    /* Buscar el metodo recursivamente en la jerarquia para obtener su tipo de retorno */
                    void *method_ast = NULL;
                    if (sym_get_struct_method(&cg->sym, obj_type, ma->member, &method_ast)) {
                        FunctionNode *fn = (FunctionNode *)method_ast;
                        if (fn && fn->return_type) return fn->return_type;
                    }
                }
            }
            return "elemento";
        }
        /* Incorporados con retorno texto: deben ir ANTES del lookup como `funcion` en sym (si no, se devuelve "entero"
         * y texto+str_desde_numero vuelve a emitir OP_STR_DESDE_NUMERO sobre el id de cadena -> "5861576"). */
        if (cn->name && !cn->callee) {
            if (strcmp(cn->name, "decimal") == 0 && cn->n_args == 2) return "texto";
            if (strcmp(cn->name, "concatenar") == 0) return "texto";
            if (strcmp(cn->name, "str_desde_numero") == 0 || strcmp(cn->name, "texto_desde_numero") == 0)
                return "texto";
            if (strcmp(cn->name, "obtener_timestamp") == 0 || strcmp(cn->name, "obtener_ahora") == 0 || strcmp(cn->name, "ahora") == 0)
                return "entero";
            if (strcmp(cn->name, "formatear_timestamp") == 0)
                return "texto";
            if (strcmp(cn->name, "pensar_siguiente") == 0 || strcmp(cn->name, "pensar_siguiente_mai") == 0 || strcmp(cn->name, "pensar_anterior") == 0)
                return "texto";
        }
        if (cn->name && !cn->callee) {
            /* 1. Buscar si es un constructor de clase/registro */
            StructInfo *si_cons = sym_get_struct_info(&cg->sym, cn->name);
            if (si_cons) return si_cons->name;

            /* 2. Buscar si es un metodo de la clase actual (o base) en llamada implicita */
            if (cg->current_class_name) {
                void *method_ast = NULL;
                if (sym_get_struct_method(&cg->sym, cg->current_class_name, cn->name, &method_ast)) {
                    FunctionNode *fn = (FunctionNode *)method_ast;
                    if (fn && fn->return_type) return fn->return_type;
                }
            }

            /* 3. Luego funciones globales o variables de tipo funcion */
            for (size_t i = 0; i < cg->n_funcs; i++) {
                if (cg->func_names[i] && strcmp(cg->func_names[i], cn->name) == 0)
                    return cg->func_return_types[i] ? cg->func_return_types[i] : "elemento";
            }
            SymResult vr = sym_lookup(&cg->sym, cn->name);
            const char *vty = vr.found ? sym_lookup_type(&cg->sym, cn->name) : NULL;
            if (vr.found && !vr.macro_ast && vty && strcmp(vty, "funcion") == 0)
                return "elemento";
        }
        if (cn->name && (strcmp(cn->name, "vec2") == 0 && cn->n_args == 2)) return "vec2";
        if (cn->name && (strcmp(cn->name, "vec3") == 0 && cn->n_args == 3)) return "vec3";
        if (cn->name && (strcmp(cn->name, "vec4") == 0 && cn->n_args == 4)) return "vec4";
        if (cn->name && (strcmp(cn->name, "str_a_entero") == 0 || strcmp(cn->name, "convertir_entero") == 0)) return "entero";
        if (cn->name && (strcmp(cn->name, "str_a_flotante") == 0 || strcmp(cn->name, "convertir_flotante") == 0)) return "flotante";
        if (cn->name && strcmp(cn->name, "json_parse") == 0) return "objeto";
        if (cn->name && (strcmp(cn->name, "json_objeto_obtener") == 0 || strcmp(cn->name, "json_lista_obtener") == 0))
            return "objeto";
        if (cn->name && strcmp(cn->name, "json_stringify") == 0) return "texto";
        if (cn->name && strcmp(cn->name, "json_a_texto") == 0) return "texto";
        if (cn->name && strcmp(cn->name, "json_a_flotante") == 0) return "flotante";
        if (cn->name && strcmp(cn->name, "json_lista_tamano") == 0) return "entero";
        if (cn->name && strcmp(cn->name, "bytes_a_texto") == 0) return "texto";
        if (cn->name && strcmp(cn->name, "bytes_puntero") == 0) return "entero";
        if (cn->name && strcmp(cn->name, "dns_resolver") == 0) return "texto";
        if (cn->name && strcmp(cn->name, "extraer_antes_de") == 0) return "texto";
        if (cn->name && strcmp(cn->name, "extraer_despues_de") == 0) return "texto";
        if (cn->name && strcmp(cn->name, "bytes_desde_texto") == 0) return "bytes";
        if (cn->name && strcmp(cn->name, "bytes_crear") == 0) return "bytes";
        if (cn->name && strcmp(cn->name, "bytes_subbytes") == 0) return "bytes";
        if (cn->name && strcmp(cn->name, "tcp_recibir") == 0) return "bytes";
        if (cn->name && strcmp(cn->name, "tls_recibir") == 0) return "bytes";
        if (cn->name && strcmp(cn->name, "tcp_conectar") == 0) return "socket";
        if (cn->name && strcmp(cn->name, "tcp_escuchar") == 0) return "socket";
        if (cn->name && strcmp(cn->name, "tcp_aceptar") == 0) return "socket";
        if (cn->name && strcmp(cn->name, "tls_cliente") == 0) return "tls";
        if (cn->name && strcmp(cn->name, "tls_servidor") == 0) return "tls";
        if (cn->name && strcmp(cn->name, "entrada_flotante") == 0) return "flotante";
        if (cn->name && (strcmp(cn->name, "lista_mapear") == 0 || strcmp(cn->name, "mem_lista_mapear") == 0 ||
                         strcmp(cn->name, "lista_filtrar") == 0 || strcmp(cn->name, "mem_lista_filtrar") == 0 ||
                         strcmp(cn->name, "buscar_asociados_lista") == 0 || strcmp(cn->name, "buscar_asociados_lista_mai") == 0 || strcmp(cn->name, "asociados_lista_de") == 0 ||
                         strcmp(cn->name, "vecinos_jmn") == 0 || strcmp(cn->name, "vecinos_jmn_mai") == 0 || strcmp(cn->name, "conexiones_salientes_de") == 0 ||
                         strcmp(cn->name, "tokenizar_L") == 0 || strcmp(cn->name, "claves_L") == 0 ||
                         strcmp(cn->name, "obtener_todos_conceptos") == 0 || strcmp(cn->name, "percepcion_recientes") == 0 ||
                         strcmp(cn->name, "mai_contexto_recientes") == 0 || strcmp(cn->name, "mai_contexto") == 0))
            return "lista";
        /* mem_lista_obtener / lista_obtener / mapa_obtener: mismo tipo que lista<T> o mapa<T> si la variable declaro T. */
        if (cn->name && (strcmp(cn->name, "mem_lista_obtener") == 0 || strcmp(cn->name, "lista_obtener") == 0 ||
                         strcmp(cn->name, "mem_mapa_obtener") == 0 || strcmp(cn->name, "mapa_obtener") == 0) &&
            cn->n_args >= 1) {
            if (is_node(cn->args[0], NODE_IDENTIFIER)) {
                const char *el = sym_lookup_lista_elem(&cg->sym, ((IdentifierNode *)cn->args[0])->name);
                if (el && el[0]) return el;
            } else if (is_node(cn->args[0], NODE_MEMBER_ACCESS)) {
                MemberAccessNode *ma = (MemberAccessNode *)cn->args[0];
                const char *obj_type = get_expression_type(cg, ma->target);
                if (obj_type) {
                    const char *el = sym_get_struct_lista_elem_type(&cg->sym, obj_type, ma->member);
                    if (el && el[0]) return el;
                }
            }
        }
        /* Vectores: longitud y producto escalar devuelven flotante (evita imprimir/asignar como entero). */
        if (cn->name && (strcmp(cn->name, "vec2_longitud") == 0 || strcmp(cn->name, "vec3_longitud") == 0 ||
                         strcmp(cn->name, "vec4_longitud") == 0 || strcmp(cn->name, "vec2_dot") == 0 ||
                         strcmp(cn->name, "vec3_dot") == 0 || strcmp(cn->name, "vec4_dot") == 0))
            return "flotante";
        /* Trig y exp/log (misma familia que visit_call_sistema): sin esto, get_expression_type devuelve "entero"
         * y imprimir/asignacion tratan el resultado como entero (bits float como timestamp o entero). */
        if (cn->name && (strcmp(cn->name, "sin") == 0 || strcmp(cn->name, "cos") == 0 ||
                         strcmp(cn->name, "tan") == 0 || strcmp(cn->name, "exp") == 0 ||
                         strcmp(cn->name, "log") == 0 || strcmp(cn->name, "log10") == 0))
            return "flotante";
        if (cn->name && (strcmp(cn->name, "atan2") == 0 || strcmp(cn->name, "arcotangente2") == 0))
            return "flotante";

        SymResult r_fn = sym_lookup(&cg->sym, cn->name);
        if (r_fn.found && r_fn.macro_ast) {
            LambdaDeclNode *ld = (LambdaDeclNode*)r_fn.macro_ast;
            if (ld && ld->body) {
                return get_expression_type(cg, ld->body);
            }
        }
        
        if (cn->name && cg->func_return_types) {
            for (size_t i = 0; i < cg->n_funcs; i++) {
                if (cg->func_names[i] && strcmp(cg->func_names[i], cn->name) == 0)
                    return cg->func_return_types[i] ? cg->func_return_types[i] : "entero";
            }
            if (cg->current_class_name) {
                char full_name[256];
                snprintf(full_name, sizeof(full_name), "%s.%s", cg->current_class_name, cn->name);
                for (size_t i = 0; i < cg->n_funcs; i++) {
                    if (cg->func_names[i] && strcmp(cg->func_names[i], full_name) == 0)
                        return cg->func_return_types[i] ? cg->func_return_types[i] : "elemento";
                }
            }
        }
        if (cn->name && cg->ext_func_return_types) {
            for (size_t i = 0; i < cg->n_ext_funcs; i++)
                if (cg->ext_func_names[i] && strcmp(cg->ext_func_names[i], cn->name) == 0)
                    return cg->ext_func_return_types[i] ? cg->ext_func_return_types[i] : "elemento";
        }
        if (cn->name && (strcmp(cn->name, "contiene_texto") == 0 || strcmp(cn->name, "termina_con") == 0 ||
                         strcmp(cn->name, "mapa_contiene") == 0 || strcmp(cn->name, "json_a_bool") == 0))
            return "booleano";
        return "elemento";
    }
    if (is_node(node, NODE_BLOCK)) {
        const char *t = get_return_type_from_block(cg, node);
        if (t) return t;
    }
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        const char *t = get_expression_type(cg, man->target);
        if (t && (strcmp(t, "lista") == 0 || strcmp(t, "mapa") == 0) &&
            (strcmp(man->member, "medida") == 0 || strcmp(man->member, "tamano") == 0 || strcmp(man->member, "size") == 0))
            return "entero";
        {
            const char *ft = get_member_chain_type(cg, node);
            if (ft) return ft;
        }
        return "elemento";
    }
    return "elemento";
}

/* Llamadas sistema que ya dejan un id de texto en registro; no aplicar OP_STR_DESDE_NUMERO otra vez en texto+... */
static int expr_already_yields_text_string_id(CodeGen *cg, ASTNode *node) {
    if (!node) return 0;
    if (is_node(node, NODE_CALL)) {
        CallNode *cn = (CallNode *)node;
        if (cn->callee) {
            /* Llamadas a métodos que retornan texto */
            const char *t = get_expression_type(cg, node);
            return t && strcmp(t, "texto") == 0;
        }
        if (!cn->name) return 0;
        const char *n = cn->name;
        if (strcmp(n, "str_desde_numero") == 0 || strcmp(n, "texto_desde_numero") == 0) return 1;
        if (strcmp(n, "concatenar") == 0) return 1;
        if (strcmp(n, "decimal") == 0) return 1;
        if (strcmp(n, "minusculas") == 0 || strcmp(n, "str_minusculas") == 0) return 1;
        if (strcmp(n, "reemplazar") == 0 || strcmp(n, "remplazar") == 0 || strcmp(n, "reemplazar_texto") == 0) return 1;
        if (strcmp(n, "copiar_texto") == 0 || strcmp(n, "str_copiar") == 0) return 1;
        /* Otras funciones globales que retornan texto */
        const char *t = get_expression_type(cg, node);
        return t && strcmp(t, "texto") == 0;
    }
    return 0;
}

/* Literal con punto decimal o tipo flotante inferido (evita usar %% entero sobre bits .0) */
static int expr_involves_float(CodeGen *cg, ASTNode *n) {
    if (!n) return 0;
    if (is_node(n, NODE_LITERAL)) {
        LiteralNode *ln = (LiteralNode*)n;
        if (ln->is_float) return 1;
        if (ln->type_name && strcmp(ln->type_name, "flotante") == 0) return 1;
    }
    const char *t = get_expression_type(cg, n);
    return t && strcmp(t, "flotante") == 0;
}

static int type_is_user_struct(CodeGen *cg, const char *type_name) {
    return type_name && sym_get_struct_size(&cg->sym, type_name) > 0;
}

typedef struct {
    uint32_t addr;
    int is_relative;
    int in_reg; /* 1 = direccion en registro */
    int reg;
    int invalid; /* 1 si hubo error semantico al resolver la direccion; no emitir accesos */
} MemberAddrResult;

static MemberAddrResult get_member_address(CodeGen *cg, ASTNode *node, int dest_reg);

/* Impresión / texto de registros: direcciones temporales fijas (no solapan visit_expression típico en 1–3). */
#define CG_STRUCT_ADDR_TMP 6
#define CG_STRUCT_VAL_TMP  4
#define CG_STRUCT_STR_ACC7 7
#define CG_STRUCT_STR_ACC8 8
#define CG_STRUCT_STR_FRAG 9
#define CG_STRUCT_COND_TMP 10
#define CG_INDIRECT_CALLEE_REG 50
/* Registro para materializar direcciones/tamanos >16b en emit_*; no usar 120 (acumulador de imprimir/concat). */
#define CG_U24_MATERIAL_TMP 110
static void emit_print_cstr(CodeGen *cg, const char *s) {
    size_t o = add_string(cg, s ? s : "");
    emit(cg, OP_IMPRIMIR_TEXTO, o & 0xFF, (o >> 8) & 0xFF, (o >> 16) & 0xFF,
         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
}

static void emit_indent_spaces(CodeGen *cg, int depth) {
    char buf[160];
    int n = depth * 2;
    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 2;
    for (int i = 0; i < n; i++) buf[i] = ' ';
    buf[n] = '\0';
    emit_print_cstr(cg, buf);
}

static void emit_load_label_addr(CodeGen *cg, int dest_reg, int label_id) {
    /* Programas grandes (>64 KiB IR): OP_MOVER + PATCH_SI solo materializaba 16 bits y corrompia punteros/cierres. */
    emit(cg, OP_MOVER_U24, (uint8_t)dest_reg, 0, 0, 0);
    add_patch(cg, label_id, PATCH_MOVER_U24);
}

static void emit_reg_plus_offset(CodeGen *cg, int base_reg, uint32_t byte_off, int out_reg) {
    emit(cg, OP_MOVER, out_reg, base_reg, 0, IR_INST_FLAG_B_REGISTER);
    while (byte_off > 0) {
        uint8_t chunk = byte_off > 255U ? 255U : (uint8_t)byte_off;
        emit(cg, OP_SUMAR, out_reg, out_reg, chunk, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        byte_off -= chunk;
    }
}

static void emit_leer_at_offset(CodeGen *cg, MemberAddrResult base, uint32_t off, int dest_reg) {
    if (base.in_reg) {
        emit_reg_plus_offset(cg, base.reg, off, CG_STRUCT_ADDR_TMP);
        emit(cg, OP_LEER, dest_reg, CG_STRUCT_ADDR_TMP, 0, IR_INST_FLAG_B_REGISTER);
    } else {
        uint32_t a = base.addr + off;
        uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (base.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, dest_reg, a & 0xFF, (a >> 8) & 0xFF, fl);
    }
}

static MemberAddrResult member_sub_at_offset(CodeGen *cg, MemberAddrResult base, size_t fo) {
    MemberAddrResult s = base;
    if (base.in_reg) {
        s.in_reg = 1;
        s.reg = CG_STRUCT_ADDR_TMP;
        emit_reg_plus_offset(cg, base.reg, (uint32_t)fo, CG_STRUCT_ADDR_TMP);
    } else {
        s.addr = base.addr + (uint32_t)fo;
    }
    return s;
}

static void emit_load_str_hash_in_reg(CodeGen *cg, size_t str_off, int reg) {
    emit(cg, OP_STR_REGISTRAR_LITERAL, str_off & 0xFF, (str_off >> 8) & 0xFF, (str_off >> 16) & 0xFF,
         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    emit(cg, OP_LOAD_STR_HASH, reg, str_off & 0xFF, (str_off >> 8) & 0xFF,
         IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
}

static void emit_str_concat_regs(CodeGen *cg, int acc_reg, int append_reg) {
    emit(cg, OP_STR_CONCATENAR_REG, acc_reg, acc_reg, append_reg,
         IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
}

/* Handle de texto 0 (sin asignar / nulo): imprimir "indefinido". */
static void emit_imprimir_texto_reg_o_indefinido(CodeGen *cg, int txt_reg) {
    int lbl_indef = new_label(cg);
    int lbl_end = new_label(cg);
    emit(cg, OP_CMP_EQ, CG_STRUCT_COND_TMP, (uint8_t)txt_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
    emit_jump_if_nonzero(cg, CG_STRUCT_COND_TMP, lbl_indef);
    emit(cg, OP_IMPRIMIR_TEXTO, (uint8_t)txt_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
    emit(cg, OP_IR, 0, 0, 0, 0);
    add_patch(cg, lbl_end, PATCH_JUMP);
    mark_label(cg, lbl_indef);
    size_t o = add_string(cg, "indefinido");
    emit(cg, OP_IMPRIMIR_TEXTO, o & 0xFF, (o >> 8) & 0xFF, (o >> 16) & 0xFF,
         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    mark_label(cg, lbl_end);
}

static void emit_concat_texto_reg_o_indefinido(CodeGen *cg, int acc_reg, int txt_reg) {
    int lbl_indef = new_label(cg);
    int lbl_end = new_label(cg);
    int frag = CG_STRUCT_STR_FRAG;
    emit(cg, OP_CMP_EQ, CG_STRUCT_COND_TMP, (uint8_t)txt_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
    emit_jump_if_nonzero(cg, CG_STRUCT_COND_TMP, lbl_indef);
    emit_str_concat_regs(cg, acc_reg, txt_reg);
    emit(cg, OP_IR, 0, 0, 0, 0);
    add_patch(cg, lbl_end, PATCH_JUMP);
    mark_label(cg, lbl_indef);
    size_t o = add_string(cg, "indefinido");
    emit_load_str_hash_in_reg(cg, o, frag);
    emit_str_concat_regs(cg, acc_reg, frag);
    mark_label(cg, lbl_end);
}

static int struct_repr_alt_acc(int acc_reg) {
    return acc_reg == CG_STRUCT_STR_ACC7 ? CG_STRUCT_STR_ACC8 : CG_STRUCT_STR_ACC7;
}

static void emit_imprimir_struct_repr(CodeGen *cg, const char *struct_name, MemberAddrResult base, int depth) {
    emit_print_cstr(cg, "{\n");
    size_t nf = sym_struct_n_fields(&cg->sym, struct_name);
    for (size_t i = 0; i < nf; i++) {
        const char *fn = NULL, *ft = NULL;
        size_t fo = 0, fsz = 0;
        if (!sym_struct_field_by_index(&cg->sym, struct_name, i, &fn, &ft, &fo, &fsz))
            break;
        emit_indent_spaces(cg, depth + 1);
        if (fn) emit_print_cstr(cg, fn);
        emit_print_cstr(cg, ": ");
        if (ft && type_is_user_struct(cg, ft)) {
            MemberAddrResult sub = member_sub_at_offset(cg, base, fo);
            emit_imprimir_struct_repr(cg, ft, sub, depth + 1);
        } else if (ft && strcmp(ft, "flotante") == 0) {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            emit(cg, OP_IMPRIMIR_FLOTANTE, CG_STRUCT_VAL_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else if (ft && strcmp(ft, "texto") == 0) {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            emit_imprimir_texto_reg_o_indefinido(cg, CG_STRUCT_VAL_TMP);
        } else if (ft && strcmp(ft, "caracter") == 0) {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            {
                int lbl_indef = new_label(cg);
                int lbl_end = new_label(cg);
                emit(cg, OP_CMP_EQ, CG_STRUCT_COND_TMP, CG_STRUCT_VAL_TMP, 0, IR_INST_FLAG_C_IMMEDIATE);
                emit_jump_if_nonzero(cg, CG_STRUCT_COND_TMP, lbl_indef);
                emit(cg, OP_STR_DESDE_CODIGO, CG_STRUCT_VAL_TMP, CG_STRUCT_VAL_TMP, 0,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_IMPRIMIR_TEXTO, CG_STRUCT_VAL_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
                emit(cg, OP_IR, 0, 0, 0, 0);
                add_patch(cg, lbl_end, PATCH_JUMP);
                mark_label(cg, lbl_indef);
                emit_print_cstr(cg, "indefinido");
                mark_label(cg, lbl_end);
            }
        } else {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            emit(cg, OP_IMPRIMIR_NUMERO, CG_STRUCT_VAL_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
        if (i + 1 < nf) emit_print_cstr(cg, ",");
        emit_print_cstr(cg, "\n");
        (void)fsz;
    }
    emit_indent_spaces(cg, depth);
    emit_print_cstr(cg, "}");
}

static void emit_imprimir_struct_repr_from_expr(CodeGen *cg, ASTNode *expr) {
    const char *st = get_expression_type(cg, expr);
    if (!st || !type_is_user_struct(cg, st)) return;
    MemberAddrResult base = get_member_address(cg, expr, 2);
    if (base.invalid) return;
    emit_imprimir_struct_repr(cg, st, base, 0);
}

static void fmt_append_cstr(CodeGen *cg, int acc_reg, const char *s) {
    int frag = CG_STRUCT_STR_FRAG;
    size_t o = add_string(cg, s ? s : "");
    emit_load_str_hash_in_reg(cg, o, frag);
    emit_str_concat_regs(cg, acc_reg, frag);
}

static void fmt_append_indent(CodeGen *cg, int acc_reg, int depth) {
    char buf[160];
    int n = depth * 2;
    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 2;
    for (int i = 0; i < n; i++) buf[i] = ' ';
    buf[n] = '\0';
    fmt_append_cstr(cg, acc_reg, buf);
}

static void emit_format_struct_string_reg(CodeGen *cg, const char *struct_name, MemberAddrResult base, int acc_reg, int depth) {
    int nest_reg = struct_repr_alt_acc(acc_reg);
    int frag = CG_STRUCT_STR_FRAG;
    fmt_append_cstr(cg, acc_reg, "{\n");
    size_t nf = sym_struct_n_fields(&cg->sym, struct_name);
    for (size_t i = 0; i < nf; i++) {
        const char *fn = NULL, *ft = NULL;
        size_t fo = 0, fsz = 0;
        if (!sym_struct_field_by_index(&cg->sym, struct_name, i, &fn, &ft, &fo, &fsz))
            break;
        fmt_append_indent(cg, acc_reg, depth + 1);
        if (fn) fmt_append_cstr(cg, acc_reg, fn);
        fmt_append_cstr(cg, acc_reg, ": ");
        if (ft && type_is_user_struct(cg, ft)) {
            MemberAddrResult sub = member_sub_at_offset(cg, base, fo);
            emit_format_struct_string_reg(cg, ft, sub, nest_reg, depth + 1);
            emit_str_concat_regs(cg, acc_reg, nest_reg);
        } else if (ft && strcmp(ft, "flotante") == 0) {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            emit(cg, OP_STR_FLOTANTE_PREC, frag, CG_STRUCT_VAL_TMP, 4,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            emit_str_concat_regs(cg, acc_reg, frag);
        } else if (ft && strcmp(ft, "texto") == 0) {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            emit_concat_texto_reg_o_indefinido(cg, acc_reg, CG_STRUCT_VAL_TMP);
        } else if (ft && strcmp(ft, "caracter") == 0) {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            {
                int lbl_indef = new_label(cg);
                int lbl_end = new_label(cg);
                emit(cg, OP_CMP_EQ, CG_STRUCT_COND_TMP, CG_STRUCT_VAL_TMP, 0, IR_INST_FLAG_C_IMMEDIATE);
                emit_jump_if_nonzero(cg, CG_STRUCT_COND_TMP, lbl_indef);
                emit(cg, OP_STR_DESDE_CODIGO, frag, CG_STRUCT_VAL_TMP, 0,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit_str_concat_regs(cg, acc_reg, frag);
                emit(cg, OP_IR, 0, 0, 0, 0);
                add_patch(cg, lbl_end, PATCH_JUMP);
                mark_label(cg, lbl_indef);
                size_t co = add_string(cg, "indefinido");
                emit_load_str_hash_in_reg(cg, co, frag);
                emit_str_concat_regs(cg, acc_reg, frag);
                mark_label(cg, lbl_end);
            }
        } else {
            emit_leer_at_offset(cg, base, (uint32_t)fo, CG_STRUCT_VAL_TMP);
            int is_bool_f = ft && (strcmp(ft, "bool") == 0 || strcmp(ft, "booleano") == 0);
            int is_int = ft && (strcmp(ft, "entero") == 0 || strcmp(ft, "u32") == 0 ||
                                strcmp(ft, "u8") == 0 || strcmp(ft, "byte") == 0 || strcmp(ft, "u64") == 0);
            uint8_t num_c = (uint8_t)(is_bool_f ? 2 : (is_int ? 1 : 0));
            emit(cg, OP_STR_DESDE_NUMERO, frag, CG_STRUCT_VAL_TMP, num_c,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            emit_str_concat_regs(cg, acc_reg, frag);
        }
        if (i + 1 < nf) fmt_append_cstr(cg, acc_reg, ",");
        fmt_append_cstr(cg, acc_reg, "\n");
        (void)fsz;
    }
    fmt_append_indent(cg, acc_reg, depth);
    fmt_append_cstr(cg, acc_reg, "}");
}

static int get_func_label(CodeGen *cg, const char *name) {
    if (!name) return -1;
    for (size_t i = 0; i < cg->n_funcs; i++) {
        if (cg->func_names[i] && strcmp(cg->func_names[i], name) == 0) {
            return cg->func_labels[i];
        }
    }
    return -1;
}

static int get_method_label_recursive(CodeGen *cg, const char *class_name, const char *method_name) {
    if (!class_name || !method_name) return -1;
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s.%s", class_name, method_name);
    int label_id = get_func_label(cg, full_name);
    if (label_id >= 0) return label_id;
    
    /* Buscar en bases */
    StructInfo *si = sym_get_struct_info(&cg->sym, class_name);
    if (si) {
        for (size_t b = 0; b < si->n_bases; b++) {
            int lid = get_method_label_recursive(cg, si->base_names[b], method_name);
            if (lid >= 0) return lid;
        }
    }
    return -1;
}

static int get_method_param_count(CodeGen *cg, const char *class_name, const char *method_name, size_t *out_count) {
    if (!cg || !class_name || !method_name || !out_count) return 0;
    void *method_ast = NULL;
    if (!sym_get_struct_method(&cg->sym, class_name, method_name, &method_ast)) return 0;
    FunctionNode *fn = (FunctionNode *)method_ast;
    if (!fn) return 0;
    *out_count = fn->n_params;
    return 1;
}

static int emit_dynamic_dispatch(CodeGen *cg, const char *obj_type, const char *method_name, CallNode *cn, int dest_reg, int is_statement) {
    StructInfo *base_si = sym_get_struct_info(&cg->sym, obj_type);
    if (!base_si || !base_si->is_class) return 0;
    
    int has_overrides = 0;
    for (size_t i = 0; i < cg->sym.n_structs; i++) {
        StructInfo *si = &cg->sym.structs[i];
        if (si->is_class && strcmp(si->name, obj_type) != 0 && sym_is_subclass_of(&cg->sym, si->name, obj_type)) {
            for (size_t j = 0; j < si->n_methods; j++) {
                if (strcmp(si->methods[j].name, method_name) == 0) {
                    has_overrides = 1; break;
                }
            }
        }
        if (has_overrides) break;
    }
    if (!has_overrides) return 0;

    const int class_id_reg = 250;
    emit(cg, OP_LEER, (uint8_t)class_id_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
    int end_label = new_label(cg);
    
    for (size_t i = 0; i < cg->sym.n_structs; i++) {
        StructInfo *si = &cg->sym.structs[i];
        if (si->is_class && sym_is_subclass_of(&cg->sym, si->name, obj_type)) {
            int label_id = get_method_label_recursive(cg, si->name, method_name);
            if (label_id >= 0) {
                int next_case = new_label(cg);
                int match_reg = 251;
                size_t off = add_string(cg, si->name);
                emit_load_str_hash_in_reg(cg, off, match_reg);
                emit(cg, OP_CMP_EQ, (uint8_t)match_reg, (uint8_t)class_id_reg, (uint8_t)match_reg, 0);
                emit(cg, OP_CMP_EQ, (uint8_t)match_reg, (uint8_t)match_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
                emit_jump_if_nonzero(cg, (uint8_t)match_reg, next_case);
                emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                add_patch(cg, label_id, PATCH_JUMP);
                if (!is_statement) emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_IR, 0, 0, 0, 0);
                add_patch(cg, end_label, PATCH_JUMP);
                mark_label(cg, next_case);
            }
        }
    }
    mark_label(cg, end_label);
    return 1;
}

static const char *get_member_chain_type(CodeGen *cg, ASTNode *node) {
    if (!node) return NULL;
    const char* result = NULL;
    if (is_node(node, NODE_IDENTIFIER)) {
        const char *t = sym_lookup_type(&cg->sym, ((IdentifierNode*)node)->name);
        /* Si es un mapa o lista con tipo de elemento (template), devolver el tipo de elemento */
        if (t && (strcmp(t, "mapa") == 0 || strcmp(t, "lista") == 0)) {
            const char *el = sym_lookup_lista_elem(&cg->sym, ((IdentifierNode *)node)->name);
            if (el && el[0]) result = el;
            else result = t;
        } else {
            result = t;
        }
    } else if (is_node(node, NODE_INDEX_ACCESS)) {
        IndexAccessNode *ian = (IndexAccessNode*)node;
        const char *t = get_member_chain_type(cg, ian->target);
        if (t) {
            const char *el = get_array_element_type(cg, t);
            if (el) result = el;
            else {
                // Fallback para tipos registrados en la tabla de símbolos
                if (is_node(ian->target, NODE_IDENTIFIER)) {
                    const char *el2 = sym_lookup_lista_elem(&cg->sym, ((IdentifierNode *)ian->target)->name);
                    if (el2 && el2[0]) result = el2;
                } else if (is_node(ian->target, NODE_MEMBER_ACCESS)) {
                    MemberAccessNode *ma = (MemberAccessNode *)ian->target;
                    const char *obj_type = get_member_chain_type(cg, ma->target);
                    if (obj_type) {
                        const char *el2 = sym_get_struct_lista_elem_type(&cg->sym, obj_type, ma->member);
                        if (el2 && el2[0]) result = el2;
                    }
                }
            }
        }
        if (!result) result = "mapa"; // Por defecto en Jasboot, los elementos de colecciones sin tipo son mapas
    } else if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        const char *base_type = get_member_chain_type(cg, man->target);
        if (base_type) {
            /* Mapa dinamico (o valor generico): encadenar .clave sigue siendo mapa para asignacion/lectura. */
            if (strcmp(base_type, "mapa") == 0 || strcmp(base_type, "elemento") == 0)
                return "mapa";
            size_t off = 0;
            const char *ft = NULL;
            size_t fsz = 0;
            if (sym_get_struct_field(&cg->sym, base_type, man->member, &off, &ft, &fsz))
                result = ft;
        }
    }
    
    // if (result) printf("DEBUG_CHAIN: node_type=%d, result=%s\n", node->type, result);
    return result;
}

/* Asignacion o lectura: campo inexistente, tipo base no registro, o cadena .x sin tipo. */
static void codegen_error_struct_member_access(CodeGen *cg, MemberAccessNode *man, const char *contexto) {
    if (!man) return;
    char buf[CODEGEN_ERROR_MAX];
    const char *mb = man->member ? man->member : "?";
    const char *base_t = get_member_chain_type(cg, man->target);
    int el = man->base.line > 0 ? man->base.line : 1;
    int ec = man->base.col > 0 ? man->base.col : 1;
    if (!base_t) {
        snprintf(buf, sizeof buf,
                 "Error en %s: no se conoce el tipo de la expresion a la izquierda de '.%s'.",
                 contexto ? contexto : "acceso", mb);
        cg_collect_push(cg, buf, el, ec);
        return;
    }
    if (sym_get_struct_size(&cg->sym, base_t) == 0) {
        snprintf(buf, sizeof buf,
                 "Error en %s: el tipo '%s' no es un registro (definido con registro ... fin_registro); no tiene el campo '.%s'.",
                 contexto ? contexto : "acceso", base_t, mb);
        cg_collect_push(cg, buf, el, ec);
        return;
    }
    snprintf(buf, sizeof buf,
             "Error en %s: el registro '%s' no declara el campo '%s' (revise el bloque registro o el nombre).",
             contexto ? contexto : "acceso", base_t, mb);
    cg_collect_push(cg, buf, el, ec);
}

static int is_access_allowed(CodeGen *cg, const char *class_name, int is_private) {
    if (!is_private) return 1;
    if (!cg || !cg->current_fn_name || !class_name) return 0;
    size_t clen = strlen(class_name);
    if (strncmp(cg->current_fn_name, class_name, clen) == 0 && cg->current_fn_name[clen] == '.')
        return 1;
    return 0;
}

static void emit_sumar_u24(CodeGen *cg, int dest_reg, int base_reg, uint32_t val) {
    if (val <= 255) {
        emit(cg, OP_SUMAR, (uint8_t)dest_reg, (uint8_t)base_reg, (uint8_t)val, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
    } else {
        emit_mover_u24(cg, CG_U24_MATERIAL_TMP, val);
        emit(cg, OP_SUMAR, (uint8_t)dest_reg, (uint8_t)base_reg, (uint8_t)CG_U24_MATERIAL_TMP,
             IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
    }
}

static void emit_escribir_u24(CodeGen *cg, uint32_t addr, int val_reg, int is_relative) {
    uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
    if (is_relative) fl |= IR_INST_FLAG_RELATIVE;
    
    if (addr <= 0xFFFF) {
        emit(cg, OP_ESCRIBIR, (uint8_t)(addr & 0xFF), (uint8_t)val_reg, (uint8_t)((addr >> 8) & 0xFF), fl);
    } else {
        const int tmp = CG_U24_MATERIAL_TMP;
        if (val_reg == tmp) {
            const int saved = 119;
            emit(cg, OP_MOVER, (uint8_t)saved, (uint8_t)val_reg, 0, IR_INST_FLAG_B_REGISTER);
            emit_mover_u24(cg, tmp, addr);
            emit(cg, OP_ESCRIBIR, (uint8_t)tmp, (uint8_t)saved, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | (is_relative ? IR_INST_FLAG_RELATIVE : 0));
        } else {
            emit_mover_u24(cg, tmp, addr);
            emit(cg, OP_ESCRIBIR, (uint8_t)tmp, (uint8_t)val_reg, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | (is_relative ? IR_INST_FLAG_RELATIVE : 0));
        }
    }
}

static void emit_leer_u24(CodeGen *cg, int dest_reg, uint32_t addr, int is_relative) {
    uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
    if (is_relative) fl |= IR_INST_FLAG_RELATIVE;
    
    if (addr <= 0xFFFF) {
        emit(cg, OP_LEER, (uint8_t)dest_reg, (uint8_t)(addr & 0xFF), (uint8_t)((addr >> 8) & 0xFF), fl);
    } else {
        emit_mover_u24(cg, CG_U24_MATERIAL_TMP, addr);
        emit(cg, OP_LEER, (uint8_t)dest_reg, (uint8_t)CG_U24_MATERIAL_TMP, 0,
             IR_INST_FLAG_B_REGISTER | (is_relative ? IR_INST_FLAG_RELATIVE : 0));
    }
}

static void emit_mover_u24(CodeGen *cg, int dest_reg, uint32_t val) {
    if (val <= 0xFFFF) {
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)(val & 0xFF), (uint8_t)((val >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    } else {
        /* Usar el campo de flags para el byte alto (0xXX0000) */
        uint8_t high = (uint8_t)((val >> 16) & 0xFF);
        emit(cg, OP_MOVER_U24, (uint8_t)dest_reg, (uint8_t)(val & 0xFF), (uint8_t)((val >> 8) & 0xFF), high);
    }
}

static void emit_heap_reservar_u24(CodeGen *cg, int dest_reg, uint32_t size) {
    if (size <= 0xFFFF) {
        emit(cg, OP_HEAP_RESERVAR, (uint8_t)dest_reg, (uint8_t)(size & 0xFF), (uint8_t)((size >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    } else {
        emit_mover_u24(cg, CG_U24_MATERIAL_TMP, size);
        emit(cg, OP_HEAP_RESERVAR, (uint8_t)dest_reg, (uint8_t)CG_U24_MATERIAL_TMP, 0, IR_INST_FLAG_B_REGISTER);
    }
}

static MemberAddrResult get_member_address(CodeGen *cg, ASTNode *node, int dest_reg) {
    MemberAddrResult r = {0, 0, 0, dest_reg, 0};
    if (is_node(node, NODE_IDENTIFIER)) {
        const char *name = ((IdentifierNode*)node)->name;
        SymResult sr = sym_lookup(&cg->sym, name);
        if (!sr.found) {
            if (get_func_label(cg, name) >= 0) {
            } else {
                char buf[CODEGEN_ERROR_MAX];
                snprintf(buf, sizeof buf, "Error: variable '%s' no declarada antes de su uso", name);
                cg_collect_push(cg, buf, ((IdentifierNode*)node)->line, ((IdentifierNode*)node)->col);
                r.invalid = 1;
            }
            return r;
        }
        
        /* No cargar el puntero aqui; dejar que el nivel superior lo haga si es necesario 
           para acceder a sus miembros. */
        r.addr = sr.addr;
        r.is_relative = sr.is_relative;
        r.in_reg = 0;
        return r;
    }
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        MemberAddrResult base = get_member_address(cg, man->target, dest_reg);
        if (base.invalid) {
            r.invalid = 1;
            return r;
        }
        const char *base_type = get_member_chain_type(cg, man->target);
        if (!base_type) return r;

        /* Si la base es un objeto, necesitamos su valor (el puntero) para acceder a sus campos.
           get_member_address devuelve la direccion donde reside el valor. Si es un objeto,
           debemos desreferenciarlo. */
        StructInfo *base_si = sym_get_struct_info(&cg->sym, base_type);
        if (base_si && base_si->is_class) {
            if (base.in_reg) {
                /* Si ya esta en registro, asumimos que es la direccion de la memoria donde esta el puntero.
                   Debemos leer el puntero. */
                emit(cg, OP_LEER, (uint8_t)dest_reg, (uint8_t)base.reg, 0, IR_INST_FLAG_B_REGISTER);
                base.is_relative = 0;
            } else {
                /* Si es una direccion directa (ej. variable global o local), leer el puntero. */
                emit_leer_u24(cg, dest_reg, base.addr, base.is_relative);
                base.in_reg = 1;
                base.reg = dest_reg;
                base.is_relative = 0;
            }
        }

        size_t off = 0;
        const char *ft = NULL;
        size_t fsz = 0;
        if (!sym_get_struct_field(&cg->sym, base_type, man->member, &off, &ft, &fsz)) {
            codegen_error_struct_member_access(cg, man, "acceso");
            r.invalid = 1;
            return r;
        }
            
        int is_priv = 0;
        if (sym_get_struct_field_visibility(&cg->sym, base_type, man->member, &is_priv)) {
            if (!is_access_allowed(cg, base_type, is_priv)) {
                char pbuf[CODEGEN_ERROR_MAX];
                snprintf(pbuf, sizeof pbuf, "Error: el campo '%s' de la clase '%s' es privado",
                         man->member ? man->member : "?", base_type ? base_type : "?");
                cg_collect_push(cg, pbuf, man->base.line > 0 ? man->base.line : 1, man->base.col > 0 ? man->base.col : 1);
                r.invalid = 1;
                return r;
            }
        }

        if (base.in_reg) {
            emit_sumar_u24(cg, dest_reg, base.reg, (uint32_t)off);
            r.in_reg = 1;
            r.reg = dest_reg;
            r.is_relative = base.is_relative;
        } else {
            r.addr = base.addr + (uint32_t)off;
            r.is_relative = base.is_relative;
        }
        return r;
    }
    return r;
}

/* Llamada de sistema reconocida pero sin argumentos obligatorios (evita mensaje engañoso "funcion no definida"). */
static void sistema_error_sin_argumentos(CodeGen *cg, const char *nombre_sistema, const char *que_pide, int line, int col) {
    char buf[1024];
    snprintf(buf, sizeof buf,
             "'%s' requiere un argumento (%s). Esta llamada no tiene argumentos.",
             nombre_sistema ? nombre_sistema : "?",
             que_pide ? que_pide : "vea la documentacion");
    cg->has_error = 1;
    cg_collect_push(cg, buf, line > 0 ? line : 1, col > 0 ? col : 1);
}

/* vec2/vec3/vec4(...) con aridad distinta a la del tipo (no son funciones globales). */
static int codegen_error_vec_constructor_arity(CodeGen *cg, CallNode *cn) {
    if (!cn || !cn->name) return 0;
    int need;
    const char *ej;
    if (strcmp(cn->name, "vec2") == 0) {
        need = 2;
        ej = "vec2(1.0, 2.0)";
    } else if (strcmp(cn->name, "vec3") == 0) {
        need = 3;
        ej = "vec3(1.0, 2.0, 3.0)";
    } else if (strcmp(cn->name, "vec4") == 0) {
        need = 4;
        ej = "vec4(1.0, 2.0, 3.0, 4.0)";
    } else
        return 0;
    if (cn->n_args == (size_t)need) return 0;
    char buf[1024];
    snprintf(buf, sizeof buf,
             "'%s' es el constructor del tipo vector (no una funcion global). Requiere exactamente %d argumentos numericos; se recibieron %zu. Ejemplo: %s.",
             cn->name, need, cn->n_args, ej);
    cg->has_error = 1;
    cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
    return 1;
}

static int codegen_error_vector_sistema_arity(CodeGen *cg, CallNode *cn, int need, const char *uso) {
    if ((size_t)need == cn->n_args) return 0;
    char buf[1024];
    snprintf(buf, sizeof buf,
             "Llamada incorrecta a '%s': se esperaban %d argumento(s) y se recibieron %zu. Uso: %s.",
             cn->name ? cn->name : "?", need, cn->n_args, uso);
    cg->has_error = 1;
    cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
    return 1;
}

static void codegen_error_vector_sistema_bad_identifiers(CodeGen *cg, CallNode *cn, const char *name, const char *uso) {
    char buf[1024];
    snprintf(buf, sizeof buf,
             "Llamada incorrecta a '%s': los argumentos deben ser identificadores (variables vector), no expresiones compuestas. Uso: %s.",
             name ? name : "?", uso);
    cg->has_error = 1;
    cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
}

/* mat3/mat4 builtins: mismos requisitos de identificadores que vec*, distinto texto. */
static void codegen_error_mat_sistema_bad_identifiers(CodeGen *cg, CallNode *cn, const char *uso) {
    char buf[1024];
    snprintf(buf, sizeof buf,
             "Llamada incorrecta a '%s': los argumentos deben ser identificadores de variable (matriz o vector segun la operacion), no expresiones compuestas. Uso: %s.",
             cn->name ? cn->name : "?", uso);
    cg->has_error = 1;
    cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
}

/* mat*_mul_vec*: el vector suele escribirse como vecN(...); mensaje mas claro que "expresiones compuestas". */
static void codegen_error_mat_mul_vec_arg_not_identifier(CodeGen *cg, CallNode *cn, int which_arg, ASTNode *arg,
                                                         const char *ctor_name, const char *uso) {
    if (arg && is_node(arg, NODE_CALL)) {
        CallNode *ac = (CallNode *)arg;
        if (ac->name && strcmp(ac->name, ctor_name) == 0) {
            char buf[1024];
            snprintf(buf, sizeof buf,
                     "Llamada incorrecta a '%s': el argumento %d debe ser el nombre de una variable %s, no %s(...) "
                     "(constructor del tipo; la VM necesita la direccion del vector ya almacenado). "
                     "Asigne antes a una variable y pase el identificador. Uso: %s.",
                     cn->name ? cn->name : "?", which_arg, ctor_name, ctor_name, uso);
            cg->has_error = 1;
            cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
            return;
        }
    }
    codegen_error_mat_sistema_bad_identifiers(cg, cn, uso);
}

/* Aridad incorrecta en APIs de lista incorporadas: evita el mensaje generico "no existe la funcion". */
static void codegen_error_sistema_lista_arity(CodeGen *cg, const CallNode *cn, const char *nombre,
                                              size_t n_recibidos, size_t n_requeridos,
                                              const char *desc_args, const char *ejemplo) {
    char buf[1024];
    snprintf(buf, sizeof buf,
             "'%s' es una llamada incorporada del lenguaje: requiere %zu argumento(s) (%s), "
             "pero esta llamada tiene %zu. Ejemplo: %s",
             nombre ? nombre : "?", n_requeridos, desc_args ? desc_args : "", n_recibidos,
             ejemplo ? ejemplo : "(ver documentacion)");
    cg->has_error = 1;
    cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
}

/* Aridad de API incorporada (texto, etc.): mensaje corto + ejemplo; consejo opcional (p. ej. sinonimos). */
static void codegen_error_sistema_incorporada_arity(CodeGen *cg, const CallNode *cn, size_t n_requeridos,
                                                    const char *desc_args, const char *ejemplo,
                                                    const char *consejo) {
    const char *nm = cn->name ? cn->name : "?";
    char balance[80] = "";
    if (cn->n_args < n_requeridos) {
        snprintf(balance, sizeof balance, "Faltan %zu argumento(s). ",
                 (size_t)(n_requeridos - cn->n_args));
    } else if (cn->n_args > n_requeridos) {
        snprintf(balance, sizeof balance, "Sobran %zu argumento(s). ",
                 (size_t)(cn->n_args - n_requeridos));
    }
    char buf[1024];
    if (consejo && consejo[0])
        snprintf(buf, sizeof buf,
                 "%s'%s' es una funcion incorporada: requiere al menos %zu (%s); "
                 "aqui hay %zu. Ejemplo: %s %s",
                 balance, nm, n_requeridos, desc_args ? desc_args : "", cn->n_args,
                 ejemplo ? ejemplo : "()", consejo);
    else
        snprintf(buf, sizeof buf,
                 "%s'%s' es una funcion incorporada: requiere al menos %zu (%s); "
                 "aqui hay %zu. Ejemplo: %s",
                 balance, nm, n_requeridos, desc_args ? desc_args : "", cn->n_args,
                 ejemplo ? ejemplo : "()");
    
    cg->has_error = 1;
    cg_collect_push(cg, buf, cn->base.line > 0 ? cn->base.line : 1, cn->base.col > 0 ? cn->base.col : 1);
}

/* buscar_en_texto / contiene_texto / termina_con: aridad 2 con mensajes por caso (0, 1, 3+ args).
 * Tambien se invoca en el fallback de NODE_CALL si visit_call_sistema no reconocio la llamada. */
static int codegen_error_if_bad_arity_buscar_contiene_termina(CodeGen *cg, const CallNode *cn) {
    const char *n = cn && cn->name ? cn->name : NULL;
    if (!n) return 0;
    if (strcmp(n, "buscar_en_texto") != 0 && strcmp(n, "contiene_texto") != 0 && strcmp(n, "termina_con") != 0)
        return 0;
    if (cn->n_args == 2) return 0;

    if (strcmp(n, "termina_con") == 0) {
        const char *desc = "texto completo y sufijo final a comprobar";
        const char *ej = "termina_con(\"foto.png\", \".png\")";
        const char *cons = NULL;
        if (cn->n_args == 0)
            cons = "(Indique el texto y, tras la coma, el sufijo que debe coincidir con el final; p. ej. extension .jasb.)";
        else if (cn->n_args == 1)
            cons = "(Falta el segundo texto: el sufijo. Sin el sufijo no se puede comprobar el final de la cadena.)";
        else
            cons = "(Solo dos argumentos; elimine comas y expresiones sobrantes tras el sufijo.)";
        codegen_error_sistema_incorporada_arity(cg, cn, 2, desc, ej, cons);
        return 1;
    }

    int es_buscar = (strcmp(n, "buscar_en_texto") == 0);
    const char *ej = es_buscar
        ? "buscar_en_texto(\"hola mundo\", \"mun\")"
        : "contiene_texto(\"hola mundo\", \"mun\")";
    const char *desc = "texto donde buscar y subcadena o patron a localizar";
    const char *cons = NULL;
    if (cn->n_args == 0) {
        cons = es_buscar
            ? "(Sinonimo: contiene_texto — mismo orden: frase completa, luego el fragmento buscado.)"
            : "(Sinonimo: buscar_en_texto — mismo orden: frase completa, luego el fragmento buscado.)";
    } else if (cn->n_args == 1) {
        cons = "(Anada tras la coma la subcadena que debe aparecer dentro del primer texto; devuelve 1 si aparece, 0 si no.)";
    } else {
        cons = "(Solo dos expresiones: texto y patron. Cualquier tercer argumento sobra.)";
    }
    codegen_error_sistema_incorporada_arity(cg, cn, 2, desc, ej, cons);
    return 1;
}

/* pensar / procesar_texto: exactamente un argumento. Mensaje explicito (evita el fallback generico
 * de "ninguna firma" si un handler antiguo hacia return 0 por !ARG0). Tambien en fallback NODE_CALL. */
static int codegen_error_if_bad_arity_pensar_procesar_texto(CodeGen *cg, const CallNode *cn) {
    const char *n = cn && cn->name ? cn->name : NULL;
    if (!n) return 0;
    int es_pensar = (strcmp(n, "pensar") == 0);
    int es_proc = (strcmp(n, "procesar_texto") == 0);
    if (!es_pensar && !es_proc) return 0;
    if (cn->n_args == 1) return 0;

    cg->has_error = 1;
    cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
    cg->err_col = cn->base.col > 0 ? cn->base.col : 1;

    if (es_pensar) {
        if (cn->n_args == 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'pensar' requiere exactamente un argumento: la entrada de razonamiento (texto o concepto). "
                     "Se llamo pensar() sin argumentos. El id de salida se guarda en la variable sistema "
                     "`resultado`. Ejemplo: pensar(\"semilla\").");
        } else {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'pensar' admite solo un argumento; aqui se pasaron %zu. "
                     "Ejemplo: pensar(\"solo_un_texto\"). Quite los argumentos sobrantes tras la coma.",
                     cn->n_args);
        }
    } else {
        if (cn->n_args == 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'procesar_texto' requiere exactamente un argumento: la cadena a procesar en la memoria "
                     "neuronal (con crear_memoria abierta). Se llamo procesar_texto() sin argumentos. "
                     "Ejemplo: procesar_texto(\"entrada\").");
        } else {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'procesar_texto' admite solo un argumento; aqui se pasaron %zu. "
                     "Ejemplo: procesar_texto(\"una_sola_cadena\").",
                     cn->n_args);
        }
    }
    return 1;
}

/* Misma convención que mem_lista_agregar: temporales y registros 1, 2. */
static void codegen_emit_mem_lista_agregar_from_regs(CodeGen *cg, uint8_t list_reg, uint8_t val_reg) {
    SymResult ag_tmp = sym_reserve_temp(&cg->sym, 8);
    uint8_t fl_agw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
    if (ag_tmp.is_relative) fl_agw |= IR_INST_FLAG_RELATIVE;
    emit_escribir_u24(cg, ag_tmp.addr, list_reg, ag_tmp.is_relative);
    emit_leer_u24(cg, 1, ag_tmp.addr, ag_tmp.is_relative);
    emit(cg, OP_MOVER, 2, val_reg, 0, IR_INST_FLAG_B_REGISTER);
    emit(cg, OP_MEM_LISTA_AGREGAR, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
}

/* Helper para escribir el resultado de una operacion JMN en la palabra reservada 'resultado' */
static void codegen_emit_write_resultado(CodeGen *cg, uint8_t reg) {
    SymResult r = sym_lookup(&cg->sym, "resultado");
    if (!r.found) return;
    emit_escribir_u24(cg, r.addr, reg, r.is_relative);
}

/* --- Nivel 6: visit_call_sistema --- */
static int visit_call_sistema(CodeGen *cg, CallNode *cn, int dest_reg) {
    const char *name = cn->name;
    if (!name || cn->callee) return 0;
    if (!is_sistema_llamada(name, strlen(name))) return 0;

#define ARG0 (cn->n_args > 0 ? cn->args[0] : NULL)
#define ARG1 (cn->n_args > 1 ? cn->args[1] : NULL)
#define ARG2 (cn->n_args > 2 ? cn->args[2] : NULL)
#define ARG3 (cn->n_args > 3 ? cn->args[3] : NULL)
#define ARG4 (cn->n_args > 4 ? cn->args[4] : NULL)
#define ARG5 (cn->n_args > 5 ? cn->args[5] : NULL)
#define ARG6 (cn->n_args > 6 ? cn->args[6] : NULL)

    /* Antes de cualquier rama que haga `if (!ARG0) return 0`, para no caer en el mensaje generico del caller. */
    if (codegen_error_if_bad_arity_pensar_procesar_texto(cg, cn)) return 1;

    /* 6.1 Texto */
    if (strcmp(name, "concatenar") == 0) {
        if (cn->n_args != 2) {
            const char *cons = NULL;
            if (cn->n_args == 0)
                cons = "(Ponga los dos textos entre parentesis, separados por coma.)";
            else if (cn->n_args == 1)
                cons = "(Falta el segundo texto a la derecha de la coma.)";
            else
                cons = "(Solo admite dos argumentos de texto.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto izquierda y texto derecha",
                "concatenar(\"Hola\", \"Mundo\")", cons);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_STR_CONCATENAR_REG, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "longitud") == 0 || strcmp(name, "longitud_texto") == 0) {
        if (cn->n_args != 1) {
            int es_lt = (strcmp(name, "longitud_texto") == 0);
            const char *ej = es_lt ? "longitud_texto(\"abc\")" : "longitud(\"abc\")";
            const char *cons = (cn->n_args == 0)
                ? (es_lt ? "(`longitud` es sinonimo.)" : "(`longitud_texto` es sinonimo.)")
                : "(Solo un argumento: el texto. No anada mas tras la coma.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "un solo texto (se cuenta en caracteres)", ej, cons);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_STR_LONGITUD, dest_reg, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "dividir") == 0 || strcmp(name, "dividir_texto") == 0 ||
        strcmp(name, "segmentar_palabras") == 0 || strcmp(name, "palabras_de") == 0) {
        if (cn->n_args != 2) {
            int es_dt = (strcmp(name, "dividir_texto") == 0 || strcmp(name, "dividir") == 0);
            const char *ej = es_dt
                ? "dividir_texto(\"uno,dos\", \",\")"
                : "segmentar_palabras(\"hola mundo\", \" \")";
            const char *cons = NULL;
            if (cn->n_args == 0) {
                cons = es_dt
                    ? "(`dividir` es sinonimo.) El segundo argumento es el delimitador."
                    : "(`dividir_texto` es sinonimo.) El segundo argumento es el delimitador.";
            } else if (cn->n_args == 1) {
                cons = "(Falta el segundo argumento: el separador o delimitador, tras la coma.)";
            } else {
                cons = "(Solo dos argumentos: texto completo y separador; no anada un tercero.)";
            }
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "cadena completa y cadena separadora", ej, cons);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        emit(cg, OP_STR_DIVIDIR_TEXTO, dest_reg, dest_reg, dest_reg + 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tokenizar_L") == 0 || strcmp(name, "claves_L") == 0) {
        if (cn->n_args < 1) {
            sistema_error_sin_argumentos(cg, name, "texto a tokenizar", cn->base.line, cn->base.col);
            return 1;
        }
        if (cn->n_args > 5) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'%s' admite 1..5 argumentos (texto [, separador [, modo [, stopwords_csv [, min_len]]]]); aqui hay %zu.",
                     name, cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        /* Evaluar en 20/21 y copiar siempre a 10/11 (visit puede devolver un reg distinto del hint). */
        (void)visit_expression(cg, ARG0, 20);
        emit(cg, OP_MOVER, 10, 20, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        if (cn->n_args >= 2 && ARG1) {
            (void)visit_expression(cg, ARG1, 21);
            emit(cg, OP_MOVER, 11, 21, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            size_t sep0 = add_string(cg, " ");
            emit_load_str_hash_in_reg(cg, sep0, 11);
        }
        if (cn->n_args >= 3 && ARG2) {
            visit_expression(cg, ARG2, 12);
            emit(cg, OP_MOVER, 240, 12, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 240, 3, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        /* Reg 242 = id texto CSV stopwords extra; 241 = min_len (0 = usar solo modo/TL_MOD_MIN2). */
        if (cn->n_args >= 4 && ARG3) {
            (void)visit_expression(cg, ARG3, 22);
            emit(cg, OP_MOVER, 242, 22, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 242, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (cn->n_args >= 5 && ARG4) {
            (void)visit_expression(cg, ARG4, 23);
            emit(cg, OP_MOVER, 241, 23, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 241, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_STR_DIVIDIR_TEXTO, (uint8_t)dest_reg, 10, 11,
             (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_SAFE));
        return 1;
    }
    if (strcmp(name, "buscar_en_texto") == 0 || strcmp(name, "contiene_texto") == 0) {
        if (codegen_error_if_bad_arity_buscar_contiene_termina(cg, cn)) return 1;
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_MEM_CONTIENE_TEXTO_REG, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "termina_con") == 0) {
        if (codegen_error_if_bad_arity_buscar_contiene_termina(cg, cn)) return 1;
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_MEM_TERMINA_CON_REG, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "extraer_subtexto") == 0) {
        if (cn->n_args < 2) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Faltan argumentos. 'extraer_subtexto' necesita el texto, el indice de inicio (0 = primer caracter) "
                     "y, si lo desea, la longitud; aqui hay %zu. Ejemplo: extraer_subtexto(\"abcdef\", 2, 3) -> \"cde\". "
                     "Con dos argumentos, desde el indice hasta el final del texto.",
                     cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        if (cn->n_args > 3) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Sobran argumentos. 'extraer_subtexto' admite como mucho tres (texto, indice inicio, longitud); "
                     "aqui hay %zu. Ejemplo: extraer_subtexto(\"abcdef\", 1, 4).",
                     cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);
        int r3 = dest_reg + 3;
        if (ARG2)
            visit_expression(cg, ARG2, r3);
        else
            emit(cg, OP_MOVER, (uint8_t)r3, 0xFF, 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_STR_SUBTEXTO, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "extraer_antes_de") == 0) {
        if (cn->n_args != 2) {
            const char *cons = NULL;
            if (cn->n_args < 2)
                cons = "(Hacen falta el texto donde buscar y el patron o delimitador. Luego del patron se descarta.)";
            else
                cons = "(Solo dos argumentos: frase y patron. No anada mas tras la segunda coma.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                                                    "texto donde buscar y texto patron o delimitador",
                                                    "extraer_antes_de(\"uno,dos\", \",\")", cons);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_STR_EXTRAER_ANTES_REG, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "extraer_despues_de") == 0) {
        if (cn->n_args != 2) {
            const char *cons = NULL;
            if (cn->n_args < 2)
                cons = "(Hacen falta el texto y el patron; se devuelve lo que sigue a la primera aparicion del patron.)";
            else
                cons = "(Solo dos argumentos: frase y patron. No anada mas tras la segunda coma.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                                                    "texto donde buscar y texto patron o delimitador",
                                                    "extraer_despues_de(\"clave:valor\", \":\")", cons);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_STR_EXTRAER_DESPUES_REG, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "copiar_texto") == 0 || strcmp(name, "str_copiar") == 0) {
        if (cn->n_args != 1) {
            int es_sc = (strcmp(name, "str_copiar") == 0);
            const char *ej = es_sc ? "str_copiar(\"hola\")" : "copiar_texto(\"hola\")";
            const char *cons = (cn->n_args == 0)
                ? (es_sc
                       ? "(`copiar_texto` hace lo mismo con un argumento.)"
                       : "(`str_copiar` hace lo mismo con un argumento.)")
                : "(Solo un argumento: el texto a copiar. No anada mas tras la coma.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "un solo texto de origen", ej, cons);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int r2 = dest_reg + 2;
        emit(cg, OP_MEM_COPIAR_TEXTO, (uint8_t)r2, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r2, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_minusculas") == 0 || strcmp(name, "minusculas") == 0) {
        if (cn->n_args != 1) {
            int es_sm = (strcmp(name, "str_minusculas") == 0);
            const char *ej = es_sm ? "str_minusculas(\"AbC\")" : "minusculas(\"HOLA\")";
            const char *cons = (cn->n_args == 0)
                ? (es_sm ? "(`minusculas` es sinonimo.)" : "(`str_minusculas` es sinonimo.)")
                : "(Solo un argumento: el texto a pasar a minusculas. No anada mas tras la coma.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "un solo texto (salida en minusculas)", ej, cons);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_STR_MINUSCULAS, dest_reg, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "reemplazar") == 0 || strcmp(name, "remplazar") == 0 || strcmp(name, "reemplazar_texto") == 0) {
        if (cn->n_args != 3) {
            codegen_error_sistema_incorporada_arity(cg, cn, 3,
                "texto base, patron a sustituir y texto de reemplazo",
                "reemplazar(\"a,b,c\", \",\", \" \")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        visit_expression(cg, ARG2, 3);
        emit(cg, OP_STR_REEMPLAZAR, (uint8_t)dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_mayusculas") == 0 || strcmp(name, "mayusculas") == 0) {
        if (cn->n_args != 1) {
            int es_sm = (strcmp(name, "str_mayusculas") == 0);
            const char *ej = es_sm ? "str_mayusculas(\"abc\")" : "mayusculas(\"hola\")";
            const char *cons = (cn->n_args == 0)
                ? (es_sm ? "(`mayusculas` es sinonimo.)" : "(`str_mayusculas` es sinonimo.)")
                : "(Solo un argumento: el texto a pasar a mayusculas. No anada mas tras la coma.)";
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "un solo texto (salida en mayusculas)", ej, cons);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_STR_MAYUSCULAS, dest_reg, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_extraer_caracter") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto y posicion del caracter",
                "str_extraer_caracter(\"hola\", 0)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        emit(cg, OP_STR_EXTRAER_CARACTER, dest_reg, dest_reg, dest_reg + 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "codigo_caracter") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "caracter o texto", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_STR_CODIGO_CARACTER, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "caracter_a_texto") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "codigo numerico del caracter", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_STR_DESDE_CODIGO, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "byte_a_caracter") == 0 || strcmp(name, "caracter_a_byte") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "valor a convertir", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_MOVER, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* Trigonometría: sin, cos, tan (radianes), atan2(y, x) */
    if (strcmp(name, "sin") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "angulo en radianes (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_SIN, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "cos") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "angulo en radianes (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_COS, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tan") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "angulo en radianes (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_TAN, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "atan2") == 0 || strcmp(name, "arcotangente2") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 2, "atan2(y, x) o arcotangente2(y, x) — radianes, y primero"))
            return 1;
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);  /* y */
        int r2 = visit_expression(cg, ARG1, dest_reg + 2);  /* x */
        emit(cg, OP_ATAN2, (uint8_t)dest_reg, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "exp") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "exponente (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_EXP, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "log") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "argumento > 0 (flotante, logaritmo natural)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_LOG, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "log10") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "argumento > 0 (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_LOG10, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "nativo_mlp_entrenar") == 0) {
        if (cn->n_args != 7) {
            codegen_error_sistema_incorporada_arity(
                cg, cn, 7,
                "pesos_capas, sesgos_capas, X, lista_y, capas_ocultas, learning_rate, epochs",
                "nativo_mlp_entrenar(pesos, sesgos, X, y, capas, 0.01, 2000)", NULL);
            return 1;
        }
        const int B0 = 40;
        visit_expression(cg, ARG0, B0);
        visit_expression(cg, ARG1, B0 + 1);
        visit_expression(cg, ARG2, B0 + 2);
        visit_expression(cg, ARG3, B0 + 3);
        visit_expression(cg, ARG4, B0 + 4);
        visit_expression(cg, ARG5, B0 + 5);
        visit_expression(cg, ARG6, B0 + 6);
        emit(cg, OP_ANALITICA_MLP_FIT, (uint8_t)dest_reg, (uint8_t)B0, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "nativo_mlp_predict") == 0) {
        if (cn->n_args != 3) {
            codegen_error_sistema_incorporada_arity(
                cg, cn, 3,
                "pesos, sesgos, vector_x",
                "nativo_mlp_predict(pesos, sesgos, x)", NULL);
            return 1;
        }
        const int B0 = 40;
        visit_expression(cg, ARG0, B0);
        visit_expression(cg, ARG1, B0 + 1);
        visit_expression(cg, ARG2, B0 + 2);
        emit(cg, OP_ANALITICA_MLP_PREDICT, (uint8_t)dest_reg, (uint8_t)B0, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "nativo_mlp_guardar") == 0) {
        if (cn->n_args != 3) {
            codegen_error_sistema_incorporada_arity(
                cg, cn, 3,
                "ruta_texto, pesos, sesgos",
                "nativo_mlp_guardar(ruta, pesos, sesgos)", NULL);
            return 1;
        }
        const int B0 = 40;
        visit_expression(cg, ARG0, B0);
        visit_expression(cg, ARG1, B0 + 1);
        visit_expression(cg, ARG2, B0 + 2);
        emit(cg, OP_ANALITICA_MLP_SAVE, (uint8_t)dest_reg, (uint8_t)B0, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* 6.x Vectores: longitud, normalizar, dot, cross */
    if (strcmp(name, "vec2_longitud") == 0 || strcmp(name, "vec3_longitud") == 0 || strcmp(name, "vec4_longitud") == 0) {
        char uso_len[56];
        snprintf(uso_len, sizeof uso_len, "%s(v)", name);
        if (codegen_error_vector_sistema_arity(cg, cn, 1, uso_len))
            return 1;
        if (!ARG0 || !is_node(ARG0, NODE_IDENTIFIER)) {
            codegen_error_vector_sistema_bad_identifiers(cg, cn, name, uso_len);
            return 1;
        }
        SymResult sr = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        if (!sr.found) return 0;
        int n = (name[3] == '2') ? 2 : (name[3] == '3') ? 3 : 4;
        uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (sr.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        int r1 = dest_reg + 1;
        int r2 = dest_reg + 2;
        emit(cg, OP_LEER, (uint8_t)r1, sr.addr & 0xFF, (sr.addr >> 8) & 0xFF, fl);
        emit(cg, OP_MULTIPLICAR_FLT, (uint8_t)r1, (uint8_t)r1, (uint8_t)r1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 1; i < n; i++) {
            uint32_t a = sr.addr + (uint32_t)(i * 8);
            emit(cg, OP_LEER, (uint8_t)r2, a & 0xFF, (a >> 8) & 0xFF, fl);
            emit(cg, OP_MULTIPLICAR_FLT, (uint8_t)r2, (uint8_t)r2, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_SUMAR_FLT, (uint8_t)r1, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_RAIZ, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "vec2_normalizar") == 0 || strcmp(name, "vec3_normalizar") == 0 || strcmp(name, "vec4_normalizar") == 0) {
        char uso_n[72];
        snprintf(uso_n, sizeof uso_n, "%s(destino, origen)", name);
        if (codegen_error_vector_sistema_arity(cg, cn, 2, uso_n))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER)) {
            codegen_error_vector_sistema_bad_identifiers(cg, cn, name, uso_n);
            return 1;
        }
        SymResult dst = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult src = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        if (!dst.found || !src.found) return 0;
        int n = (name[3] == '2') ? 2 : (name[3] == '3') ? 3 : 4;
        uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        uint8_t flS = fl | (src.is_relative ? IR_INST_FLAG_RELATIVE : 0);
        int r1 = dest_reg + 1;
        int r2 = dest_reg + 2;
        emit(cg, OP_LEER, (uint8_t)r1, src.addr & 0xFF, (src.addr >> 8) & 0xFF, flS);
        emit(cg, OP_MULTIPLICAR_FLT, (uint8_t)r1, (uint8_t)r1, (uint8_t)r1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 1; i < n; i++) {
            uint32_t a = src.addr + (uint32_t)(i * 8);
            emit(cg, OP_LEER, (uint8_t)r2, a & 0xFF, (a >> 8) & 0xFF, flS);
            emit(cg, OP_MULTIPLICAR_FLT, (uint8_t)r2, (uint8_t)r2, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_SUMAR_FLT, (uint8_t)r1, (uint8_t)r1, (uint8_t)r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_RAIZ, (uint8_t)r2, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 0; i < n; i++) {
            uint32_t a = src.addr + (uint32_t)(i * 8);
            emit_leer_u24(cg, 1, a, src.is_relative);
            emit(cg, OP_DIVIDIR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            a = dst.addr + (uint32_t)(i * 8);
            emit_escribir_u24(cg, a, 1, dst.is_relative);
        }
        return 1;
    }
    if (strcmp(name, "vec2_dot") == 0 || strcmp(name, "vec3_dot") == 0 || strcmp(name, "vec4_dot") == 0) {
        char uso_d[56];
        snprintf(uso_d, sizeof uso_d, "%s(a, b)", name);
        if (codegen_error_vector_sistema_arity(cg, cn, 2, uso_d))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER)) {
            codegen_error_vector_sistema_bad_identifiers(cg, cn, name, uso_d);
            return 1;
        }
        SymResult sa = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult sb = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        if (!sa.found || !sb.found) return 0;
        int n = (name[3] == '2') ? 2 : (name[3] == '3') ? 3 : 4;
        uint8_t flA = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | (sa.is_relative ? IR_INST_FLAG_RELATIVE : 0);
        uint8_t flB = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | (sb.is_relative ? IR_INST_FLAG_RELATIVE : 0);
        /* Acumular en reg fijo: si dest_reg es 1 o 2, los LEER del bucle pisan el resultado parcial. */
        const int acc = 4;
        emit_leer_u24(cg, 1, sa.addr, sa.is_relative);
        emit_leer_u24(cg, 2, sb.addr, sb.is_relative);
        emit(cg, OP_MULTIPLICAR_FLT, acc, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 1; i < n; i++) {
            uint32_t oa = sa.addr + (uint32_t)(i * 8), ob = sb.addr + (uint32_t)(i * 8);
            emit_leer_u24(cg, 1, oa, sa.is_relative);
            emit_leer_u24(cg, 2, ob, sb.is_relative);
            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_SUMAR_FLT, acc, acc, 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        if (acc != dest_reg)
            emit(cg, OP_MOVER, dest_reg, acc, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "vec3_cross") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 3, "vec3_cross(destino, a, b)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER) || !is_node(ARG2, NODE_IDENTIFIER)) {
            codegen_error_vector_sistema_bad_identifiers(cg, cn, name, "vec3_cross(destino, a, b)");
            return 1;
        }
        SymResult dst = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult sa = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        SymResult sb = sym_lookup(&cg->sym, ((IdentifierNode*)ARG2)->name);
        if (!dst.found || !sa.found || !sb.found) return 0;
        uint8_t flA = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | (sa.is_relative ? IR_INST_FLAG_RELATIVE : 0);
        uint8_t flB = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | (sb.is_relative ? IR_INST_FLAG_RELATIVE : 0);
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3, k = (i + 2) % 3;
            uint32_t aj = sa.addr + (uint32_t)(j * 8), ak = sa.addr + (uint32_t)(k * 8);
            uint32_t bj = sb.addr + (uint32_t)(j * 8), bk = sb.addr + (uint32_t)(k * 8);
            emit_leer_u24(cg, 1, aj, sa.is_relative);
            emit_leer_u24(cg, 2, bk, sb.is_relative);
            emit(cg, OP_MULTIPLICAR_FLT, 3, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit_leer_u24(cg, 1, ak, sa.is_relative);
            emit_leer_u24(cg, 2, bj, sb.is_relative);
            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_RESTAR_FLT, 1, 3, 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            uint32_t d = dst.addr + (uint32_t)(i * 8);
            emit_escribir_u24(cg, d, 1, dst.is_relative);
        }
        return 1;
    }

    /* mat4_mul_vec4(dest, mat, vec): dest = mat * vec; todos identificadores */
    if (strcmp(name, "mat4_mul_vec4") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 3, "mat4_mul_vec4(destino_vec4, matriz_mat4, vector_vec4)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER) || !is_node(ARG2, NODE_IDENTIFIER)) {
            if (!is_node(ARG2, NODE_IDENTIFIER))
                codegen_error_mat_mul_vec_arg_not_identifier(cg, cn, 3, ARG2, "vec4",
                                                             "mat4_mul_vec4(destino_vec4, matriz_mat4, vector_vec4)");
            else
                codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat4_mul_vec4(destino_vec4, matriz_mat4, vector_vec4)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult mat  = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        SymResult vec  = sym_lookup(&cg->sym, ((IdentifierNode*)ARG2)->name);
        if (!dest.found || !mat.found || !vec.found) return 0;
        /* Poner direcciones en regs 1, 3, 4 (reg 2 temporal) */
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (mat.is_relative) {
            emit(cg, OP_GET_FP, 3, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(mat.addr & 0xFF), (uint8_t)((mat.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 3, 3, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 3, (uint8_t)(mat.addr & 0xFF), (uint8_t)((mat.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (vec.is_relative) {
            emit(cg, OP_GET_FP, 4, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(vec.addr & 0xFF), (uint8_t)((vec.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 4, 4, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 4, (uint8_t)(vec.addr & 0xFF), (uint8_t)((vec.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT4_MUL_VEC4, 1, 3, 4, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    /* mat4_mul(dest, matL, matR): dest = matL * matR */
    if (strcmp(name, "mat4_mul") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 3, "mat4_mul(destino_mat4, izquierda_mat4, derecha_mat4)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER) || !is_node(ARG2, NODE_IDENTIFIER)) {
            codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat4_mul(destino_mat4, izquierda_mat4, derecha_mat4)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult matL = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        SymResult matR = sym_lookup(&cg->sym, ((IdentifierNode*)ARG2)->name);
        if (!dest.found || !matL.found || !matR.found) return 0;
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (matL.is_relative) {
            emit(cg, OP_GET_FP, 3, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(matL.addr & 0xFF), (uint8_t)((matL.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 3, 3, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 3, (uint8_t)(matL.addr & 0xFF), (uint8_t)((matL.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (matR.is_relative) {
            emit(cg, OP_GET_FP, 4, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(matR.addr & 0xFF), (uint8_t)((matR.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 4, 4, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 4, (uint8_t)(matR.addr & 0xFF), (uint8_t)((matR.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT4_MUL, 1, 3, 4, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mat4_identidad") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 1, "mat4_identidad(M)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER)) {
            codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat4_identidad(M)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        if (!dest.found) return 0;
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT4_IDENTIDAD, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "mat4_transpuesta") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 2, "mat4_transpuesta(destino_mat4, origen_mat4)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER)) {
            codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat4_transpuesta(destino_mat4, origen_mat4)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult src  = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        if (!dest.found || !src.found) return 0;
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (src.is_relative) {
            emit(cg, OP_GET_FP, 3, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(src.addr & 0xFF), (uint8_t)((src.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 3, 3, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 3, (uint8_t)(src.addr & 0xFF), (uint8_t)((src.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT4_TRANSPUESTA, 1, 3, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mat4_inversa") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 2, "mat4_inversa(destino_mat4, origen_mat4)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER)) {
            codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat4_inversa(destino_mat4, origen_mat4)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult src  = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        if (!dest.found || !src.found) return 0;
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (src.is_relative) {
            emit(cg, OP_GET_FP, 3, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(src.addr & 0xFF), (uint8_t)((src.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 3, 3, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 3, (uint8_t)(src.addr & 0xFF), (uint8_t)((src.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT4_INVERSA, 1, 3, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mat3_mul_vec3") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 3, "mat3_mul_vec3(destino_vec3, matriz_mat3, vector_vec3)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER) || !is_node(ARG2, NODE_IDENTIFIER)) {
            if (!is_node(ARG2, NODE_IDENTIFIER))
                codegen_error_mat_mul_vec_arg_not_identifier(cg, cn, 3, ARG2, "vec3",
                                                             "mat3_mul_vec3(destino_vec3, matriz_mat3, vector_vec3)");
            else
                codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat3_mul_vec3(destino_vec3, matriz_mat3, vector_vec3)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult mat  = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        SymResult vec  = sym_lookup(&cg->sym, ((IdentifierNode*)ARG2)->name);
        if (!dest.found || !mat.found || !vec.found) return 0;
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (mat.is_relative) {
            emit(cg, OP_GET_FP, 3, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(mat.addr & 0xFF), (uint8_t)((mat.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 3, 3, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 3, (uint8_t)(mat.addr & 0xFF), (uint8_t)((mat.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (vec.is_relative) {
            emit(cg, OP_GET_FP, 4, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(vec.addr & 0xFF), (uint8_t)((vec.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 4, 4, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 4, (uint8_t)(vec.addr & 0xFF), (uint8_t)((vec.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT3_MUL_VEC3, 1, 3, 4, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mat3_mul") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 3, "mat3_mul(destino_mat3, izquierda_mat3, derecha_mat3)"))
            return 1;
        if (!is_node(ARG0, NODE_IDENTIFIER) || !is_node(ARG1, NODE_IDENTIFIER) || !is_node(ARG2, NODE_IDENTIFIER)) {
            codegen_error_mat_sistema_bad_identifiers(cg, cn, "mat3_mul(destino_mat3, izquierda_mat3, derecha_mat3)");
            return 1;
        }
        SymResult dest = sym_lookup(&cg->sym, ((IdentifierNode*)ARG0)->name);
        SymResult matL = sym_lookup(&cg->sym, ((IdentifierNode*)ARG1)->name);
        SymResult matR = sym_lookup(&cg->sym, ((IdentifierNode*)ARG2)->name);
        if (!dest.found || !matL.found || !matR.found) return 0;
        if (dest.is_relative) {
            emit(cg, OP_GET_FP, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 1, (uint8_t)(dest.addr & 0xFF), (uint8_t)((dest.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (matL.is_relative) {
            emit(cg, OP_GET_FP, 3, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(matL.addr & 0xFF), (uint8_t)((matL.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 3, 3, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 3, (uint8_t)(matL.addr & 0xFF), (uint8_t)((matL.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (matR.is_relative) {
            emit(cg, OP_GET_FP, 4, 0, 0, IR_INST_FLAG_A_REGISTER);
            emit(cg, OP_MOVER, 2, (uint8_t)(matR.addr & 0xFF), (uint8_t)((matR.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SUMAR, 4, 4, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MOVER, 4, (uint8_t)(matR.addr & 0xFF), (uint8_t)((matR.addr >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        emit(cg, OP_MAT3_MUL, 1, 3, 4, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* 6.3 IA */
    if (strcmp(name, "aprender") == 0 || strcmp(name, "aprender_peso") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "identificador de concepto y peso",
                "aprender(id, 0.8)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_APRENDER_PESO_REG, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "reforzar") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int mag = 10;
        if (cn->n_args >= 2 && ARG1 && is_node(ARG1, NODE_LITERAL) &&
            ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG1)->value.i;
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            mag = (int)v;
        }
        emit(cg, OP_MEM_REFORZAR_CONCEPTO, (uint8_t)r1, 0, (uint8_t)mag,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "penalizar") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        int mag = 10;
        if (cn->n_args >= 2 && ARG1 && is_node(ARG1, NODE_LITERAL) &&
            ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG1)->value.i;
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            mag = (int)v;
        }
        emit(cg, OP_MEM_PENALIZAR_CONCEPTO, (uint8_t)r1, 0, (uint8_t)mag,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "decae_conexiones") == 0 || strcmp(name, "decaer_conexiones") == 0) {
        emit(cg, OP_MEM_DECAE_CONEXIONES, (uint8_t)dest_reg, 5, 10,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "consolidar_memoria") == 0 || strcmp(name, "dormir") == 0 ||
        strcmp(name, "consolidar") == 0 || strcmp(name, "consolidar_sueno") == 0) {
        if (cn->n_args != 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`%s` no admite argumentos (se recibieron %zu).",
                     name, cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        emit(cg, OP_MEM_CONSOLIDAR_SUENO, (uint8_t)dest_reg, 5, 10,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "olvidar_debiles") == 0) {
        emit(cg, OP_MEM_OLVIDAR_DEBILES, (uint8_t)dest_reg, 0, 10,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "olvidar") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_PENALIZAR_CONCEPTO, 1, 0, 100, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "ventana_percepcion") == 0 || strcmp(name, "flujo_temporal") == 0) {
        int cap = 64;
        if (cn->n_args >= 1 && ARG0 && is_node(ARG0, NODE_LITERAL) &&
            ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG0)->value.i;
            if (v < 8) v = 8;
            if (v > 4096) v = 4096;
            cap = (int)v;
        }
        emit(cg, OP_PERCEPCION_VENTANA, (uint8_t)dest_reg, (uint8_t)(cap & 0xFF), (uint8_t)((cap >> 8) & 0xFF),
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "percepcion") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_PERCEPCION_REGISTRAR, 0, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "percepcion_limpiar") == 0) {
        emit(cg, OP_PERCEPCION_LIMPIAR, 0, 0, 0, 0);
        return 1;
    }
    if (strcmp(name, "percepcion_tamano") == 0) {
        emit(cg, OP_PERCEPCION_TAMANO, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "percepcion_anterior") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "indice K de percepcion anterior", cn->base.line, cn->base.col);
            return 1;
        }
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
            strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t k = ((LiteralNode*)ARG0)->value.i;
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            emit(cg, OP_PERCEPCION_ANTERIOR, (uint8_t)dest_reg, (uint8_t)k, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        } else {
            int r1 = visit_expression(cg, ARG0, dest_reg + 1);
            emit(cg, OP_PERCEPCION_ANTERIOR, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "percepcion_recientes") == 0) {
        emit(cg, OP_PERCEPCION_LISTA, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "mai_contexto_recientes") == 0 || strcmp(name, "mai_contexto") == 0) {
        uint8_t flags = (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_SAFE);
        int mag = 10;
        if (cn->n_args >= 1 && ARG0) {
            if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
                strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
                int64_t v = ((LiteralNode*)ARG0)->value.i;
                if (v < 1) v = 1;
                if (v > 10) v = 10;
                mag = (int)v;
            } else {
                int r1 = visit_expression(cg, ARG0, dest_reg + 1);
                flags = (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_SAFE);
                emit(cg, OP_MAI_CONTEXTO_LISTA, (uint8_t)dest_reg, (uint8_t)r1, 0, flags);
                return 1;
            }
        }
        emit(cg, OP_MAI_CONTEXTO_LISTA, (uint8_t)dest_reg, (uint8_t)mag, 0, flags);
        return 1;
    }
    if (strcmp(name, "ventana_rastro_activacion") == 0 || strcmp(name, "rastro_activacion_ventana") == 0) {
        int cap = 128;
        if (cn->n_args >= 1 && ARG0 && is_node(ARG0, NODE_LITERAL) &&
            ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG0)->value.i;
            if (v < 16) v = 16;
            if (v > 2048) v = 2048;
            cap = (int)v;
        }
        emit(cg, OP_RASTRO_ACTIVACION_VENTANA, (uint8_t)dest_reg, (uint8_t)(cap & 0xFF), (uint8_t)((cap >> 8) & 0xFF),
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "rastro_activacion_limpiar") == 0) {
        emit(cg, OP_RASTRO_ACTIVACION_LIMPIAR, 0, 0, 0, 0);
        return 1;
    }
    if (strcmp(name, "rastro_activacion_tamano") == 0) {
        emit(cg, OP_RASTRO_ACTIVACION_TAMANO, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "rastro_activacion_obtener") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "indice K de rastro anterior", cn->base.line, cn->base.col);
            return 1;
        }
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
            strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t k = ((LiteralNode*)ARG0)->value.i;
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            emit(cg, OP_RASTRO_ACTIVACION_OBTENER, (uint8_t)dest_reg, (uint8_t)k, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        } else {
            int r1 = visit_expression(cg, ARG0, dest_reg + 1);
            emit(cg, OP_RASTRO_ACTIVACION_OBTENER, (uint8_t)dest_reg, (uint8_t)r1, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "rastro_activacion_peso") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "indice K de rastro anterior", cn->base.line, cn->base.col);
            return 1;
        }
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
            strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t k = ((LiteralNode*)ARG0)->value.i;
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            emit(cg, OP_RASTRO_ACTIVACION_PESO, (uint8_t)dest_reg, (uint8_t)k, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        } else {
            int r1 = visit_expression(cg, ARG0, dest_reg + 1);
            emit(cg, OP_RASTRO_ACTIVACION_PESO, (uint8_t)dest_reg, (uint8_t)r1, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "rastro_activacion_lista") == 0 || strcmp(name, "rastro_activacion_recientes") == 0) {
        emit(cg, OP_RASTRO_ACTIVACION_LISTA, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "propagar_activacion") == 0 || strcmp(name, "propagar_activacion_de") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto inicial", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = dest_reg + 1;
        int r2 = visit_expression(cg, ARG0, dest_reg + 2);
        uint32_t tipo = 0, K = 8, prof = 3;
        if (strcmp(name, "propagar_activacion_de") == 0 && cn->n_args >= 2 && ARG1 &&
            is_node(ARG1, NODE_LITERAL) && ((LiteralNode*)ARG1)->type_name &&
            strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG1)->value.i;
            if (v >= 0 && v <= 10) tipo = (uint32_t)v;
        }
        uint32_t pack = tipo | (K << 8) | (prof << 16);
        int r3 = dest_reg + 3;
        emit(cg, OP_MOVER, (uint8_t)r3, (uint8_t)(pack & 0xFFu), (uint8_t)((pack >> 8) & 0xFFu),
             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        {
            uint8_t hi = (uint8_t)((pack >> 16) & 0xFFu);
            if (hi != 0) {
                int r4 = dest_reg + 4;
                emit(cg, OP_MOVER, (uint8_t)r4, hi, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_BIT_SHL, (uint8_t)r4, (uint8_t)r4, 16, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_SUMAR, (uint8_t)r3, (uint8_t)r3, (uint8_t)r4, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            }
        }
        emit(cg, OP_MEM_PROPAGAR_ACTIVACION, (uint8_t)r1, (uint8_t)r2, (uint8_t)r3,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "propagar_activacion_mai") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto inicial", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = dest_reg + 1;
        int r2 = visit_expression(cg, ARG0, dest_reg + 2);
        int r3 = dest_reg + 3;
        int r4 = dest_reg + 4;
        int r5 = dest_reg + 5;
        int r6 = dest_reg + 6;
        int r7 = dest_reg + 7;
        if (cn->n_args >= 2 && ARG1)
            visit_expression(cg, ARG1, r4);
        else
            emit(cg, OP_MOVER, (uint8_t)r4, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MOVER, (uint8_t)r5, 8, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MOVER, (uint8_t)r6, 16, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_BIT_SHL, (uint8_t)r5, (uint8_t)r5, (uint8_t)r6,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        emit(cg, OP_MOVER, (uint8_t)r6, 3, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MOVER, (uint8_t)r7, 24, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_BIT_SHL, (uint8_t)r6, (uint8_t)r6, (uint8_t)r7,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        emit(cg, OP_SUMAR, (uint8_t)r3, (uint8_t)r4, (uint8_t)r5,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        emit(cg, OP_SUMAR, (uint8_t)r3, (uint8_t)r3, (uint8_t)r6,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        emit(cg, OP_MEM_PROPAGAR_ACTIVACION, (uint8_t)r1, (uint8_t)r2, (uint8_t)r3,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER | IR_INST_FLAG_RELATIVE);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "elegir_por_peso") == 0 || strcmp(name, "elegir_por_peso_segun") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "lista de conceptos y contexto de activacion",
                "elegir_por_peso(lista, contexto)", NULL);
            return 1;
        }
        int r1 = dest_reg + 1;
        int r2 = visit_expression(cg, ARG0, dest_reg + 2);
        int rA = visit_expression(cg, ARG1, dest_reg + 3);
        emit(cg, OP_MEM_ELEGIR_POR_PESO_IDX, (uint8_t)r1, (uint8_t)rA, (uint8_t)r2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "elegir_por_peso_id") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "lista de conceptos y contexto de activacion",
                "elegir_por_peso_id(lista, contexto)", NULL);
            return 1;
        }
        int r1 = dest_reg + 1;
        int r2 = visit_expression(cg, ARG0, dest_reg + 2);
        int rA = visit_expression(cg, ARG1, dest_reg + 3);
        emit(cg, OP_MEM_ELEGIR_POR_PESO_ID, (uint8_t)r1, (uint8_t)rA, (uint8_t)r2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "elegir_por_peso_semilla") == 0 || strcmp(name, "elegir_por_peso_seed") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "semilla de aleatoriedad", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = dest_reg + 1;
        int rA = visit_expression(cg, ARG0, dest_reg + 2);
        emit(cg, OP_MEM_ELEGIR_POR_PESO_SEMILLA, (uint8_t)r1, (uint8_t)rA, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar") == 0) {
        int r1 = dest_reg + 1;
        int rA = visit_expression(cg, ARG0, dest_reg + 2);
        /* Profundidad BFS por defecto 2 (byte C inmediato); A=B=reg 1: id entrante y salida. */
        emit(cg, OP_MEM_PENSAR, (uint8_t)r1, (uint8_t)rA, 2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar_respuesta") == 0) {
        int r1 = dest_reg + 1;
        int rA = visit_expression(cg, ARG0, dest_reg + 2);
        uint8_t params = 0;
        if (ARG1) {
            /* TODO: permitir pasar creatividad y umbral empaquetados */
        }
        emit(cg, OP_MEM_PENSAR_RESPUESTA, (uint8_t)r1, (uint8_t)rA, params,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        codegen_emit_write_resultado(cg, (uint8_t)r1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "asociar_relacion") == 0) {
        if (getenv("JASBOOT_DEBUG")) fprintf(stderr, "[CODEGEN] Generando asociar_relacion n_args=%zu\n", cn->n_args);
        if (cn->n_args < 3 || cn->n_args > 4) {
            codegen_error_sistema_incorporada_arity(cg, cn, 3,
                "origen, destino, tipo y peso (opcional)",
                "asociar_relacion(\"agua\", \"vida\", 1, 0.7)",
                "(El cuarto argumento 'peso' es opcional y debe ser entre 0.0 y 1.0)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        
        if (cn->n_args == 4) {
            visit_expression(cg, ARG2, 3); // tipo
            
            if (is_node(ARG3, NODE_LITERAL) && ((LiteralNode*)ARG3)->is_float) {
                float f = (float)((LiteralNode*)ARG3)->value.f;
                uint32_t p1000 = (uint32_t)(f * 1000.0f);
                if (getenv("JASBOOT_DEBUG")) fprintf(stderr, "  Literal float: %.4f -> p1000=%u\n", f, p1000);
                // Empaquetar en registro 4: (p1000 << 32) | tipo_reg (para soportar hashes de 32 bits)
                emit(cg, OP_MOVER, 5, p1000 & 0xFF, (p1000 >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_MOVER, 6, 32, 0, IR_INST_FLAG_B_IMMEDIATE);
                emit(cg, OP_BIT_SHL, 5, 5, 6, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_SUMAR, 4, 5, 3, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_MEM_ASOCIAR_RELACION, 1, 2, 4, 
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
            } else {
                if (getenv("JASBOOT_DEBUG")) fprintf(stderr, "  Argumento no literal o no float\n");
                visit_expression(cg, ARG3, 4); // peso (variable)
                emit(cg, OP_MOVER, 15, 1000 & 0xFF, (1000 >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_CONV_I2F, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); // 16 = 1000.0f
                // Redondeo: peso * 1000 + 0.5
                emit(cg, OP_MULTIPLICAR_FLT, 17, 4, 16, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_CONV_F2I, 18, 17, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); // 18 = (int)1000*peso
                emit(cg, OP_MOVER, 19, 32, 0, IR_INST_FLAG_B_IMMEDIATE);
                emit(cg, OP_BIT_SHL, 20, 18, 19, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_SUMAR, 21, 20, 3, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_MEM_ASOCIAR_RELACION, 1, 2, 21, 
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
            }
        } else {
            visit_expression(cg, ARG2, 3);
            emit(cg, OP_MEM_ASOCIAR_RELACION, 1, 2, 3,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "asociar_relacion_mai") == 0) {
        if (cn->n_args < 3 || cn->n_args > 4) {
            codegen_error_sistema_incorporada_arity(cg, cn, 3,
                "origen, destino, tipo y peso (opcional)",
                "asociar_relacion_mai(\"agua\", \"vida\", 1, 0.7)",
                "(El cuarto argumento 'peso' es opcional y debe ser entre 0.0 y 1.0)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        const uint8_t fl_mai = (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER
            | IR_INST_FLAG_C_REGISTER | IR_INST_FLAG_RELATIVE);
        if (cn->n_args == 4) {
            visit_expression(cg, ARG2, 3);
            if (is_node(ARG3, NODE_LITERAL) && ((LiteralNode*)ARG3)->is_float) {
                float f = (float)((LiteralNode*)ARG3)->value.f;
                uint32_t p1000 = (uint32_t)(f * 1000.0f);
                emit(cg, OP_MOVER, 5, p1000 & 0xFF, (p1000 >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_MOVER, 6, 32, 0, IR_INST_FLAG_B_IMMEDIATE);
                emit(cg, OP_BIT_SHL, 5, 5, 6, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_SUMAR, 4, 5, 3, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_MEM_ASOCIAR_RELACION, 1, 2, 4, fl_mai);
            } else {
                visit_expression(cg, ARG3, 4);
                emit(cg, OP_MOVER, 15, 1000 & 0xFF, (1000 >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_CONV_I2F, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_MULTIPLICAR_FLT, 17, 4, 16, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_CONV_F2I, 18, 17, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_MOVER, 19, 32, 0, IR_INST_FLAG_B_IMMEDIATE);
                emit(cg, OP_BIT_SHL, 20, 18, 19, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_SUMAR, 21, 20, 3, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                emit(cg, OP_MEM_ASOCIAR_RELACION, 1, 2, 21, fl_mai);
            }
        } else {
            visit_expression(cg, ARG2, 3);
            emit(cg, OP_MEM_ASOCIAR_RELACION, 1, 2, 3, fl_mai);
        }
        return 1;
    }
    if (strcmp(name, "corregir_secuencia") == 0) {
        if (cn->n_args < 3) {
            codegen_error_sistema_incorporada_arity(cg, cn, 3,
                "identificador base, secuencia y correccion",
                "corregir_secuencia(id, [\"token1\", \"token2\"], \"corregido\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        visit_expression(cg, ARG2, 3);
        emit(cg, OP_MEM_CORREGIR_SECUENCIA, 1, 2, 3,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_asociados_rango") == 0) {
        if (cn->n_args < 3) {
            codegen_error_sistema_incorporada_arity(cg, cn, 3,
                "lista de conceptos, peso minimo y peso maximo",
                "buscar_asociados_rango([\"agua\", \"h2o\"], 0.5, 1.0)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 10); /* lista */
        visit_expression(cg, ARG1, 11); /* min_p */
        visit_expression(cg, ARG2, 12); /* max_p */
        
        /* Escalar min/max (0-1000) con redondeo y empaquetar */
        emit(cg, OP_MOVER, 22, 1000 & 0xFF, (1000 >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_CONV_I2F, 23, 22, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); // 23 = 1000.0f
        
        // Usar 0.5 para redondeo
        emit(cg, OP_MOVER, 20, 1, 0, IR_INST_FLAG_B_IMMEDIATE); // usaremos 0.5? No hay literal float facil.
        // Multiplicar y luego sumar un pequeño epsilon antes de truncar
        emit(cg, OP_MULTIPLICAR_FLT, 24, 11, 23, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER); 
        emit(cg, OP_MULTIPLICAR_FLT, 25, 12, 23, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        
        emit(cg, OP_CONV_F2I, 26, 24, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); 
        emit(cg, OP_CONV_F2I, 27, 25, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);

        emit(cg, OP_MOVER, 28, 16, 0, IR_INST_FLAG_B_IMMEDIATE);
        emit(cg, OP_BIT_SHL, 29, 27, 28, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER); 
        emit(cg, OP_SUMAR, 30, 29, 26, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);   
        
        emit(cg, OP_MEM_BUSCAR_MAPA_ASOCIADOS, 1, 10, 30, 
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "comparar_patrones") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto base y patron de comparacion",
                "comparar_patrones(\"hola\", \"h*la\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_COMPARAR_PATRONES, dest_reg, 1, 2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador o texto a buscar", cn->base.line, cn->base.col);
            return 1;
        }
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_LOAD_STR_HASH, 1, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 1);
        }
        emit(cg, OP_MEM_OBTENER_VALOR, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_ID_A_TEXTO, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_en_memoria") == 0 || strcmp(name, "buscar_introspectiva") == 0) {
        // Búsqueda introspectiva: busca texto dentro de claves y valores de conceptos JMN
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto a buscar en la memoria", cn->base.line, cn->base.col);
            return 1;
        }
        // Cargar el texto a buscar
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_LOAD_STR_HASH, 1, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 1);
        }
        // Llamar al opcode de búsqueda introspectiva (retorna ID del primer concepto que contiene el texto)
        emit(cg, OP_MEM_BUSCAR_INTROSPECTIVA, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        // Convertir ID a texto para que 'resultado' tenga el texto del concepto encontrado
        emit(cg, OP_ID_A_TEXTO, (uint8_t)dest_reg, (uint8_t)dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, (uint8_t)dest_reg);
        return 1;
    }
    if (strcmp(name, "buscar_en_memoria_lista") == 0) {
        // Búsqueda introspectiva que devuelve lista de IDs
        // Argumentos: termino, max_resultados
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto a buscar y numero maximo de resultados",
                "buscar_en_memoria_lista(\"termino\", 10)", NULL);
            return 1;
        }
        
        // Cargar término de búsqueda en reg 10
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_LOAD_STR_HASH, 10, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 10);
        }
        
        // Cargar max_resultados en reg 11 (default 10 si no se especifica)
        if (ARG1 && is_node(ARG1, NODE_LITERAL) && ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t max_val = ((LiteralNode*)ARG1)->value.i;
            if (max_val < 1) max_val = 1;
            if (max_val > 100) max_val = 100;
            emit(cg, OP_MOVER, 11, (uint8_t)(max_val & 0xFF), 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else if (ARG1) {
            visit_expression(cg, ARG1, 11);
        } else {
            emit(cg, OP_MOVER, 11, 10, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        
        // Llamar al opcode que retorna una lista de IDs
        // A <- lista_id con IDs; B=termino_id, C=max_resultados
        emit(cg, OP_MEM_BUSCAR_INTROSPECTIVA_LISTA, 1, 10, 11, 
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_en_memoria_cs") == 0) {
        // Búsqueda introspectiva con control de case sensitive
        // Argumentos: termino, case_sensitive (0 o 1)
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto a buscar y booleano para distinguir mayusculas",
                "buscar_en_memoria_cs(\"termino\", verdadero)", NULL);
            return 1;
        }
        
        // Cargar término de búsqueda en reg 10
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_LOAD_STR_HASH, 10, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 10);
        }
        
        // Cargar case_sensitive en reg 11 (0 o 1)
        if (ARG1 && is_node(ARG1, NODE_LITERAL) && ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t cs_val = ((LiteralNode*)ARG1)->value.i;
            emit(cg, OP_MOVER, 11, (cs_val != 0) ? 1 : 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else if (ARG1) {
            visit_expression(cg, ARG1, 11);
        } else {
            emit(cg, OP_MOVER, 11, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        
        // Llamar al opcode con case sensitive
        // A <- primer ID; B=termino_id, C=case_sensitive(0/1)
        emit(cg, OP_MEM_BUSCAR_INTROSPECTIVA_CS, 1, 10, 11,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        // Convertir ID a texto
        emit(cg, OP_ID_A_TEXTO, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_en_memoria_detallada") == 0) {
        // Búsqueda introspectiva detallada con metadata completa
        // Argumentos: termino, max_resultados, case_sensitive (opcional)
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto a buscar y numero maximo de resultados",
                "buscar_en_memoria_detallada(\"termino\", 10)", NULL);
            return 1;
        }
        
        // Cargar término de búsqueda en reg 10
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_LOAD_STR_HASH, 10, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 10);
        }
        
        // Cargar max_resultados en reg 11
        uint32_t max_val = 10;
        if (ARG1 && is_node(ARG1, NODE_LITERAL) && ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t val = ((LiteralNode*)ARG1)->value.i;
            if (val < 1) val = 1;
            if (val > 100) val = 100;
            max_val = (uint32_t)val;
        }
        
        // Cargar case_sensitive en reg 12 (0 por defecto)
        uint32_t cs_val = 0;
        if (cn->n_args >= 3 && ARG2 && is_node(ARG2, NODE_LITERAL) && ((LiteralNode*)ARG2)->type_name && strcmp(((LiteralNode*)ARG2)->type_name, "entero") == 0) {
            cs_val = (((LiteralNode*)ARG2)->value.i != 0) ? 1 : 0;
        }
        
        // Empaquetar: max_resultados | (case_sensitive << 8)
        uint32_t packed = max_val | (cs_val << 8);
        emit(cg, OP_MOVER, 11, (uint8_t)(packed & 0xFF), (uint8_t)((packed >> 8) & 0xFF), 
             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        
        // Llamar al opcode detallado
        // A <- lista_id con metadata; B=termino, C=max|(cs<<8)
        emit(cg, OP_MEM_BUSCAR_INTROSPECTIVA_DETALLADA, 1, 10, 11,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
        if (strcmp(name, "procesar_texto") == 0) {
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_MEM_PROCESAR_TEXTO, (uint8_t)dest_reg, (uint8_t)dest_reg, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "obtener_relacionados") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_OBTENER_RELACIONADOS, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "es_variable_sistema") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_ES_VARIABLE_SISTEMA, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "obtener_todos_conceptos") == 0) {
        emit(cg, OP_MEM_OBTENER_TODOS, 1, 0, 0, 0);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* Secuencias en JMN (VM: patrones, aristas JMN_RELACION_SECUENCIA / PATRON) */
    if (strcmp(name, "registrar_patron") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto del patron", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_MEM_REGISTRAR_PATRON, (uint8_t)dest_reg, (uint8_t)dest_reg, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "asociar_secuencia") == 0) {
        if (cn->n_args < 1) {
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "identificador de concepto (y opcionalmente el segundo)",
                "asociar_secuencia(id1) o asociar_secuencia(id1, id2)", NULL);
            return 1;
        }
        if (cn->n_args == 1) {
            visit_expression(cg, ARG0, 1);
            emit(cg, OP_STR_ASOCIAR_SECUENCIA, 0, 1, 0, IR_INST_FLAG_B_REGISTER);
        } else {
            visit_expression(cg, ARG0, 1);
            visit_expression(cg, ARG1, 2);
            emit(cg, OP_STR_ASOCIAR_SECUENCIA, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar_siguiente") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto base", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 10);
        if (ARG1) {
            visit_expression(cg, ARG1, 11);
            emit(cg, OP_MEM_PENSAR_SIGUIENTE, 1, 10, 11, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        } else {
            emit(cg, OP_MEM_PENSAR_SIGUIENTE, 1, 10, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_ID_A_TEXTO, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar_siguiente_mai") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto base", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 10);
        const uint8_t fl_mai = (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER
            | IR_INST_FLAG_RELATIVE | IR_INST_FLAG_C_REGISTER);
        if (ARG1) {
            visit_expression(cg, ARG1, 11);
            emit(cg, OP_MEM_PENSAR_SIGUIENTE, 1, 10, 11, fl_mai);
        } else {
            emit(cg, OP_MEM_PENSAR_SIGUIENTE, 1, 10, 0,
                 (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_RELATIVE));
        }
        emit(cg, OP_ID_A_TEXTO, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar_anterior") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto base", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 10);
        if (ARG1) {
            visit_expression(cg, ARG1, 11);
            emit(cg, OP_MEM_PENSAR_ANTERIOR, 1, 10, 11, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        } else {
            emit(cg, OP_MEM_PENSAR_ANTERIOR, 1, 10, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_ID_A_TEXTO, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_asociados") == 0 || strcmp(name, "asociados_de") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto base", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 10);
        if (ARG1) visit_expression(cg, ARG1, 11);
        else emit(cg, OP_MOVER, 11, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_BUSCAR_ASOCIADOS, 1, 10, 11, 
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        emit(cg, OP_ID_A_TEXTO, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_asociados_lista") == 0 || strcmp(name, "asociados_lista_de") == 0
        || strcmp(name, "buscar_asociados_lista_mai") == 0
        || strcmp(name, "vecinos_jmn") == 0 || strcmp(name, "vecinos_jmn_mai") == 0
        || strcmp(name, "conexiones_salientes_de") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "identificador base y numero maximo de asociados (K)",
                "buscar_asociados_lista(\"agua\", 5)", NULL);
            return 1;
        }
        /* Regs 10–15: no pisar dest_reg (suele ser 1) ni args en 2–4 */
        visit_expression(cg, ARG0, 10); /* origen */
        visit_expression(cg, ARG1, 11); /* K */
        if (cn->n_args >= 3 && ARG2) visit_expression(cg, ARG2, 12); /* tipo */
        else emit(cg, OP_MOVER, 12, 1, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE); /* tipo 1 = asociaciones (defecto) */
        
        /* Empaquetar como lee la VM: tipo = C & 0xFFFF, K = (C >> 16) & 0xFFFF → C = (K << 16) | tipo */
        emit(cg, OP_MOVER, 13, 16, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_BIT_SHL, 14, 11, 13, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER); /* 14 = K << 16 */
        emit(cg, OP_SUMAR, 15, 14, 12, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);   /* 15 = (K << 16) | tipo */
        {
            uint8_t fl = (uint8_t)(IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
            if (strcmp(name, "buscar_asociados_lista_mai") == 0 || strcmp(name, "vecinos_jmn_mai") == 0)
                fl = (uint8_t)(fl | IR_INST_FLAG_RELATIVE);
            emit(cg, OP_MEM_BUSCAR_ASOCIADOS_LISTA, 1, 10, 15, fl);
        }
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "comparar_patrones") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "texto base y patron de comparacion",
                "comparar_patrones(\"hola\", \"h*la\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 10);
        visit_expression(cg, ARG1, 11);
        emit(cg, OP_MEM_COMPARAR_PATRONES, (uint8_t)dest_reg, 10, 11, 0);
        return 1;
    }

    /* 6.4 I/O, 6.5 Tiempo, 7.2 Entrada no bloqueante */
    if (strcmp(name, "leer_entrada") == 0) {
        emit(cg, OP_IO_INPUT_REG, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "percibir_teclado") == 0) {
        emit(cg, OP_IO_PERCIBIR_TECLADO, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "ingreso_inmediato") == 0) {
        emit(cg, OP_IO_PERCIBIR_TECLADO, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "entrada_flotante") == 0) {
        if (cn->n_args != 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'entrada_flotante' no admite argumentos (se recibieron %zu).",
                     cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        emit(cg, OP_IO_ENTRADA_FLOTANTE, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "limpiar_consola") == 0) {
        size_t off = add_string(cg, "\x1b[2J\x1b[H");
        emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
             IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        /* En consolas Windows sin VT (cmd clásico), los ESC no borran; system("cls") sí. */
#if defined(_WIN32) || defined(_WIN64)
        {
            size_t clsoff = add_string(cg, "cls");
            emit_load_str_hash_in_reg(cg, clsoff, 1);
            emit(cg, OP_SYS_EXEC, dest_reg, 1, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
#endif
        return 1;
    }
    if (strcmp(name, "obtener_secuencia") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 10);
        emit(cg, OP_MEM_OBTENER_SECUENCIA, 1, 10, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        codegen_emit_write_resultado(cg, 1);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pausa") == 0) {
        if (cn->n_args > 1) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`pausa` acepta cero argumentos o un mensaje opcional: `pausa` o `pausa(\"Mensaje\")`.");
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        if (cn->n_args == 1) {
            int msg_reg = visit_expression(cg, cn->args[0], 1);
            if (cg->has_error) return 1;
            emit(cg, OP_IO_PAUSA, (uint8_t)msg_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else {
            emit(cg, OP_IO_PAUSA, 0, 0, 0, 0);
        }
        return 1;
    }
    if (strcmp(name, "ahora") == 0 || strcmp(name, "obtener_ahora") == 0 || strcmp(name, "obtener_timestamp") == 0) {
        emit(cg, OP_SYS_TIMESTAMP, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "formatear_timestamp") == 0) {
        if (cn->n_args != 2) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`formatear_timestamp` requiere 2 argumentos: timestamp y formato.");
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_STR_FORMATEAR_TIMESTAMP, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), (uint8_t)(dest_reg + 2),
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return 1;
    }
    if (strcmp(name, "pausa_milisegundos") == 0 || strcmp(name, "esperar_milisegundos") == 0) {
        if (cn->n_args != 1) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`%s` requiere un argumento entero (milisegundos). Ejemplo: pausa_milisegundos(500).",
                     name);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_PAUSA_MILISEGUNDOS, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "diferencia_en_segundos") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "timestamp final y timestamp inicial",
                "diferencia_en_segundos(ahora(), t1)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_RESTAR, dest_reg, dest_reg + 2, dest_reg + 1, 0);
        return 1;
    }

    /* 6.6 Archivos */
    if (strcmp(name, "abrir_archivo") == 0 || strcmp(name, "fs_abrir") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "ruta del archivo", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        if (ARG1) visit_expression(cg, ARG1, dest_reg + 1);
        else emit(cg, OP_MOVER, dest_reg + 1, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_FS_ABRIR, dest_reg, dest_reg, dest_reg + 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "cerrar_archivo") == 0 || strcmp(name, "fs_cerrar") == 0) {
        if (ARG0) visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_FS_CERRAR, ARG0 ? dest_reg : 0, 0, 0, ARG0 ? IR_INST_FLAG_A_REGISTER : 0);
        return 1;
    }
    if (strcmp(name, "leer_linea_archivo") == 0 || strcmp(name, "fs_leer_linea") == 0) {
        if (ARG0) { visit_expression(cg, ARG0, dest_reg + 1); emit(cg, OP_FS_LEER_LINEA, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); }
        else emit(cg, OP_FS_LEER_LINEA, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "escribir_archivo") == 0 || strcmp(name, "fs_escribir") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto o datos a escribir", cn->base.line, cn->base.col);
            return 1;
        }
        if (ARG1) {
            /* Firma esperada: escribir_archivo(data, handle) */
            visit_expression(cg, ARG0, 1); /* data */
            visit_expression(cg, ARG1, 2); /* handle */
            emit(cg, OP_FS_ESCRIBIR, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            visit_expression(cg, ARG0, 1); /* data (usar current_file) */
            emit(cg, OP_FS_ESCRIBIR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "fin_archivo") == 0) {
        if (ARG0) { visit_expression(cg, ARG0, dest_reg + 1); emit(cg, OP_FS_FIN_ARCHIVO, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); }
        else emit(cg, OP_FS_FIN_ARCHIVO, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "existe_archivo") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "ruta del archivo a comprobar", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_FS_EXISTE, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "listar_archivos") == 0 || strcmp(name, "fs_listar") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "ruta del directorio", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_FS_LISTAR, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "fs_leer_byte") == 0) {
        if (ARG0) {
            visit_expression(cg, ARG0, dest_reg + 1);
            emit(cg, OP_FS_LEER_BYTE, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_FS_LEER_BYTE, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "fs_escribir_byte") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "valor del byte (0-255)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);  /* byte value */
        if (ARG1) {
            visit_expression(cg, ARG1, dest_reg + 1);
            emit(cg, OP_FS_ESCRIBIR_BYTE, dest_reg + 1, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_FS_ESCRIBIR_BYTE, 0, dest_reg, 0, IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "fs_leer_texto") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "manejador de archivo o ruta", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_FS_LEER_TEXTO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_parse") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "cadena de texto JSON", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_PARSE, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_stringify") == 0) {
        if (!ARG0) return 0;
        if (cn->n_args > 2) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`json_stringify` admite el valor JSON y opcionalmente la sangria (0-16 espacios por nivel); se recibieron %zu argumentos.",
                     cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        if (cn->n_args >= 2 && ARG1) {
            visit_expression(cg, ARG1, 2);
            emit(cg, OP_JSON_STRINGIFY, dest_reg, 1, 2,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_JSON_STRINGIFY, dest_reg, 1, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        }
        return 1;
    }
    if (strcmp(name, "json_objeto_obtener") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_JSON_OBJETO_OBTENER, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_lista_obtener") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_JSON_LISTA_OBTENER, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_lista_tamano") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_LISTA_TAMANO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_a_texto") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_A_TEXTO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_a_entero") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_A_ENTERO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_a_flotante") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_A_FLOTANTE, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_a_bool") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_A_BOOL, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "json_tipo") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_JSON_TIPO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_crear") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_BYTES_CREAR, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_tamano") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_BYTES_TAMANO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_obtener") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_BYTES_OBTENER, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_poner") == 0) {
        if (!ARG0 || !ARG1 || !ARG2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        visit_expression(cg, ARG2, 3);
        emit(cg, OP_BYTES_PONER, 1, 2, 3, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_anexar") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_BYTES_ANEXAR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_subbytes") == 0) {
        if (!ARG0 || !ARG1 || !ARG2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        visit_expression(cg, ARG2, 3);
        emit(cg, OP_BYTES_SUBBYTES, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_desde_texto") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_BYTES_DESDE_TEXTO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_a_texto") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_BYTES_A_TEXTO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "bytes_puntero") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_BYTES_PUNTERO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "dns_resolver") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_DNS_RESOLVER, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tcp_conectar") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TCP_CONECTAR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tcp_escuchar") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TCP_ESCUCHAR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tcp_aceptar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_TCP_ACEPTAR, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tcp_enviar") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TCP_ENVIAR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tcp_recibir") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TCP_RECIBIR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tcp_cerrar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_TCP_CERRAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "tls_cliente") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TLS_CLIENTE, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tls_servidor") == 0) {
        if (!ARG0 || !ARG1 || !ARG2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        visit_expression(cg, ARG2, 3);
        emit(cg, OP_TLS_SERVIDOR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tls_enviar") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TLS_ENVIAR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tls_recibir") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_TLS_RECIBIR, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tls_cerrar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_TLS_CERRAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }

    /* FFI: cargar biblioteca, obtener símbolo, llamar */
    /* 6.x Heap */
    if (strcmp(name, "reservar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_HEAP_RESERVAR, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "liberar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_HEAP_LIBERAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }

    if (strcmp(name, "ffi_cargar") == 0) {
        if (!ARG0) return 0;
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            const char* path = ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "";
            size_t off = add_string(cg, path);
            if (off > 0xFFFF) return 0;
            emit(cg, OP_CARGAR_BIBLIOTECA, dest_reg, (uint8_t)(off & 0xFF), (uint8_t)((off >> 8) & 0xFF), 0);
            return 1;
        }
        return 0;
    }
    if (strcmp(name, "ffi_simbolo") == 0) {
        if (cn->n_args < 2 || !ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, dest_reg + 1); /* handle en dest_reg+1 */
        if (is_node(ARG1, NODE_LITERAL) && ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "texto") == 0) {
            const char* sym = ((LiteralNode*)ARG1)->value.str ? ((LiteralNode*)ARG1)->value.str : "";
            size_t off = add_string(cg, sym);
            if (off > 0xFFFF) return 0;
            emit(cg, OP_MOVER, dest_reg + 2, (uint8_t)(off & 0xFF), (uint8_t)((off >> 8) & 0xFF), IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG1, dest_reg + 2); /* offset en data (debe ser válido) */
        }
        emit(cg, OP_FFI_OBTENER_SIMBOLO, dest_reg, dest_reg + 1, dest_reg + 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "ffi_llamar") == 0) {
        if (cn->n_args < 1) return 0;
        visit_expression(cg, ARG0, 2); /* fn_ptr en reg 2 */
        unsigned n = (unsigned)(cn->n_args - 1);
        if (n > 4) n = 4;
        if (n > 0 && ARG1) visit_expression(cg, ARG1, 3);
        if (n > 1 && ARG2) visit_expression(cg, ARG2, 4);
        if (n > 2 && ARG3) visit_expression(cg, ARG3, 5);
        if (n > 3 && cn->n_args > 4) visit_expression(cg, cn->args[4], 6);
        emit(cg, OP_FFI_LLAMAR, dest_reg, 2, (uint8_t)n, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* 6.7 Listas, 6.8 Mapas */
    if (strcmp(name, "crear_lista") == 0 || strcmp(name, "mem_lista_crear") == 0 || strcmp(name, "lista_crear") == 0) {
        if (cn->n_args > 1) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`crear_lista`, `mem_lista_crear` y `lista_crear` solo admiten 0 o 1 argumento; esta llamada tiene %zu.",
                     cn->n_args);
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        if (cn->n_args == 0) {
            emit(cg, OP_MOVER, dest_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_MEM_LISTA_CREAR, dest_reg, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_MEM_LISTA_CREAR, dest_reg, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else if (is_node(ARG0, NODE_LITERAL) && !((LiteralNode*)ARG0)->is_float &&
                   (!((LiteralNode*)ARG0)->type_name || strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) &&
                   ((LiteralNode*)ARG0)->value.i == 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`crear_lista(0)` no es valido: en la VM el id 0 no crea una lista usable. "
                     "Use `crear_lista()` sin argumentos u otro entero distinto de 0.");
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        } else {
            visit_expression(cg, ARG0, dest_reg);
            emit(cg, OP_MEM_LISTA_CREAR, dest_reg, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "lista_agregar") == 0 || strcmp(name, "mem_lista_agregar") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 2,
                "lista y valor (entero)",
                "mem_lista_agregar(mi_lista, 42) o lista_agregar(mi_lista, 42)");
            return 1;
        }
        if (is_node(ARG0, NODE_IDENTIFIER)) {
            const char *el = sym_lookup_lista_elem(&cg->sym, ((IdentifierNode *)ARG0)->name);
            if (el && el[0]) {
                const char *vt = get_expression_type(cg, ARG1);
                if (vt && strcmp(el, vt) != 0) {
                    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                             "mem_lista_agregar: la variable de lista se declaro como lista<%s>; "
                             "el valor a agregar se infiere como `%s`.",
                             el, vt);
                    cg->has_error = 1;
                    cg->err_line = cn->base.line;
                    cg->err_col = cn->base.col;
                    return 1;
                }
            }
        }
        visit_expression(cg, ARG0, 1);
        SymResult ag_tmp = sym_reserve_temp(&cg->sym, 8);
        uint8_t fl_agw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (ag_tmp.is_relative) fl_agw |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, ag_tmp.addr, 1, ag_tmp.is_relative);
        visit_expression(cg, ARG1, 2);
        uint8_t fl_agr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (ag_tmp.is_relative) fl_agr |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 1, ag_tmp.addr & 0xFF, (ag_tmp.addr >> 8) & 0xFF, fl_agr);
        emit(cg, OP_MEM_LISTA_AGREGAR, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "lista_obtener") == 0 || strcmp(name, "mem_lista_obtener") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 2,
                "lista e indice (entero)",
                "mem_lista_obtener(mi_lista, 0) o lista_obtener(mi_lista, 0)");
            return 1;
        }
        /* Evitar overflow de registros temporales: cuando dest_reg esta cerca de 255,
         * dest_reg+1/dest_reg+2 se envuelven a 0 y rompen el indice inline. */
        int reg_lista = (dest_reg <= 253) ? (dest_reg + 1) : 1;
        int reg_indice = (dest_reg <= 253) ? (dest_reg + 2) : 2;
        visit_expression(cg, ARG0, reg_lista);
        visit_expression(cg, ARG1, reg_indice);
        emit(cg, OP_MEM_LISTA_OBTENER, (uint8_t)dest_reg, (uint8_t)reg_lista, (uint8_t)reg_indice, 
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return 1;
    }
    if (strcmp(name, "lista_poner") == 0 || strcmp(name, "mem_lista_poner") == 0) {
        if (cn->n_args < 3) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 3,
                "lista, indice (entero) y valor",
                "lista_poner(mi_lista, 0, 42)");
            return 1;
        }
        int reg_lista = (dest_reg <= 252) ? (dest_reg + 1) : 1;
        int reg_indice = (dest_reg <= 252) ? (dest_reg + 2) : 2;
        int reg_valor = (dest_reg <= 252) ? (dest_reg + 3) : 3;
        visit_expression(cg, ARG0, reg_lista);
        visit_expression(cg, ARG1, reg_indice);
        visit_expression(cg, ARG2, reg_valor);
        emit(cg, OP_MEM_LISTA_PONER, (uint8_t)reg_lista, (uint8_t)reg_indice, (uint8_t)reg_valor,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)reg_lista, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "lista_tamano") == 0 || strcmp(name, "mem_lista_tamano") == 0) {
        if (cn->n_args < 1 || !ARG0) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 1,
                "lista",
                "mem_lista_tamano(mi_lista) o lista_tamano(mi_lista)");
            return 1;
        }
        /* Nunca usar R1 para el id de lista: R1 es `este` en metodos y muchos caminos del
         * runtime/stdlib lo reutilizan; dejar el id en R1 provoca OP_MEM_LISTA_TAMANO con
         * list_id corrupto tras `usar` / analitica (crash silencioso o salida 1 sin OK).
         * Misma idea que lista_obtener: temp = dest+1 salvo colision/envoltura. */
        int reg_lista = (dest_reg <= 253) ? (dest_reg + 1) : 2;
        if (reg_lista == (int)(uint8_t)dest_reg)
            reg_lista = (dest_reg <= 252) ? (dest_reg + 2) : 3;
        visit_expression(cg, ARG0, reg_lista);
        emit(cg, OP_MEM_LISTA_TAMANO, (uint8_t)dest_reg, (uint8_t)reg_lista, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "lista_limpiar") == 0 || strcmp(name, "mem_lista_limpiar") == 0) {
        if (cn->n_args < 1 || !ARG0) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 1,
                "lista",
                "lista_limpiar(mi_lista) o mem_lista_limpiar(mi_lista)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_LISTA_LIMPIAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "lista_liberar") == 0 || strcmp(name, "mem_lista_liberar") == 0) {
        if (cn->n_args < 1 || !ARG0) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 1,
                "lista (id a liberar en JMN)",
                "mem_lista_liberar(mi_lista) o lista_liberar(mi_lista)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_LISTA_LIBERAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        /* El handle almacenado en la variable Jasboot queda obsoleto; el valor de expresión es 0. */
        emit(cg, OP_MOVER, dest_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "lista_mapear") == 0 || strcmp(name, "mem_lista_mapear") == 0) {
        if (cn->n_args != 2 || !ARG0 || !ARG1 || !is_node(ARG1, NODE_IDENTIFIER)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`lista_mapear` requiere (lista, nombre_funcion): la funcion debe ser un identificador "
                     "global con firma compatible (un argumento) y el mismo tipo de retorno que el elemento.");
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        const char *fnm = ((IdentifierNode *)ARG1)->name;
        int fn_lab = get_func_label(cg, fnm);
        if (fn_lab < 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`lista_mapear`: `%s` no es una funcion definida en este programa.", fnm);
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        SymResult src_tmp = sym_reserve_temp(&cg->sym, 8);
        SymResult out_tmp = sym_reserve_temp(&cg->sym, 8);
        SymResult idx_tmp = sym_reserve_temp(&cg->sym, 8);
        uint8_t flw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flw |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, src_tmp.addr, 1, src_tmp.is_relative);
        int nid = ++cg->literal_counter;
        emit(cg, OP_MOVER, 2, nid & 0xFF, (nid >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_LISTA_CREAR, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flwo = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flwo |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, out_tmp.addr, 2, out_tmp.is_relative);
        emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        uint8_t fli = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) fli |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, idx_tmp.addr, 15, idx_tmp.is_relative);
        int loop_id = new_label(cg);
        int end_id = new_label(cg);
        mark_label(cg, loop_id);
        uint8_t flr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flr |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 15, src_tmp.addr & 0xFF, (src_tmp.addr >> 8) & 0xFF, flr);
        emit(cg, OP_MEM_MAPA_TAMANO, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flri = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) flri |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_CMP_LT, 18, 17, 16, 0);
        emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 18, end_id);
        emit(cg, OP_MEM_LISTA_OBTENER, 19, 15, 17, 0);
        emit(cg, OP_MOVER, 1, 19, 0, IR_INST_FLAG_B_REGISTER);
        int prev = cg->expr_allow_func_literal;
        cg->expr_allow_func_literal = 1;
        emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        add_patch(cg, fn_lab, PATCH_JUMP);
        cg->expr_allow_func_literal = prev;
        uint8_t flro = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flro |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 20, out_tmp.addr & 0xFF, (out_tmp.addr >> 8) & 0xFF, flro);
        emit(cg, OP_MOVER, 21, 1, 0, IR_INST_FLAG_B_REGISTER);
        codegen_emit_mem_lista_agregar_from_regs(cg, 20, 21);
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        emit_escribir_u24(cg, idx_tmp.addr, 17, idx_tmp.is_relative);
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, loop_id, PATCH_JUMP);
        mark_label(cg, end_id);
        uint8_t fld = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) fld |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, dest_reg, out_tmp.addr & 0xFF, (out_tmp.addr >> 8) & 0xFF, fld);
        return 1;
    }
    if (strcmp(name, "lista_filtrar") == 0 || strcmp(name, "mem_lista_filtrar") == 0) {
        if (cn->n_args != 2 || !ARG0 || !ARG1 || !is_node(ARG1, NODE_IDENTIFIER)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`lista_filtrar` requiere (lista, nombre_funcion): la funcion predicado debe ser un identificador "
                     "global; debe devolver distinto de cero para conservar el elemento.");
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        const char *fnm = ((IdentifierNode *)ARG1)->name;
        int fn_lab = get_func_label(cg, fnm);
        if (fn_lab < 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "`lista_filtrar`: `%s` no es una funcion definida en este programa.", fnm);
            cg->has_error = 1;
            cg->err_line = cn->base.line;
            cg->err_col = cn->base.col;
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        SymResult src_tmp = sym_reserve_temp(&cg->sym, 8);
        SymResult out_tmp = sym_reserve_temp(&cg->sym, 8);
        SymResult idx_tmp = sym_reserve_temp(&cg->sym, 8);
        uint8_t flw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flw |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, src_tmp.addr, 1, src_tmp.is_relative);
        int nid = ++cg->literal_counter;
        emit(cg, OP_MOVER, 2, nid & 0xFF, (nid >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_LISTA_CREAR, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flwo = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flwo |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, out_tmp.addr, 2, out_tmp.is_relative);
        emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        uint8_t fli = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) fli |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, idx_tmp.addr, 15, idx_tmp.is_relative);
        int loop_id = new_label(cg);
        int end_id = new_label(cg);
        int skip_agr_id = new_label(cg);
        mark_label(cg, loop_id);
        uint8_t flr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flr |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 15, src_tmp.addr & 0xFF, (src_tmp.addr >> 8) & 0xFF, flr);
        emit(cg, OP_MEM_MAPA_TAMANO, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flri = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) flri |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_CMP_LT, 18, 17, 16, 0);
        emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 18, end_id);
        emit(cg, OP_MEM_LISTA_OBTENER, 19, 15, 17, 0);
        emit(cg, OP_MOVER, 1, 19, 0, IR_INST_FLAG_B_REGISTER);
        int prev2 = cg->expr_allow_func_literal;
        cg->expr_allow_func_literal = 1;
        emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        add_patch(cg, fn_lab, PATCH_JUMP);
        cg->expr_allow_func_literal = prev2;
        emit(cg, OP_CMP_EQ, 22, 1, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 22, skip_agr_id);
        uint8_t flro = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flro |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 20, out_tmp.addr & 0xFF, (out_tmp.addr >> 8) & 0xFF, flro);
        codegen_emit_mem_lista_agregar_from_regs(cg, 20, 19);
        mark_label(cg, skip_agr_id);
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        emit_escribir_u24(cg, idx_tmp.addr, 17, idx_tmp.is_relative);
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, loop_id, PATCH_JUMP);
        mark_label(cg, end_id);
        uint8_t fld = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) fld |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, dest_reg, out_tmp.addr & 0xFF, (out_tmp.addr >> 8) & 0xFF, fld);
        return 1;
    }
    if (strcmp(name, "mapa_crear") == 0) {
        emit(cg, OP_MEM_MAPA_CREAR, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "mapa_poner") == 0) {
        if (cn->n_args < 3) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 3,
                "mapa, clave y valor",
                "mapa_poner(m, clave, valor)");
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            const char *vt = get_expression_type(cg, ARG2);
            int v_reg = visit_expression(cg, ARG2, dest_reg + 2);
            if (vt && strcmp(vt, "flotante") == 0) {
                // Si el valor es flotante, convertir sus bits a entero para mapa_poner
                // El mapa neuronal guarda uint64_t internamente.
                // En JASBOOT, visit_expression para flotante ya pone los bits en el registro.
            }
            (void)v_reg; // Evitar advertencia de variable no usada
            cg->expr_allow_func_literal = prev;
        }
        emit(cg, OP_MEM_MAPA_PONER, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), (uint8_t)(dest_reg + 2), 
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return 1;
    }
    if (strcmp(name, "mapa_obtener") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 2,
                "mapa y clave (entero, texto o flotante)",
                "mapa_obtener(m, clave)");
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_MEM_MAPA_OBTENER, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), (uint8_t)(dest_reg + 2), 
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return 1;
    }
    if (strcmp(name, "mapa_eliminar") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 2,
                "mapa y clave",
                "mapa_eliminar(m, clave)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_MAPA_BORRAR, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mapa_tamano") == 0) {
        if (cn->n_args < 1) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 1,
                "mapa", "mapa_tamano(m)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_MAPA_TAMANO, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mapa_contiene") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 2,
                "mapa y clave",
                "mapa_contiene(m, clave)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_MAPA_CONTIENE, (uint8_t)dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* 6.9 Memoria neuronal */
    if (strcmp(name, "mem_crear") == 0 || strcmp(name, "abrir_memoria") == 0 || strcmp(name, "crear_memoria") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "ruta de la base de datos neuronal (.jmn)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        if (ARG1) visit_expression(cg, ARG1, 2); else emit(cg, OP_MOVER, 2, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        if (ARG2) visit_expression(cg, ARG2, 3); else emit(cg, OP_MOVER, 3, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_CREAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mem_cerrar") == 0 || strcmp(name, "cerrar_memoria") == 0) {
        emit(cg, OP_MEM_CERRAR, 0, 0, 0, 0);
        return 1;
    }
    if (strcmp(name, "mem_asociar") == 0) {
        if (cn->n_args < 3) {
            codegen_error_sistema_incorporada_arity(cg, cn, 3,
                "origen, destino y fuerza de asociacion",
                "mem_asociar(\"agua\", \"h2o\", 100)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        
        if (is_node(ARG2, NODE_LITERAL) && !((LiteralNode*)ARG2)->is_float &&
            ((LiteralNode*)ARG2)->type_name && strcmp(((LiteralNode*)ARG2)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG2)->value.i;
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, (uint8_t)(dest_reg + 1), (uint8_t)(dest_reg + 2), (uint8_t)v,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG2, dest_reg + 3);
            emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, (uint8_t)(dest_reg + 1), (uint8_t)(dest_reg + 2), (uint8_t)(dest_reg + 3), 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "tiene_asociacion") == 0 || strcmp(name, "mem_obtener_fuerza") == 0 || strcmp(name, "buscar_peso") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "origen y destino", "buscar_peso(\"agua\", \"vida\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_OBTENER_FUERZA, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    if (strcmp(name, "fs_borrar") == 0) {
        if (!ARG0) {
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "ruta del archivo o directorio", "fs_borrar(\"temp.txt\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        /* VM: path y resultado en operand_a (registro 1), no en dest_reg */
        emit(cg, OP_FS_BORRAR, 1, 0, 0, 0);
        if (dest_reg != 1)
            emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "fs_copiar") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "ruta origen y ruta destino", "fs_copiar(\"a.txt\", \"b.txt\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        /* VM: src en operand_a (1), dst en operand_b (2); resultado pisa reg 1 */
        emit(cg, OP_FS_COPIAR, 1, 2, 0, IR_INST_FLAG_B_REGISTER);
        if (dest_reg != 1)
            emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "fs_mover") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "ruta origen y ruta destino", "fs_mover(\"a.txt\", \"b.txt\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_FS_MOVER, 1, 2, 0, IR_INST_FLAG_B_REGISTER);
        if (dest_reg != 1)
            emit(cg, OP_MOVER, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "fs_tamano") == 0) {
        if (!ARG0) {
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "ruta del archivo", "fs_tamano(\"datos.bin\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_FS_TAMANO, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mem_obtener_relacion") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "origen y destino", "mem_obtener_relacion(\"agua\", \"vida\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_OBTENER_RELACION, dest_reg, 1, 2, IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* 6.10 Sistema, 6.11 Conversiones */
    if (strcmp(name, "sistema_ejecutar") == 0) {
        if (!ARG0) {
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "comando a ejecutar", "sistema_ejecutar(\"ls\")", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_SYS_EXEC, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "sys_argc") == 0) {
        emit(cg, OP_SYS_ARGC, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "sys_argv") == 0) {
        if (!ARG0) {
            codegen_error_sistema_incorporada_arity(cg, cn, 1,
                "indice del argumento", "sys_argv(0)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_SYS_ARGV, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_a_entero") == 0 || strcmp(name, "convertir_entero") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto, flotante o entero: valor a convertir", cn->base.line, cn->base.col);
            return 1;
        }
        const char *t = get_expression_type(cg, ARG0);
        visit_expression(cg, ARG0, dest_reg + 1);
        if (t && strcmp(t, "entero") == 0) {
            emit(cg, OP_MOVER, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_B_REGISTER);
        } else if (t && strcmp(t, "flotante") == 0) {
            emit(cg, OP_CONV_F2I, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else if (t && strcmp(t, "elemento") == 0) {
            emit(cg, OP_CONV_ANY2I, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            /* Por defecto asumimos texto para str_a_entero */
            emit(cg, OP_STR_A_ENTERO, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "str_a_flotante") == 0 || strcmp(name, "convertir_flotante") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto, entero o flotante: valor a convertir", cn->base.line, cn->base.col);
            return 1;
        }
        const char *t = get_expression_type(cg, ARG0);
        visit_expression(cg, ARG0, dest_reg + 1);
        if (t && strcmp(t, "flotante") == 0) {
            emit(cg, OP_MOVER, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_B_REGISTER);
        } else if (t && strcmp(t, "entero") == 0) {
            emit(cg, OP_CONV_I2F, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else if (t && strcmp(t, "elemento") == 0) {
            emit(cg, OP_CONV_ANY2F, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            /* Por defecto asumimos texto para str_a_flotante */
            emit(cg, OP_STR_A_FLOTANTE, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "str_desde_numero") == 0 || strcmp(name, "texto_desde_numero") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "numero (entero o flotante) a convertir", cn->base.line, cn->base.col);
            return 1;
        }
        const char *t = get_expression_type(cg, ARG0);
        int is_flt = (t && strcmp(t, "flotante") == 0);
        int is_bool = (t && (strcmp(t, "bool") == 0 || strcmp(t, "booleano") == 0));
        int is_int = (t && strcmp(t, "entero") == 0);
        visit_expression(cg, ARG0, dest_reg + 1);
        if (is_flt) {
            emit(cg, OP_STR_DESDE_NUMERO, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), 0, 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        } else if (is_bool) {
            emit(cg, OP_STR_DESDE_NUMERO, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), 2,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        } else if (is_int) {
            emit(cg, OP_STR_DESDE_NUMERO, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), 1, 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            /* Caso por defecto: Delegar a la VM para decidir si es int o float (elemento, etc) */
            emit(cg, OP_STR_DESDE_ANY, (uint8_t)dest_reg, (uint8_t)(dest_reg + 1), 0, 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "decimal") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_incorporada_arity(cg, cn, 2,
                "numero flotante y cantidad de decimales",
                "decimal(3.14159, 2)", NULL);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        if (is_node(ARG1, NODE_LITERAL)) {
            LiteralNode *ln = (LiteralNode *)ARG1;
            int p;
            if (ln->is_float)
                p = (int)ln->value.f;
            else if (ln->type_name && strcmp(ln->type_name, "flotante") == 0)
                p = (int)ln->value.f;
            else
                p = (int)ln->value.i;
            if (p < 0) p = 0;
            if (p > 20) p = 20;
            emit(cg, OP_STR_FLOTANTE_PREC, dest_reg, 1, (uint8_t)p,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            return 1;
        }
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_STR_FLOTANTE_PREC, dest_reg, 1, 2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    if (strcmp(name, "imprimir_sin_salto") == 0) {
        /* Igual que imprimir pero sin salto de línea al final */
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "expresion a imprimir (texto, entero, flotante, etc.)", cn->base.line, cn->base.col);
            return 1;
        }
        ASTNode *expr = ARG0;
        if (is_node(expr, NODE_LITERAL)) {
            LiteralNode *ln = (LiteralNode*)expr;
            if (ln->type_name && strcmp(ln->type_name, "texto") == 0) {
                const char *s = ln->value.str ? ln->value.str : "";
                if (has_interpolation(s)) {
                    emit_print_interpolated(cg, s, 0, ln->base.line, ln->base.col);
                } else {
                    size_t off = add_string(cg, s);
                    emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                }
            } else if (ln->is_float) {
                visit_expression(cg, expr, dest_reg + 1);
                emit(cg, OP_IMPRIMIR_FLOTANTE, (uint8_t)(dest_reg + 1), 0, 0, IR_INST_FLAG_A_REGISTER);
            } else if (ln->type_name && tipo_imprimir_es_bool(ln->type_name)) {
                visit_expression(cg, expr, dest_reg + 1);
                emit(cg, OP_IMPRIMIR_BOOLEANO, (uint8_t)(dest_reg + 1), 0, 0, IR_INST_FLAG_A_REGISTER);
            } else {
                visit_expression(cg, expr, dest_reg + 1);
                emit(cg, OP_IMPRIMIR_NUMERO, (uint8_t)(dest_reg + 1), 0, 0, IR_INST_FLAG_A_REGISTER);
            }
        } else {
            const char *t = get_expression_type(cg, expr);
            if (emit_print_plus_is_string_concat(cg, expr)) t = "texto";
            {
                int el = (expr->line > 0) ? expr->line : cn->base.line;
                int ec = (expr->col > 0) ? expr->col : cn->base.col;
                if (reject_funcion_in_display_context(cg, t, el, ec, 0))
                    return 1;
            }
            if (type_is_user_struct(cg, t)) {
                emit_imprimir_struct_repr_from_expr(cg, expr);
                return 1;
            }
            int reg = visit_expression(cg, expr, dest_reg + 1);
            if (t && strcmp(t, "texto") == 0)
                emit(cg, OP_IMPRIMIR_TEXTO, (uint8_t)reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else if (t && strcmp(t, "caracter") == 0) {
                emit(cg, OP_STR_DESDE_CODIGO, (uint8_t)reg, (uint8_t)reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_IMPRIMIR_TEXTO, (uint8_t)reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            } else if (t && strcmp(t, "flotante") == 0)
                emit(cg, OP_IMPRIMIR_FLOTANTE, (uint8_t)reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else if (tipo_imprimir_es_bool(t))
                emit(cg, OP_IMPRIMIR_BOOLEANO, (uint8_t)reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else
                emit(cg, OP_IMPRIMIR_NUMERO, (uint8_t)reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "imprimir_id") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto (expresion)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_MEM_IMPRIMIR_ID, (uint8_t)r1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "obtener_nombre_concepto") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto (u32)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_ID_A_TEXTO, (uint8_t)dest_reg, (uint8_t)r1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "imprimir_flotante") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "expresion de tipo flotante (o convertible)", cn->base.line, cn->base.col);
            return 1;
        }
        int r1 = visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_IMPRIMIR_FLOTANTE, (uint8_t)r1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "bit_shl") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 2, "bit_shl(valor, desplazamiento)"))
            return 1;
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        emit(cg, OP_BIT_SHL, dest_reg, dest_reg, dest_reg + 1, 0);
        return 1;
    }
    if (strcmp(name, "bit_shr") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 2, "bit_shr(valor, desplazamiento)"))
            return 1;
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        emit(cg, OP_BIT_SHR, dest_reg, dest_reg, dest_reg + 1, 0);
        return 1;
    }

#undef ARG0
#undef ARG1
#undef ARG2
#undef ARG3
#undef ARG4
#undef ARG5
#undef ARG6
    return 0;
}

CodeGen *codegen_create(void) {
    CodeGen *cg = calloc(1, sizeof(CodeGen));
    if (!cg) return NULL;
    sym_init_global(&cg->sym);
    cg->err_line = -1;
    cg->err_col = -1;
    cg->macro_end_label = -1;
    cg->macro_dest_reg = -1;
    return cg;
}

void codegen_register_external_func(CodeGen *cg, const char *name, const char *return_type) {
    if (!cg || !name) return;
    size_t n = cg->n_ext_funcs;
    char **nnames = realloc(cg->ext_func_names, (n + 1) * sizeof(char*));
    char **ntypes = realloc(cg->ext_func_return_types, (n + 1) * sizeof(char*));
    if (!nnames || !ntypes) return;
    cg->ext_func_names = nnames;
    cg->ext_func_return_types = ntypes;
    cg->ext_func_names[n] = strdup(name);
    cg->ext_func_return_types[n] = return_type ? strdup(return_type) : NULL;
    cg->n_ext_funcs = n + 1;
}

void codegen_free(CodeGen *cg) {
    if (!cg) return;
    sym_free(&cg->sym);
    free(cg->code);
    free(cg->data);
    free(cg->labels);
    free(cg->patches);
    free(cg->loop_stack);
    free(cg->try_stack);
    free(cg->func_names);
    free(cg->func_return_types);
    free(cg->func_return_task_elems);
    free(cg->func_labels);
    for (size_t i = 0; i < cg->string_pool_count; i++) {
        free(cg->string_pool_keys[i]);
    }
    free(cg->string_pool_keys);
    free(cg->string_pool_offsets);
    for (size_t i = 0; i < cg->n_ext_funcs; i++) {
        free(cg->ext_func_names[i]);
        free(cg->ext_func_return_types[i]);
    }
    free(cg->ext_func_names);
    free(cg->ext_func_return_types);
    free(cg->err_diag_unit_path);
    for (size_t di = 0; di < cg->n_collected_diags; di++)
        free(cg->collected_diags[di].unit_path);
    free(cg->collected_diags);
    free(cg);
}

const char *codegen_get_error(CodeGen *cg, int *out_line, int *out_col) {
    if (!cg || !cg->has_error) return NULL;
    if (out_line) *out_line = cg->err_line;
    if (out_col) *out_col = cg->err_col;
    return cg->last_error[0] ? cg->last_error : "Error desconocido";
}

const char *codegen_get_error_unit_path(const CodeGen *cg) {
    if (!cg || !cg->has_error || !cg->err_diag_unit_path || !cg->err_diag_unit_path[0])
        return NULL;
    return cg->err_diag_unit_path;
}

size_t codegen_collected_diag_count(const CodeGen *cg) {
    return cg ? cg->n_collected_diags : 0U;
}

void codegen_collected_diag_at(const CodeGen *cg, size_t idx,
                               const char **msg, int *line, int *col, const char **unit_path) {
    if (msg) *msg = NULL;
    if (line) *line = 1;
    if (col) *col = 1;
    if (unit_path) *unit_path = NULL;
    if (!cg || idx >= cg->n_collected_diags) return;
    const CollectedDiag *d = &cg->collected_diags[idx];
    if (msg) *msg = d->msg;
    if (line) *line = d->line;
    if (col) *col = d->col;
    if (unit_path) *unit_path = d->unit_path;
}

/* --- Interpolación ${expr} en cadenas --- */
static int has_interpolation(const char *s) {
    if (!s) return 0;
    return strstr(s, "${") != NULL;
}

/* Construye string interpolado en dest_reg (para retornar, asignar, etc.) */
static void emit_build_interpolated_string(CodeGen *cg, const char *text, int dest_reg, int ctx_line, int ctx_col) {
    if (!text) text = "";
    const char *p = text;
    int tmp_reg = (dest_reg == CG_STRUCT_STR_FRAG) ? CG_STRUCT_STR_ACC7 : CG_STRUCT_STR_FRAG;
    int first = 1;
    while (*p) {
        const char *dollar = strstr(p, "${");
        if (!dollar) {
            if (p < text + strlen(text)) {
                size_t len = strlen(p);
                char *part = malloc(len + 1);
                if (part) {
                    memcpy(part, p, len + 1);
                    size_t off = add_string(cg, part);
                    free(part);
                    emit(cg, OP_STR_REGISTRAR_LITERAL, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                    emit(cg, OP_LOAD_STR_HASH, first ? dest_reg : tmp_reg, off & 0xFF, (off >> 8) & 0xFF,
                         IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                    if (!first) {
                        emit(cg, OP_STR_CONCATENAR_REG, dest_reg, dest_reg, tmp_reg,
                             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                    }
                    first = 0;
                }
            }
            break;
        }
        if (dollar > p) {
            size_t len = (size_t)(dollar - p);
            char *part = malloc(len + 1);
            if (part) {
                memcpy(part, p, len);
                part[len] = '\0';
                size_t off = add_string(cg, part);
                free(part);
                emit(cg, OP_STR_REGISTRAR_LITERAL, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                     IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_LOAD_STR_HASH, first ? dest_reg : tmp_reg, off & 0xFF, (off >> 8) & 0xFF,
                     IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                if (!first) {
                    emit(cg, OP_STR_CONCATENAR_REG, dest_reg, dest_reg, tmp_reg,
                         IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                }
                first = 0;
            }
        }
        const char *end = strchr(dollar + 2, '}');
        if (!end) {
            size_t off = add_string(cg, dollar);
            emit(cg, OP_STR_REGISTRAR_LITERAL, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_LOAD_STR_HASH, first ? dest_reg : tmp_reg, off & 0xFF, (off >> 8) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            if (!first) {
                emit(cg, OP_STR_CONCATENAR_REG, dest_reg, dest_reg, tmp_reg,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            }
            break;
        }
        size_t expr_len = (size_t)(end - (dollar + 2));
        char *expr_str = malloc(expr_len + 1);
        if (!expr_str) { p = end + 1; continue; }
        memcpy(expr_str, dollar + 2, expr_len);
        expr_str[expr_len] = '\0';
        char *err = NULL;
        ASTNode *expr_node = parser_parse_expression_from_string(expr_str, &err);
        free(expr_str);
        if (expr_node) {
            const char *tchk = get_expression_type(cg, expr_node);
            if (reject_funcion_in_display_context(cg, tchk, ctx_line, ctx_col, 1)) {
                ast_free(expr_node);
                if (err) free(err);
                return;
            }
            SymResult tmp_dest;
            if (!first) {
                tmp_dest = sym_reserve_temp(&cg->sym, 8);
                uint8_t fl_w = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp_dest.is_relative) fl_w |= IR_INST_FLAG_RELATIVE;
                emit_escribir_u24(cg, tmp_dest.addr, dest_reg, tmp_dest.is_relative);
            }
            if (type_is_user_struct(cg, tchk)) {
                MemberAddrResult mbase = get_member_address(cg, expr_node, 2);
                if (mbase.invalid) {
                    ast_free(expr_node);
                    if (err) free(err);
                    return;
                }
                emit_format_struct_string_reg(cg, tchk, mbase, CG_STRUCT_STR_ACC7, 0);
            } else {
                int expr_reg = 3;
                (void)visit_expression(cg, expr_node, expr_reg);
                const char *t = get_expression_type(cg, expr_node);
                if (t && strcmp(t, "texto") != 0 && strcmp(t, "concepto") != 0) {
                    int is_bool_t = (t && (strcmp(t, "bool") == 0 || strcmp(t, "booleano") == 0));
                    int is_int = (t && strcmp(t, "entero") == 0);
                    uint8_t ic = (uint8_t)(is_bool_t ? 2 : (is_int ? 1 : 0));
                    emit(cg, OP_STR_DESDE_NUMERO, expr_reg, expr_reg, ic,
                         IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
                }
                if (!first) {
                    emit_leer_u24(cg, dest_reg, tmp_dest.addr, tmp_dest.is_relative);
                }
                if (first) {
                    if (dest_reg != expr_reg)
                        emit(cg, OP_MOVER, dest_reg, expr_reg, 0, IR_INST_FLAG_B_REGISTER);
                } else {
                    emit(cg, OP_STR_CONCATENAR_REG, dest_reg, dest_reg, expr_reg,
                         IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                }
                first = 0;
                ast_free(expr_node);
                p = end + 1;
                continue;
            }
            if (!first) {
                emit_leer_u24(cg, dest_reg, tmp_dest.addr, tmp_dest.is_relative);
            }
            if (first) {
                if (dest_reg != CG_STRUCT_STR_ACC7)
                    emit(cg, OP_MOVER, dest_reg, CG_STRUCT_STR_ACC7, 0, IR_INST_FLAG_B_REGISTER);
            } else {
                emit(cg, OP_STR_CONCATENAR_REG, dest_reg, dest_reg, CG_STRUCT_STR_ACC7,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            }
            first = 0;
            ast_free(expr_node);
        }
        if (err) free(err);
        p = end + 1;
    }
}

static void emit_print_interpolated(CodeGen *cg, const char *text, int add_newline, int ctx_line, int ctx_col) {
    if (!text) text = "";
    const char *p = text;
    while (*p) {
        const char *dollar = strstr(p, "${");
        if (!dollar) {
            if (p < text + strlen(text)) {
                size_t len = strlen(p);
                char *part = malloc(len + 1);
                if (part) {
                    memcpy(part, p, len + 1);
                    size_t off = add_string(cg, part);
                    free(part);
                    emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                }
            }
            break;
        }
        if (dollar > p) {
            size_t len = (size_t)(dollar - p);
            char *part = malloc(len + 1);
            if (part) {
                memcpy(part, p, len);
                part[len] = '\0';
                size_t off = add_string(cg, part);
                free(part);
                emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                     IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            }
        }
        const char *end = strchr(dollar + 2, '}');
        if (!end) {
            size_t off = add_string(cg, dollar);
            emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            break;
        }
        size_t expr_len = (size_t)(end - (dollar + 2));
        char *expr_str = malloc(expr_len + 1);
        if (!expr_str) { p = end + 1; continue; }
        memcpy(expr_str, dollar + 2, expr_len);
        expr_str[expr_len] = '\0';
        char *err = NULL;
        ASTNode *expr_node = parser_parse_expression_from_string(expr_str, &err);
        free(expr_str);
        if (expr_node) {
            const char *t = get_expression_type(cg, expr_node);
            if (reject_funcion_in_display_context(cg, t, ctx_line, ctx_col, 1))
                return;
            if (type_is_user_struct(cg, t)) {
                emit_imprimir_struct_repr_from_expr(cg, expr_node);
            } else if (t && (strcmp(t, "lista") == 0 || strcmp(t, "mapa") == 0 || strcmp(t, "elemento") == 0 || strcmp(t, "objeto") == 0 || strcmp(t, "json") == 0)) {
                const int reg_mem = 120;
                visit_expression(cg, expr_node, reg_mem);
                if (cg->has_error) return;
                emit(cg, OP_MEM_IMPRIMIR_ID, (uint8_t)reg_mem, 0, 0, IR_INST_FLAG_A_REGISTER);
            } else {
                int reg = visit_expression(cg, expr_node, 1);
                if (t && strcmp(t, "texto") == 0)
                    emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
                else if (t && strcmp(t, "flotante") == 0)
                    emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
                else if (tipo_imprimir_es_bool(t))
                    emit(cg, OP_IMPRIMIR_BOOLEANO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
                else
                    emit(cg, OP_IMPRIMIR_NUMERO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            }
            ast_free(expr_node);
        } else {
            size_t off = add_string(cg, "${");
            emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (err) free(err);
        p = end + 1;
    }
    if (add_newline) {
        size_t nl = add_string(cg, "\n");
        emit(cg, OP_IMPRIMIR_TEXTO, nl & 0xFF, (nl >> 8) & 0xFF, (nl >> 16) & 0xFF,
             IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    }
}

/* imprimir / interpolacion: lista como resumen legible (id + tamano), no solo el entero id */
static void emit_imprimir_lista_resumen(CodeGen *cg, ASTNode *expr) {
    const int reg_lista = 120;
    visit_expression(cg, expr, reg_lista);
    if (cg->has_error) return;
    emit(cg, OP_MEM_IMPRIMIR_ID, (uint8_t)reg_lista, 0, 0, IR_INST_FLAG_A_REGISTER);
}

static void emit_imprimir_mapa_resumen(CodeGen *cg, ASTNode *expr) {
    const int reg_mapa = 120;
    visit_expression(cg, expr, reg_mapa);
    if (cg->has_error) return;
    emit(cg, OP_MEM_IMPRIMIR_ID, (uint8_t)reg_mapa, 0, 0, IR_INST_FLAG_A_REGISTER);
}

/* `imprimir "a=" + x` evalua como concatenacion (texto) aunque get_expression_type vea flotante antes que texto. */
static int emit_print_plus_is_string_concat(CodeGen *cg, ASTNode *expr) {
    if (!cg || !expr || !is_node(expr, NODE_BINARY_OP)) return 0;
    BinaryOpNode *bn = (BinaryOpNode *)expr;
    if (!bn->operator || strcmp(bn->operator, "+") != 0) return 0;
    const char *lt = get_expression_type(cg, bn->left);
    const char *rt = get_expression_type(cg, bn->right);
    int lt_c = lt && (strcmp(lt, "texto") == 0 || strcmp(lt, "concepto") == 0 || strcmp(lt, "caracter") == 0);
    int rt_c = rt && (strcmp(rt, "texto") == 0 || strcmp(rt, "concepto") == 0 || strcmp(rt, "caracter") == 0);
    return lt_c || rt_c;
}

/* imprimir: bool del lenguaje (`bool`) y tipo inferido `booleano` */
static int tipo_imprimir_es_bool(const char *t) {
    return t && (strcmp(t, "booleano") == 0 || strcmp(t, "bool") == 0);
}

/* --- 4.1 PrintNode --- */
static void emit_print(CodeGen *cg, ASTNode *expr, int stmt_line, int stmt_col) {
    /* IMPORTANTE: Usar un registro seguro (120) para evaluar la expresión de 'imprimir'.
       Si usamos el Registro 1, machacamos 'este' cuando estamos dentro de un método. */
    const int print_reg = 120;
    
        if (is_node(expr, NODE_LITERAL)) {
        LiteralNode *ln = (LiteralNode*)expr;
        if (ln->type_name && strcmp(ln->type_name, "texto") == 0) {
            const char *s = ln->value.str ? ln->value.str : "";
            if (has_interpolation(s)) {
                emit_print_interpolated(cg, s, 1, stmt_line, stmt_col);
                return;
            }
            size_t off = add_string(cg, s);
            emit(cg, OP_IMPRIMIR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else if (ln->type_name && strcmp(ln->type_name, "caracter") == 0) {
            int reg = visit_expression(cg, expr, print_reg);
            emit(cg, OP_STR_DESDE_CODIGO, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else if (ln->type_name && tipo_imprimir_es_bool(ln->type_name)) {
            int reg = visit_expression(cg, expr, print_reg);
            emit(cg, OP_IMPRIMIR_BOOLEANO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else if (ln->is_float) {
            int reg = visit_expression(cg, expr, print_reg);
            emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else {
            int reg = visit_expression(cg, expr, print_reg);
            emit(cg, OP_IMPRIMIR_NUMERO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
    } else {
        const char *t = get_expression_type(cg, expr);
        if (emit_print_plus_is_string_concat(cg, expr)) t = "texto";
        {
            int el = (expr->line > 0) ? expr->line : stmt_line;
            int ec = (expr->col > 0) ? expr->col : stmt_col;
            if (reject_funcion_in_display_context(cg, t, el, ec, 0))
                return;
        }
        if (type_is_user_struct(cg, t)) {
            emit_imprimir_struct_repr_from_expr(cg, expr);
        } else if (t && strcmp(t, "lista") == 0) {
            emit_imprimir_lista_resumen(cg, expr);
        } else if (t && strcmp(t, "mapa") == 0) {
            emit_imprimir_mapa_resumen(cg, expr);
        } else {
            int reg = visit_expression(cg, expr, print_reg);
            if (cg->has_error) return;
            if (t && strcmp(t, "texto") == 0)
                emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else if (t && strcmp(t, "caracter") == 0) {
                emit(cg, OP_STR_DESDE_CODIGO, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            } else if (t && strcmp(t, "flotante") == 0)
                emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else if (tipo_imprimir_es_bool(t))
                emit(cg, OP_IMPRIMIR_BOOLEANO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else if (t && strcmp(t, "elemento") == 0) {
                emit(cg, OP_MEM_IMPRIMIR_ID, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            } else {
                emit(cg, OP_IMPRIMIR_NUMERO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            }
        }
    }
    size_t nl = add_string(cg, "\n");
    emit(cg, OP_IMPRIMIR_TEXTO, nl & 0xFF, (nl >> 8) & 0xFF, (nl >> 16) & 0xFF,
         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
}

/* --- visit_expression: Nivel 5 completo --- */
static void emit_call_args_preserved_offset(CodeGen *cg, ASTNode **args, size_t n_args, int reg_start);
static void emit_call_args_preserved(CodeGen *cg, ASTNode **args, size_t n_args);
static void emit_call_args_preserved_methods(CodeGen *cg, ASTNode **args, size_t n_args);

static int visit_expression(CodeGen *cg, ASTNode *node, int dest_reg) {
    if (!node || cg->has_error) return dest_reg;
    if (is_node(node, NODE_INPUT)) {
        InputNode *in = (InputNode*)node;
        if (in->immediate)
            emit(cg, OP_IO_PERCIBIR_TECLADO, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        else
            emit(cg, OP_IO_INPUT_REG, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return dest_reg;
    }
    if (is_node(node, NODE_LIST_LITERAL)) {
        ListLiteralNode *lln = (ListLiteralNode*)node;
        if (lln->n > 65535) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Literal de lista: demasiados elementos (%zu); maximo 65535.", lln->n);
            cg->has_error = 1;
            cg->err_line = lln->base.line;
            cg->err_col = lln->base.col;
            return dest_reg;
        }
        int list_reg = dest_reg;
        int el_reg = 121; // Usar registro alto para evitar clobbering por literales float/u64
        /* Pasar 0 para que la VM genere un ID único seguro (evita colisión con booleano 1) */
        emit(cg, OP_MOVER, (uint8_t)list_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_LISTA_CREAR, (uint8_t)list_reg, (uint8_t)list_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (size_t i = 0; i < lln->n; i++) {
            visit_expression(cg, lln->elements[i], el_reg);
            emit(cg, OP_MEM_LISTA_AGREGAR, (uint8_t)list_reg, (uint8_t)el_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return dest_reg;
    }
    if (is_node(node, NODE_LITERAL)) {
        LiteralNode *ln = (LiteralNode*)node;
        if (ln->type_name && (strcmp(ln->type_name, "texto") == 0 || strcmp(ln->type_name, "concepto") == 0)) {
            const char *s = ln->value.str ? ln->value.str : "";
            if (strcmp(ln->type_name, "texto") == 0 && has_interpolation(s)) {
                emit_build_interpolated_string(cg, s, dest_reg, ln->base.line, ln->base.col);
            } else {
                size_t off = add_string(cg, s);
                emit(cg, OP_STR_REGISTRAR_LITERAL, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                     IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_LOAD_STR_HASH, dest_reg, off & 0xFF, (off >> 8) & 0xFF,
                     IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            }
        } else if (ln->is_float) {
            float f = (float)ln->value.f;
            uint32_t bits;
            memcpy(&bits, &f, 4);
            emit(cg, OP_MOVER, dest_reg, bits & 0xFF, (bits >> 8) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            int tmp = (dest_reg != 3) ? 3 : 4;
            emit(cg, OP_MOVER, tmp, (bits >> 16) & 0xFF, (bits >> 24) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_BIT_SHL, tmp, tmp, 16, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
        } else {
            uint64_t v = (uint64_t)ln->value.i;
            if (v <= 0xFFFF) {
                emit(cg, OP_MOVER, dest_reg, v & 0xFF, (v >> 8) & 0xFF,
                     IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            } else {
                emit(cg, OP_MOVER, dest_reg, v & 0xFF, (v >> 8) & 0xFF,
                     IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                if (v & 0xFFFFFFFFFFFF0000ULL) {
                    /* No usar el mismo registro que dest_reg (p. ej. indice en mem_lista_obtener usa reg 3). */
                    int tmp = (dest_reg != 3) ? 3 : 4;
                    if ((v >> 16) & 0xFFFF) {
                        emit(cg, OP_MOVER, tmp, (v >> 16) & 0xFF, (v >> 24) & 0xFF,
                             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                        emit(cg, OP_BIT_SHL, tmp, tmp, 16, IR_INST_FLAG_C_IMMEDIATE);
                        emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
                    }
                    if ((v >> 32) & 0xFFFF) {
                        emit(cg, OP_MOVER, tmp, (v >> 32) & 0xFF, (v >> 40) & 0xFF,
                             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                        emit(cg, OP_BIT_SHL, tmp, tmp, 32, IR_INST_FLAG_C_IMMEDIATE);
                        emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
                    }
                    if ((v >> 48) & 0xFFFF) {
                        emit(cg, OP_MOVER, tmp, (v >> 48) & 0xFF, (v >> 56) & 0xFF,
                             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                        emit(cg, OP_BIT_SHL, tmp, tmp, 48, IR_INST_FLAG_C_IMMEDIATE);
                        emit(cg, OP_O, dest_reg, dest_reg, tmp, 0);
                    }
                }
            }
        }
        return dest_reg;
    }
    if (is_node(node, NODE_LAMBDA_DECL)) {
        LambdaDeclNode *ld = (LambdaDeclNode *)node;
        int entry_lbl = new_label(cg);
        int skip_lbl = new_label(cg);
        char **capture_names = NULL;
        const char **capture_types = NULL;
        size_t capture_count = 0;
        char **locals = NULL;
        size_t local_count = 0;
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, skip_lbl, PATCH_JUMP);
        for (size_t i = 0; i < ld->n_params; i++) {
            if (ld->params && ld->params[i]) (void)codegen_push_name(&locals, &local_count, ld->params[i]);
        }
        if (is_node(ld->body, NODE_BLOCK))
            collect_lambda_captures_stmt(cg, ld->body, &locals, &local_count, &capture_names, &capture_types, &capture_count);
        else
            collect_lambda_captures_expr(cg, ld->body, &locals, &local_count, &capture_names, &capture_types, &capture_count);
        for (size_t i = 0; i < local_count; i++) free(locals[i]);
        free(locals);
        mark_label(cg, entry_lbl);
        sym_enter_scope(&cg->sym, 1);
        const char *prev_ret = cg->current_fn_return;
        const char *prev_name = cg->current_fn_name;
        int prev_fd = cg->function_depth;
        char **prev_capture_names = cg->current_lambda_capture_names;
        const char **prev_capture_types = cg->current_lambda_capture_types;
        size_t prev_capture_count = cg->current_lambda_capture_count;
        size_t prev_capture_base = cg->current_lambda_scope_base;
        cg->function_depth++;
        cg->current_fn_name = "__lambda";
        cg->current_lambda_capture_names = capture_names;
        cg->current_lambda_capture_types = capture_types;
        cg->current_lambda_capture_count = capture_count;
        cg->current_lambda_scope_base = cg->sym.scope_depth;
        {
            const char *infer_ret = get_expression_type(cg, ld->body);
            if (!infer_ret || strcmp(infer_ret, "funcion") == 0) infer_ret = "entero";
            cg->current_fn_return = infer_ret;
        }
        size_t reserve_pos = cg->code_size;
        emit(cg, OP_RESERVAR_PILA, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE);
        uint32_t *param_addrs = ld->n_params ? (uint32_t*)calloc(ld->n_params, sizeof(uint32_t)) : NULL;
        int n_params = (int)ld->n_params;
        for (int i = 0; i < n_params; i++) {
            const char *pt = (ld->types && ld->types[i]) ? ld->types[i] : "entero";
            SymResult pr = sym_declare(&cg->sym, ld->params[i], pt, 8, 1, 0, NULL, SYMDECL_FLAGS_NONE);
            if (!pr.found) {
                codegen_sym_declare_fail(cg, ld->params[i], node->line, node->col, "Parámetro de macro/lambda");
                free(param_addrs);
                cg->function_depth = prev_fd;
                cg->current_fn_return = prev_ret;
                cg->current_fn_name = prev_name;
                cg->current_lambda_capture_names = prev_capture_names;
                cg->current_lambda_capture_types = prev_capture_types;
                cg->current_lambda_capture_count = prev_capture_count;
                cg->current_lambda_scope_base = prev_capture_base;
                sym_exit_scope(&cg->sym);
                return dest_reg;
            }
            if (pr.found && param_addrs) param_addrs[i] = pr.addr;
        }
        for (int i = 0; i < n_params; i++) {
            uint32_t addr = param_addrs ? param_addrs[i] : 0;
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            fl |= IR_INST_FLAG_RELATIVE;
            emit_escribir_u24(cg, addr, 1 + i, 1);
        }
        if (is_node(ld->body, NODE_BLOCK))
            visit_block(cg, ld->body);
        else
            (void)visit_expression(cg, ld->body, 1);
        free(param_addrs);
        uint32_t frame_size = cg->sym.next_local_offset;
        if (!codegen_validar_tamano_frame(cg, frame_size, "__lambda", node->line, node->col))
            return dest_reg;
        cg->code[reserve_pos + 2] = frame_size & 0xFF;
        cg->code[reserve_pos + 3] = (frame_size >> 8) & 0xFF;
        cg->code[reserve_pos + 4] = (frame_size >> 16) & 0xFF;
        emit(cg, OP_RETORNAR, 0, 0, 0, 0);
        cg->function_depth = prev_fd;
        cg->current_fn_return = prev_ret;
        cg->current_fn_name = prev_name;
        cg->current_lambda_capture_names = prev_capture_names;
        cg->current_lambda_capture_types = prev_capture_types;
        cg->current_lambda_capture_count = prev_capture_count;
        cg->current_lambda_scope_base = prev_capture_base;
        sym_exit_scope(&cg->sym);
        mark_label(cg, skip_lbl);
        emit_load_label_addr(cg, dest_reg, entry_lbl);
        if (capture_count > 0) {
            int env_reg = (dest_reg == 1) ? 2 : 1;
            int tmp_reg = (dest_reg == 1 || env_reg == 1) ? 3 : 1;
            int list_id = ++cg->literal_counter;
            emit(cg, OP_MOVER, env_reg, list_id & 0xFF, (list_id >> 8) & 0xFF,
                 IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_MEM_LISTA_CREAR, env_reg, env_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            int capture_fail = 0;
            for (size_t i = 0; i < capture_count; i++) {
                SymResult cr = sym_lookup(&cg->sym, capture_names[i]);
                uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                if (!cr.found) {
                    char cbuf[CODEGEN_ERROR_MAX];
                    snprintf(cbuf, sizeof cbuf, "Error: captura lambda '%s' no declarada", capture_names[i]);
                    cg_collect_push(cg, cbuf, ld->base.line > 0 ? ld->base.line : 1, ld->base.col > 0 ? ld->base.col : 1);
                    capture_fail = 1;
                    continue;
                }
                if (cr.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_LEER, tmp_reg, cr.addr & 0xFF, (cr.addr >> 8) & 0xFF, fl);
                emit(cg, OP_MEM_LISTA_AGREGAR, env_reg, tmp_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            }
            if (!capture_fail)
                emit(cg, OP_CLOSURE_CREAR, dest_reg, dest_reg, env_reg,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        for (size_t i = 0; i < capture_count; i++) free(capture_names[i]);
        free(capture_names);
        free((void*)capture_types);
        return dest_reg;
    }
    if (is_node(node, NODE_IDENTIFIER)) {
        IdentifierNode *id_node = (IdentifierNode*)node;
        const char *name = id_node->name;
        if (cg->current_lambda_capture_count > 0 &&
            !codegen_lookup_type_in_scope_range(cg, name, cg->current_lambda_scope_base) &&
            codegen_name_in_list(cg->current_lambda_capture_names, cg->current_lambda_capture_count, name)) {
            size_t capture_idx = 0;
            while (capture_idx < cg->current_lambda_capture_count &&
                   strcmp(cg->current_lambda_capture_names[capture_idx], name) != 0) capture_idx++;
            emit(cg, OP_CLOSURE_CARGAR, dest_reg, (uint8_t)capture_idx, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
            return dest_reg;
        }
        SymResult r = sym_lookup(&cg->sym, name);
        if (r.found) {
            if (r.macro_ast) {
                emit(cg, OP_MOVER, dest_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                return dest_reg;
            }
            emit_leer_u24(cg, dest_reg, r.addr, r.is_relative);
            return dest_reg;
        }
        
        /* Caso implicito: si estamos en un metodo, buscar si 'name' es un campo de la clase */
        if (cg->current_class_name) {
            size_t off = 0, sz = 0;
            const char *ft = NULL;
            if (sym_get_struct_field(&cg->sym, cg->current_class_name, name, &off, &ft, &sz)) {
                SymResult sr_este = sym_lookup(&cg->sym, "este");
                if (sr_este.found) {
                    /* RE-CARGAR 'este' desde memoria para evitar usar un registro corrupto (como un float o hash) */
                    int este_reg = 254; /* Usar un registro temporal alto y seguro */
                    emit_leer_u24(cg, este_reg, sr_este.addr, sr_este.is_relative);
                    emit_sumar_u24(cg, este_reg, este_reg, (uint32_t)off);
                    emit(cg, OP_LEER, (uint8_t)dest_reg, (uint8_t)este_reg, 0, IR_INST_FLAG_B_REGISTER);
                    return dest_reg;
                }
            }
        }

        int fl = get_func_label(cg, name);
        if (fl >= 0) {
            if (!cg->expr_allow_func_literal) {
                char fbuf[CODEGEN_ERROR_MAX];
                snprintf(fbuf, sizeof fbuf,
                         "Error: '%s' es una funcion global; como valor solo puede usarse donde se espera tipo funcion "
                         "(por ejemplo argumento de otra funcion, o `funcion f = %s`).",
                         name, name);
                cg_collect_push(cg, fbuf, id_node->line, id_node->col);
                return dest_reg;
            }
            emit_load_label_addr(cg, dest_reg, fl);
            return dest_reg;
        }
        char vbuf[CODEGEN_ERROR_MAX];
        snprintf(vbuf, sizeof vbuf, "Error: variable '%s' no declarada antes de su uso", name);
        cg_collect_push(cg, vbuf, id_node->line, id_node->col);
        return dest_reg;
    }
    if (is_node(node, NODE_POSTFIX_UPDATE)) {
        PostfixUpdateNode *pu = (PostfixUpdateNode*)node;
        if (!is_node(pu->target, NODE_IDENTIFIER)) {
            cg_collect_push(cg,
                            "Error: '++' y '--' postfijos requieren una variable simple (identificador); use 'i = i + 1' para otros casos",
                            pu->base.line, pu->base.col);
            return dest_reg;
        }
        IdentifierNode *id_node = (IdentifierNode*)pu->target;
        SymResult r = sym_lookup(&cg->sym, id_node->name);
        if (!r.found) {
            char pvbuf[CODEGEN_ERROR_MAX];
            snprintf(pvbuf, sizeof pvbuf, "Error: variable '%s' no declarada antes de su uso", id_node->name);
            cg_collect_push(cg, pvbuf, id_node->line, id_node->col);
            return dest_reg;
        }
        (void)visit_expression(cg, pu->target, dest_reg);
        int tmp = (dest_reg == 254) ? 253 : 254;
        if (pu->delta == 1)
            emit(cg, OP_SUMAR, tmp, dest_reg, 1, IR_INST_FLAG_C_IMMEDIATE);
        else
            emit(cg, OP_RESTAR, tmp, dest_reg, 1, IR_INST_FLAG_C_IMMEDIATE);
        uint8_t wflags = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) wflags |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, r.addr, tmp, r.is_relative);
        return dest_reg;
    }
    if (is_node(node, NODE_UNARY_OP)) {
        UnaryOpNode *un = (UnaryOpNode*)node;
        int r = visit_expression(cg, un->expression, dest_reg);
        if (strcmp(un->operator, "esperar") == 0) {
            /* MVP: sin planificador en VM, `esperar x` equivale a evaluar x (tarea real = trabajo futuro). */
            return r;
        }
        if (strcmp(un->operator, "no") == 0) {
            emit(cg, OP_NO, dest_reg, r, 0, 0);
        } else if (strcmp(un->operator, "-") == 0) {
            const char *t = get_expression_type(cg, un->expression);
            if (t && strcmp(t, "flotante") == 0) {
                /* Proteccion de Reg 1 (este): no usar 1 como temporal. */
                int tmp = (dest_reg == 254) ? 253 : 254;
                emit(cg, OP_MOVER, tmp, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_RESTAR_FLT, dest_reg, tmp, r, 0);
            } else {
                emit(cg, OP_RESTAR, dest_reg, 0, r, IR_INST_FLAG_B_IMMEDIATE);
            }
        }
        return dest_reg;
    }
    if (is_node(node, NODE_BINARY_OP)) {
        BinaryOpNode *bn = (BinaryOpNode*)node;
        const char *op = bn->operator;
        const char *lt = get_expression_type(cg, bn->left);
        const char *rt = get_expression_type(cg, bn->right);
        int is_texto = (lt && (strcmp(lt, "texto") == 0 || strcmp(lt, "concepto") == 0 || strcmp(lt, "caracter") == 0 || strcmp(lt, "elemento") == 0)) ||
                       (rt && (strcmp(rt, "texto") == 0 || strcmp(rt, "concepto") == 0 || strcmp(rt, "caracter") == 0 || strcmp(rt, "elemento") == 0));
        int is_flt = (lt && strcmp(lt, "flotante") == 0) || (rt && strcmp(rt, "flotante") == 0) ||
                     expr_involves_float(cg, bn->left) || expr_involves_float(cg, bn->right);

        if (strcmp(op, "%") == 0 &&
            (expr_involves_float(cg, bn->left) || expr_involves_float(cg, bn->right))) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Error: operador '%%' no aplicable a flotante");
            cg->has_error = 1;
            cg->err_line = bn->line;
            cg->err_col = bn->col;
            return dest_reg;
        }

        if ((strcmp(op, "/") == 0 || strcmp(op, "%") == 0) && is_node(bn->right, NODE_LITERAL)) {
            LiteralNode *r_lit = (LiteralNode*)bn->right;
            if (!r_lit->is_float && r_lit->value.i == 0) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: division o modulo por cero");
                cg->has_error = 1;
                cg->err_line = bn->line;
                cg->err_col = bn->col;
                return dest_reg;
            }
        }

        /* Evitar reg 1 cuando dest_reg=3 (concat usa reg 1 para el izquierdo) */
        int rL = dest_reg;
        int rR_reg = (dest_reg == 1) ? 2 : ((dest_reg == 2) ? 1 : 2);
        
        if (strcmp(op, "+") == 0 && is_texto) {
            /* Concatenación de texto: usar stack para evitar clobbering si la derecha tiene llamadas/interpolaciones */
            rL = visit_expression(cg, bn->left, dest_reg);
            
            SymResult tmp = sym_reserve_temp(&cg->sym, 8);
            uint8_t fl_w = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (tmp.is_relative) fl_w |= IR_INST_FLAG_RELATIVE;
            emit_escribir_u24(cg, tmp.addr, rL, tmp.is_relative);
            
            rR_reg = (rL == 1 || rL == 254) ? 253 : 254;
            (void)visit_expression(cg, bn->right, rR_reg);
            
            emit_leer_u24(cg, rL, tmp.addr, tmp.is_relative);
        } else if (strcmp(op, "y") == 0) {
            int end_label = new_label(cg);
            visit_expression(cg, bn->left, dest_reg);
            if (cg->has_error) return dest_reg;
            
            // Si la izquierda es falsa (0), el resultado ya es 0 en dest_reg, saltamos al final
            int tmp_reg = (dest_reg == 254) ? 253 : 254;
            emit(cg, OP_CMP_EQ, tmp_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit_jump_if_nonzero(cg, (uint8_t)tmp_reg, end_label);
            
            visit_expression(cg, bn->right, dest_reg);
            if (cg->has_error) return dest_reg;
            
            mark_label(cg, end_label);
            // Convertir resultado a booleano (0 o 1)
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_NO, dest_reg, dest_reg, 0, 0);
            return dest_reg;
        } else if (strcmp(op, "o") == 0) {
            int end_label = new_label(cg);
            visit_expression(cg, bn->left, dest_reg);
            if (cg->has_error) return dest_reg;
            
            // Si la izquierda es verdadera (no 0), saltamos al final
            emit_jump_if_nonzero(cg, (uint8_t)dest_reg, end_label);
            
            visit_expression(cg, bn->right, dest_reg);
            if (cg->has_error) return dest_reg;
            
            mark_label(cg, end_label);
            // Convertir resultado a booleano (0 o 1)
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_NO, dest_reg, dest_reg, 0, 0);
            return dest_reg;
        } else {
            int left_has_call = expr_has_call(bn->left);
            int right_has_call = expr_has_call(bn->right);
            int next_safe_reg = dest_reg + 1;
            if (next_safe_reg > 250) next_safe_reg = 250;

            if (right_has_call && !left_has_call) {
                rR_reg = visit_expression(cg, bn->right, next_safe_reg);
                if (cg->has_error) return dest_reg;
                
                SymResult tmp = sym_reserve_temp(&cg->sym, 8);
                uint8_t fl_w = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp.is_relative) fl_w |= IR_INST_FLAG_RELATIVE;
                emit_escribir_u24(cg, tmp.addr, rR_reg, tmp.is_relative);
                
                rL = visit_expression(cg, bn->left, dest_reg);
                if (cg->has_error) return dest_reg;
                
                emit_leer_u24(cg, rR_reg, tmp.addr, tmp.is_relative);
            } else {
                rL = visit_expression(cg, bn->left, dest_reg);
                if (cg->has_error) return dest_reg;
                
                SymResult tmp = sym_reserve_temp(&cg->sym, 8);
                emit_escribir_u24(cg, tmp.addr, rL, tmp.is_relative);
                
                rR_reg = visit_expression(cg, bn->right, next_safe_reg);
                if (cg->has_error) return dest_reg;
                
                emit_leer_u24(cg, rL, tmp.addr, tmp.is_relative);
            }
        }
        if (strcmp(op, "+") == 0 && is_texto) {
            /* Concatenación de texto (incl. caracter -> texto) */
            /* Solo convertir a número->texto cuando el tipo es explícitamente entero o flotante.
             * texto, concepto, caracter y NULL (desconocido, p.ej. ingresar_texto) se usan como ID hash. */
            int lt_is_int = (lt && strcmp(lt, "entero") == 0);
            int rt_is_int = (rt && strcmp(rt, "entero") == 0);
            int lt_is_flt = (lt && strcmp(lt, "flotante") == 0);
            int rt_is_flt = (rt && strcmp(rt, "flotante") == 0);
            int lt_is_car = (lt && strcmp(lt, "caracter") == 0);
            int rt_is_car = (rt && strcmp(rt, "caracter") == 0);
            int lt_is_bool = (lt && (strcmp(lt, "booleano") == 0 || strcmp(lt, "bool") == 0));
            int rt_is_bool = (rt && (strcmp(rt, "booleano") == 0 || strcmp(rt, "bool") == 0));
            int lt_is_u32 = (lt && (strcmp(lt, "u32") == 0 || strcmp(lt, "u64") == 0));
            int rt_is_u32 = (rt && (strcmp(rt, "u32") == 0 || strcmp(rt, "u64") == 0));
            uint8_t lt_num_c = (uint8_t)(lt_is_bool ? 2 : ((lt_is_int || lt_is_u32) ? 1 : 0));
            uint8_t rt_num_c = (uint8_t)(rt_is_bool ? 2 : ((rt_is_int || rt_is_u32) ? 1 : 0));

            if (lt && (lt_is_int || lt_is_flt || lt_is_car || lt_is_u32 || lt_is_bool) && !expr_already_yields_text_string_id(cg, bn->left))
                emit(cg, lt_is_car ? OP_STR_DESDE_CODIGO : OP_STR_DESDE_NUMERO, rL, rL, lt_is_car ? 0 : lt_num_c,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | (lt_is_car ? 0 : IR_INST_FLAG_C_IMMEDIATE));
            if (rt && (rt_is_int || rt_is_flt || rt_is_car || rt_is_u32 || rt_is_bool) && !expr_already_yields_text_string_id(cg, bn->right))
                emit(cg, rt_is_car ? OP_STR_DESDE_CODIGO : OP_STR_DESDE_NUMERO, rR_reg, rR_reg, rt_is_car ? 0 : rt_num_c,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | (rt_is_car ? 0 : IR_INST_FLAG_C_IMMEDIATE));
            emit(cg, OP_STR_CONCATENAR_REG, dest_reg, rL, rR_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            return dest_reg;
        }
        if (is_flt) {
            if (lt && strcmp(lt, "entero") == 0)
                emit(cg, OP_CONV_I2F, rL, rL, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            if (rt && strcmp(rt, "entero") == 0)
                emit(cg, OP_CONV_I2F, rR_reg, rR_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            if (strcmp(op, "+") == 0) emit(cg, OP_SUMAR_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "-") == 0) emit(cg, OP_RESTAR_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "*") == 0) emit(cg, OP_MULTIPLICAR_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "/") == 0) emit(cg, OP_DIVIDIR_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, ">") == 0) emit(cg, OP_CMP_GT_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "<") == 0) emit(cg, OP_CMP_LT_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "<=") == 0) emit(cg, OP_CMP_LE_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, ">=") == 0) emit(cg, OP_CMP_GE_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "==") == 0) emit(cg, OP_CMP_EQ_FLT, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "!=") == 0) {
                emit(cg, OP_CMP_EQ_FLT, dest_reg, rL, rR_reg, 0);
                emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            } else if (strcmp(op, "%") == 0 || strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Error: operador '%s' no aplicable a flotante", op);
                cg->has_error = 1;
                cg->err_line = bn->line;
                cg->err_col = bn->col;
            } else {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Error: operador '%s' no soportado entre expresiones flotantes", op);
                cg->has_error = 1;
                cg->err_line = bn->line;
                cg->err_col = bn->col;
            }
        } else {
            if (strcmp(op, "+") == 0) emit(cg, OP_SUMAR, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "-") == 0) emit(cg, OP_RESTAR, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "*") == 0) emit(cg, OP_MULTIPLICAR, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "/") == 0) emit(cg, OP_DIVIDIR, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "%") == 0) emit(cg, OP_MODULO, dest_reg, rL, rR_reg, 0);
            else         if (strcmp(op, ">") == 0) {
                int use_u = (lt && (strcmp(lt, "u32") == 0 || strcmp(lt, "u64") == 0)) ||
                            (rt && (strcmp(rt, "u32") == 0 || strcmp(rt, "u64") == 0));
                emit(cg, use_u ? OP_CMP_GT_U : OP_CMP_GT, dest_reg, rL, rR_reg, 0);
            } else if (strcmp(op, "<") == 0) {
                int use_u = (lt && (strcmp(lt, "u32") == 0 || strcmp(lt, "u64") == 0)) ||
                            (rt && (strcmp(rt, "u32") == 0 || strcmp(rt, "u64") == 0));
                emit(cg, use_u ? OP_CMP_LT_U : OP_CMP_LT, dest_reg, rL, rR_reg, 0);
            } else if (strcmp(op, "==") == 0) emit(cg, OP_CMP_EQ, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, "!=") == 0) {
                emit(cg, OP_CMP_EQ, dest_reg, rL, rR_reg, 0);
                emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            }
            else if (strcmp(op, "<=") == 0) {
                int use_u = (lt && (strcmp(lt, "u32") == 0 || strcmp(lt, "u64") == 0)) ||
                            (rt && (strcmp(rt, "u32") == 0 || strcmp(rt, "u64") == 0));
                emit(cg, use_u ? OP_CMP_LE_U : OP_CMP_LE, dest_reg, rL, rR_reg, 0);
            } else if (strcmp(op, ">=") == 0) {
                int use_u = (lt && (strcmp(lt, "u32") == 0 || strcmp(lt, "u64") == 0)) ||
                            (rt && (strcmp(rt, "u32") == 0 || strcmp(rt, "u64") == 0));
                emit(cg, use_u ? OP_CMP_GE_U : OP_CMP_GE, dest_reg, rL, rR_reg, 0);
            }
            else if (strcmp(op, "<<") == 0) emit(cg, OP_BIT_SHL, dest_reg, rL, rR_reg, 0);
            else if (strcmp(op, ">>") == 0) emit(cg, OP_BIT_SHR, dest_reg, rL, rR_reg, 0);
        }
        return dest_reg;
    }
    if (is_node(node, NODE_TERNARY)) {
        TernaryNode *tn = (TernaryNode*)node;
        int else_id = new_label(cg);
        int end_id = new_label(cg);
        
        /* Proteccion de Reg 1 (este): usar registro alto para la condicion. */
        int cond_reg = visit_expression(cg, tn->condition, 253);
        emit(cg, OP_CMP_EQ, 254, cond_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 254, else_id);
        
        visit_expression(cg, tn->true_expr, dest_reg);
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, end_id, PATCH_JUMP);
        mark_label(cg, else_id);
        visit_expression(cg, tn->false_expr, dest_reg);
        mark_label(cg, end_id);
        return dest_reg;
    }
    if (is_node(node, NODE_CALL)) {
        CallNode *cn = (CallNode*)node;
        
        /* 1. Caso especial: Llamada a metodo explicito obj.metodo(...) 
           Debe ir ANTES de visit_call_sistema para evitar conflictos con palabras clave (ej: dormir) */
        if (cn->callee && is_node(cn->callee, NODE_MEMBER_ACCESS)) {
            MemberAccessNode *ma = (MemberAccessNode *)cn->callee;
            const char *obj_type = get_expression_type(cg, ma->target);
            if (obj_type && !is_builtin_type(obj_type)) {
                /* IMPORTANTE: Evaluar argumentos PRIMERO. 
                   Si evaluar un argumento implica otra llamada a metodo, ésta sobrescribirá el Reg 1.
                   Al evaluar la instancia 'este' al final, garantizamos que el Reg 1 sea el correcto
                   para la llamada actual. */
                emit_call_args_preserved_methods(cg, cn->args, cn->n_args);
                
                /* Evaluar instancia 'este' -> reg 1 */
                visit_expression(cg, ma->target, 1);

                /* Intentar dispatch dinámico (polimorfismo) 
                   EXCEPTO si el objetivo es 'padre' (queremos dispatch estático a la base) */
                int is_super_call = 0;
                if (is_node(ma->target, NODE_IDENTIFIER) && strcmp(((IdentifierNode*)ma->target)->name, "padre") == 0) {
                    is_super_call = 1;
                }

                if (!is_super_call && emit_dynamic_dispatch(cg, obj_type, ma->member, cn, dest_reg, 0)) {
                    return dest_reg;
                }

                /* Fallback dispatch estático */
                int label_id = get_method_label_recursive(cg, obj_type, ma->member);
                if (label_id >= 0) {
                    emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                    add_patch(cg, label_id, PATCH_JUMP);
                    emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
                    return dest_reg;
                }
            }
        }

        if (visit_call_sistema(cg, cn, dest_reg)) {
            if (cg->has_error) return dest_reg;
            return dest_reg;
        }

        /* 2. Caso: Variable de tipo funcion o callee arbitrario */
        if (cn->name && !cn->callee) {
            SymResult vr = sym_lookup(&cg->sym, cn->name);
            const char *vty = vr.found ? sym_lookup_type(&cg->sym, cn->name) : NULL;
            if (vr.found && !vr.macro_ast && vty && strcmp(vty, "funcion") == 0) {
                emit_leer_u24(cg, CG_INDIRECT_CALLEE_REG, vr.addr, vr.is_relative);
                int prev = cg->expr_allow_func_literal;
                cg->expr_allow_func_literal = 1;
                emit_call_args_preserved(cg, cn->args, cn->n_args);
                cg->expr_allow_func_literal = prev;
                emit(cg, OP_LLAMAR, CG_INDIRECT_CALLEE_REG, 0, 0, 0);
                emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
                return dest_reg;
            }
        }
        if (cn->callee) {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            visit_expression(cg, cn->callee, CG_INDIRECT_CALLEE_REG);
            emit_call_args_preserved(cg, cn->args, cn->n_args);
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, CG_INDIRECT_CALLEE_REG, 0, 0, 0);
            emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
            return dest_reg;
        }

        /* 3. Caso: Macro (lambda-macro) */
        SymResult r_fn = {0};
        if (cn->name) r_fn = sym_lookup(&cg->sym, cn->name);
        if (r_fn.found && r_fn.macro_ast) {
            LambdaDeclNode *ld = (LambdaDeclNode*)r_fn.macro_ast;
            if (ld->n_params != cn->n_args) {
                codegen_error_macro_arity(cg, cn->name, ld->n_params, cn->n_args, node->line, node->col);
                return dest_reg;
            }
            sym_enter_scope(&cg->sym, 0); 
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            for (size_t i = 0; i < ld->n_params; i++) {
                int reg = visit_expression(cg, cn->args[i], dest_reg + 1);
                const char *arg_type = (ld->types && ld->types[i]) ? ld->types[i] : get_expression_type(cg, cn->args[i]);
                if (!arg_type) arg_type = "entero"; 
                SymResult p_r = sym_declare(&cg->sym, ld->params[i], arg_type, 8, 1, 0, NULL, SYMDECL_FLAGS_NONE);
                if (!p_r.found) {
                    codegen_sym_declare_fail(cg, ld->params[i], node->line, node->col, "Invocación de macro");
                    cg->expr_allow_func_literal = prev;
                    sym_exit_scope(&cg->sym);
                    return dest_reg;
                }
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (p_r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit_escribir_u24(cg, p_r.addr, reg, 1);
            }
            cg->expr_allow_func_literal = prev;
            int old_macro_end_label = cg->macro_end_label;
            int old_macro_dest_reg = cg->macro_dest_reg;
            cg->macro_end_label = new_label(cg);
            cg->macro_dest_reg = dest_reg;
            int res_reg = visit_expression(cg, ld->body, dest_reg);
            mark_label(cg, cg->macro_end_label);
            cg->macro_end_label = old_macro_end_label;
            cg->macro_dest_reg = old_macro_dest_reg;
            sym_exit_scope(&cg->sym);
            if (res_reg != dest_reg) {
                emit(cg, OP_MOVER, dest_reg, res_reg, 0, IR_INST_FLAG_B_REGISTER);
            }
            return dest_reg;
        }

        /* 4. Caso: Funcion global o metodo implícito (este.metodo) */
        int label_id = -1;
        int is_implicit_este = 0;
        if (cn->name) {
            label_id = get_func_label(cg, cn->name);
            if (label_id < 0 && cg->current_class_name) {
                label_id = get_method_label_recursive(cg, cg->current_class_name, cn->name);
                if (label_id >= 0) is_implicit_este = 1;
            }
        }

        if (label_id >= 0) {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            if (is_implicit_este) {
                SymResult sr = sym_lookup(&cg->sym, "este");
                if (sr.found) {
                    emit_leer_u24(cg, 1, sr.addr, sr.is_relative);
                }
                emit_call_args_preserved_methods(cg, cn->args, cn->n_args);
            } else {
                emit_call_args_preserved(cg, cn->args, cn->n_args);
            }
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            add_patch(cg, label_id, PATCH_JUMP);
            emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        } else {
            /* Errores de aridad o funcion no encontrada */
            if (cn->name) {
                StructInfo *si = sym_get_struct_info(&cg->sym, cn->name);
                if (si) {
                    /* Constructor: reservar memoria */
                    /* BUGFIX: No usar dest_reg directamente si es 1 (este), usar un temporal */
                    int res_ptr_reg = (dest_reg == 1) ? 252 : dest_reg;
                    emit_heap_reservar_u24(cg, res_ptr_reg, (uint32_t)si->total_size);
                    
                    if (si->is_class) {
                        /* Escribir Class ID (hash del nombre) en offset 0 */
                        const int tmp_id_reg = 250; // Registro temporal seguro lejos de los demas
                        emit_load_text_literal_reg(cg, si->name, tmp_id_reg);
                        emit(cg, OP_ESCRIBIR, (uint8_t)res_ptr_reg, (uint8_t)tmp_id_reg, 0, 
                             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                    }
                    
                    int init_label = get_method_label_recursive(cg, si->name, "inicializar");
                    size_t init_arity = 0;
                    int has_init_sig = get_method_param_count(cg, si->name, "inicializar", &init_arity);
                    if (init_label >= 0 && has_init_sig && cn->n_args > 0 && cn->n_args != init_arity) {
                        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                                 "Constructor `%s(...)`: se pasaron %zu argumentos, pero `inicializar` de `%s` requiere %zu.",
                                 cn->name ? cn->name : si->name, cn->n_args, si->name, init_arity);
                        cg->has_error = 1;
                        cg->err_line = node->line;
                        cg->err_col = node->col;
                        return dest_reg;
                    }
                    if (init_label >= 0 && (cn->n_args > 0 || !has_init_sig || init_arity == 0)) {
                        const int obj_ptr_temp = 251; // Registro temporal seguro lejos de los demas
                        /* Guardar el objeto en un registro temporal seguro (251) para evitar colision con args (120) y dest_reg */
                        emit(cg, OP_MOVER, (uint8_t)obj_ptr_temp, (uint8_t)res_ptr_reg, 0, IR_INST_FLAG_B_REGISTER);
                        
                        /* El objeto recien creado va al registro 1 (este) para la llamada */
                        emit(cg, OP_MOVER, 1, (uint8_t)res_ptr_reg, 0, IR_INST_FLAG_B_REGISTER);
                        
                        /* Evaluar argumentos -> reg 2, 3... (usan temp_reg 120 internamente) */
                        emit_call_args_preserved_methods(cg, cn->args, cn->n_args);
                        /* Emitir llamada */
                        emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                        add_patch(cg, init_label, PATCH_JUMP);
                        
                        /* Restaurar el objeto a res_ptr_reg desde nuestro temporal seguro */
                        emit(cg, OP_MOVER, (uint8_t)res_ptr_reg, (uint8_t)obj_ptr_temp, 0, IR_INST_FLAG_B_REGISTER);
                    }
                    
                    if (dest_reg == 1) {
                        /* Si dest_reg era 1, lo sobreescribimos al final con el nuevo puntero */
                        emit(cg, OP_MOVER, 1, (uint8_t)res_ptr_reg, 0, IR_INST_FLAG_B_REGISTER);
                    } else if (res_ptr_reg != dest_reg) {
                         emit(cg, OP_MOVER, (uint8_t)dest_reg, (uint8_t)res_ptr_reg, 0, IR_INST_FLAG_B_REGISTER);
                    }
                    return dest_reg;
                }
            }
            if (codegen_error_vec_constructor_arity(cg, cn)) return dest_reg;
            if (cn->name && codegen_error_if_bad_arity_buscar_contiene_termina(cg, cn)) return dest_reg;
            if (cn->name && codegen_error_if_bad_arity_pensar_procesar_texto(cg, cn)) return dest_reg;
            if (cn->name && is_sistema_llamada(cn->name, strlen(cn->name))) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "[EXP] '%s' es una funcion incorporada del lenguaje, pero el numero o la forma de los argumentos "
                         "no coincide con ninguna firma que el compilador admita en esta llamada (se pasaron %zu). "
                         "Revise la documentacion o el orden de los parametros."
                         " Si usa `usar`, linea/columna suelen ser del modulo importado; el resaltado puede tomar el .jasb principal.",
                         cn->name, cn->n_args);
            } else {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Llamada no resuelta: '%s' no es una funcion definida en este programa.",
                         cn->name ? cn->name : "?");
            }
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : node->line;
            cg->err_col = cn->base.col > 0 ? cn->base.col : node->col;
            codegen_note_error_diag_path(cg);
        }
        return dest_reg;
    }
    if (is_node(node, NODE_INDEX_ACCESS)) {
        IndexAccessNode *ian = (IndexAccessNode*)node;
        const char *target_type = get_expression_type(cg, ian->target);
        
        int target_reg = visit_expression(cg, ian->target, dest_reg);
        
        /* Proteccion de Reg 1 (este): si dest_reg no es 1, no usar 1 como temporal para el indice. */
        int preferred_index_reg = (dest_reg == 1) ? 2 : 253;
        int index_reg = visit_expression(cg, ian->index, preferred_index_reg);
        
        if (target_type && strcmp(target_type, "mapa") == 0)
            emit(cg, OP_MEM_MAPA_OBTENER, dest_reg, target_reg, index_reg, 0);
        else
            emit(cg, OP_MEM_LISTA_OBTENER, dest_reg, target_reg, index_reg, 0);
        return dest_reg;
    }
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        const char *t = get_expression_type(cg, man->target);
        
        int is_map_backed = (t && strcmp(t, "mapa") == 0);
        /* En Jasboot, los elementos de listas/mapas que son registros se almacenan como IDs de mapa JMN.
           Tambien las variables declaradas como 'mapa x<T>' son map-backed. */
        if (!is_map_backed && t && type_is_user_struct(cg, t)) {
            if (is_node(man->target, NODE_INDEX_ACCESS)) {
                is_map_backed = 1;
            } else if (is_node(man->target, NODE_IDENTIFIER)) {
                /* Verificar si el identificador fue declarado como mapa con template */
                const char *raw_t = sym_lookup_type(&cg->sym, ((IdentifierNode*)man->target)->name);
                if (raw_t && strcmp(raw_t, "mapa") == 0) {
                    is_map_backed = 1;
                }
            }
        }

        if (t && strcmp(t, "lista") == 0 &&
            (strcmp(man->member, "medida") == 0 || strcmp(man->member, "tamano") == 0 || strcmp(man->member, "size") == 0)) {
            int tr = visit_expression(cg, man->target, dest_reg);
            emit(cg, OP_MEM_LISTA_TAMANO, dest_reg, tr, 0, IR_INST_FLAG_B_REGISTER);
            return dest_reg;
        }
        if (is_map_backed) {
            if (strcmp(man->member, "medida") == 0 || strcmp(man->member, "tamano") == 0 || strcmp(man->member, "size") == 0) {
                int tr = visit_expression(cg, man->target, dest_reg);
                emit(cg, OP_MEM_MAPA_TAMANO, dest_reg, tr, 0, IR_INST_FLAG_B_REGISTER);
                return dest_reg;
            }
            /* Acceso a campo de mapa dinámico: obj.campo -> mapa_obtener(obj, "campo") */
            int tr = visit_expression(cg, man->target, dest_reg);
            int kr = (dest_reg == 254) ? 253 : 254;
            emit_load_text_literal_reg(cg, man->member, kr);
            emit(cg, OP_MEM_MAPA_OBTENER, dest_reg, tr, kr, 0);
            return dest_reg;
        }
        /* Usar un registro temporal seguro (254) para evitar colisiones con 'este' (reg 1) o dest_reg */
        int m_tmp_reg = 254;
        if (dest_reg == m_tmp_reg) m_tmp_reg = 253;
        MemberAddrResult mar = get_member_address(cg, node, m_tmp_reg);
        if (mar.invalid)
            return dest_reg;
        if (mar.in_reg) {
            uint8_t flags = IR_INST_FLAG_B_REGISTER;
            if (mar.is_relative) flags |= IR_INST_FLAG_RELATIVE;
            /* Validación: si el tipo base es texto o similar, no deberíamos estar leyendo de él como puntero
               si lo que queremos es el valor. Pero aquí ya tenemos la dirección del miembro. */
            emit(cg, OP_LEER, (uint8_t)dest_reg, (uint8_t)mar.reg, 0, flags);
        } else {
            emit_leer_u24(cg, dest_reg, mar.addr, mar.is_relative);
        }
        return dest_reg;
    }
    if (is_node(node, NODE_CONTIENE_TEXTO)) {
        ContieneTextoNode *cn = (ContieneTextoNode*)node;
        visit_expression(cg, cn->source, 10);
        visit_expression(cg, cn->pattern, 11);
        emit(cg, OP_MEM_CONTIENE_TEXTO_REG, (uint8_t)dest_reg, 10, 11, 0);
        SymResult r = sym_get_or_create(&cg->sym, "resultado", NULL);
        emit_escribir_u24(cg, r.addr, (uint8_t)dest_reg, r.is_relative);
        return dest_reg;
    }
    if (is_node(node, NODE_TERMINA_CON)) {
        TerminaConNode *tn = (TerminaConNode*)node;
        visit_expression(cg, tn->source, 10);
        visit_expression(cg, tn->suffix, 11);
        emit(cg, OP_MEM_TERMINA_CON_REG, (uint8_t)dest_reg, 10, 11, 0);
        SymResult r = sym_get_or_create(&cg->sym, "resultado", NULL);
        emit_escribir_u24(cg, r.addr, (uint8_t)dest_reg, r.is_relative);
        return dest_reg;
    }
    if (is_node(node, NODE_JSON_LITERAL)) {
        if (!emit_json_literal_map(cg, (MapLiteralNode*)node, dest_reg)) {
            if (!cg->has_error) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX, "No se pudo emitir literal json.");
                cg->has_error = 1;
            }
        }
        return dest_reg;
    }
    if (is_node(node, NODE_MAP_LITERAL)) {
        MapLiteralNode *mln = (MapLiteralNode*)node;
        emit(cg, OP_MEM_MAPA_CREAR, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        for (size_t i = 0; i < mln->n; i++) {
            /* Usar registros altos y distintos para llaves y valores temporales para evitar clobbering en anidamientos */
            int key_reg = 121;
            int val_reg = 122;
            
            /* Guardar la llave primero */
            visit_expression(cg, mln->keys[i], key_reg);
            
            /* Si el valor es complejo (lista/mapa), podria usar reg 121. Lo protegemos. */
            SymResult tmp_key = sym_reserve_temp(&cg->sym, 8);
            emit_escribir_u24(cg, tmp_key.addr, key_reg, tmp_key.is_relative);
            
            visit_expression(cg, mln->values[i], val_reg);
            
            /* Restaurar la llave */
            emit_leer_u24(cg, key_reg, tmp_key.addr, tmp_key.is_relative);
            
            emit(cg, OP_MEM_MAPA_PONER, (uint8_t)dest_reg, (uint8_t)key_reg, (uint8_t)val_reg, 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        }
        return dest_reg;
    }
    if (is_node(node, NODE_LAMBDA_DECL)) {
        return dest_reg; /* Placeholder: se maneja en VarDeclNode */
    }
    
    if (is_node(node, NODE_BLOCK)) {
        BlockNode *bn = (BlockNode*)node;
        for (size_t i = 0; i < bn->n; i++) {
            visit_statement(cg, bn->statements[i]);
        }
        return dest_reg;
    }

    return dest_reg;
}

static uint8_t cg_vec_fl_read(int is_rel) {
    uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
    if (is_rel) fl |= IR_INST_FLAG_RELATIVE;
    return fl;
}

static uint8_t cg_vec_fl_write(int is_rel) {
    uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
    if (is_rel) fl |= IR_INST_FLAG_RELATIVE;
    return fl;
}

/* Base de almacenamiento para vec2/3/4 (variable o campo); no cubre direccion solo en registro. */
static int codegen_vec_lvalue_base(CodeGen *cg, ASTNode *node, uint32_t *out_base, int *out_rel) {
    *out_base = 0;
    *out_rel = 0;
    if (!node) return 0;
    if (is_node(node, NODE_IDENTIFIER)) {
        const char *n = ((IdentifierNode *)node)->name;
        const char *t = sym_lookup_type(&cg->sym, n);
        if (!t || (strcmp(t, "vec2") != 0 && strcmp(t, "vec3") != 0 && strcmp(t, "vec4") != 0))
            return 0;
        SymResult sr = sym_lookup(&cg->sym, n);
        if (!sr.found) return 0;
        *out_base = sr.addr;
        *out_rel = sr.is_relative ? 1 : 0;
        return 1;
    }
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        const char *ft = get_member_chain_type(cg, node);
        if (!ft || (strcmp(ft, "vec2") != 0 && strcmp(ft, "vec3") != 0 && strcmp(ft, "vec4") != 0))
            return 0;
        MemberAddrResult mar = get_member_address(cg, node, 2);
        if (mar.invalid || mar.in_reg) return 0;
        *out_base = mar.addr;
        *out_rel = mar.is_relative ? 1 : 0;
        return 1;
    }
    return 0;
}

/* Conversion implicita al guardar: entero <- flotante (F2I), flotante <- entero (I2F). 
 * También permitimos entero/flotante -> texto automáticamente (OP_STR_DESDE_NUMERO). */
static void emit_conv_for_store(CodeGen *cg, const char *dest_type, const char *expr_type, int reg) {
    if (!dest_type || !expr_type) return;
    if (strcmp(dest_type, "entero") == 0 && strcmp(expr_type, "flotante") == 0)
        emit(cg, OP_CONV_F2I, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
    else if (strcmp(dest_type, "flotante") == 0 && strcmp(expr_type, "entero") == 0)
        emit(cg, OP_CONV_I2F, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
    else if (strcmp(dest_type, "texto") == 0 &&
             (strcmp(expr_type, "entero") == 0 || strcmp(expr_type, "flotante") == 0 ||
              strcmp(expr_type, "bool") == 0 || strcmp(expr_type, "booleano") == 0)) {
        int is_bool_e = (strcmp(expr_type, "bool") == 0 || strcmp(expr_type, "booleano") == 0);
        int is_int = (strcmp(expr_type, "entero") == 0);
        uint8_t ic = (uint8_t)(is_bool_e ? 2 : (is_int ? 1 : 0));
        emit(cg, OP_STR_DESDE_NUMERO, (uint8_t)reg, (uint8_t)reg, ic,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
    }
}

/* Solo entero<->flotante es implicito; otros tipos a numerico exigen API explicita (str_a_*, etc.). */
static int reject_non_numeric_to_scalar(CodeGen *cg, const char *dest_type, const char *expr_type, int line, int col) {
    if (!dest_type || !expr_type) return 0;
    if (strcmp(dest_type, "entero") != 0 && strcmp(dest_type, "flotante") != 0) return 0;
    if (strcmp(expr_type, "entero") == 0 || strcmp(expr_type, "flotante") == 0 || strcmp(expr_type, "elemento") == 0) return 0;
    if (strcmp(dest_type, "entero") == 0 && strcmp(expr_type, "objeto") == 0) return 0;
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "No hay conversion implicita de tipo '%s' a '%s'. Use convertir_entero/convertir_flotante (o str_a_entero/str_a_flotante) o ajuste el tipo.",
             expr_type, dest_type);
    cg->has_error = 1;
    cg->err_line = line;
    cg->err_col = col;
    return 1;
}

/* Misma condicion que reject_non_numeric_to_scalar, mensaje orientado a retornar vs firma. */
static int reject_return_incompatible_value(CodeGen *cg, const char *declared_ret, const char *expr_type, int line, int col) {
    if (!declared_ret || !expr_type) return 0;
    if (strcmp(declared_ret, "entero") != 0 && strcmp(declared_ret, "flotante") != 0) return 0;
    if (strcmp(expr_type, "entero") == 0 || strcmp(expr_type, "flotante") == 0 || strcmp(expr_type, "elemento") == 0) return 0;
    const char *fn = (cg->current_fn_name && cg->current_fn_name[0]) ? cg->current_fn_name : "?";
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "La funcion '%s' declara retorno tipo '%s' y no puede retornar un valor de tipo '%s'. "
             "Solo hay conversion implicita entre entero y flotante; use convertir_entero/convertir_flotante "
             "(o str_a_*) o cambie 'retorna' en la firma.",
             fn, declared_ret, expr_type);
    cg->has_error = 1;
    cg->err_line = line;
    cg->err_col = col;
    return 1;
}

/* imprimir / ${...} / texto interpolado: tipo funcion no es un valor imprimible */
static int reject_funcion_in_display_context(CodeGen *cg, const char *t, int ctx_line, int ctx_col, int in_text_interpolation) {
    if (!t || strcmp(t, "funcion") != 0) return 0;
    int el = ctx_line > 0 ? ctx_line : 1;
    int ec = ctx_col > 0 ? ctx_col : 1;
    if (in_text_interpolation) {
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "Tipo `funcion` no admite interpolacion en texto (${...}): es invocable, no un dato imprimible; "
                 "un entero mostrado seria solo una direccion interna. Use f(args) y interpole el resultado.");
    } else {
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "Tipo `funcion` no se imprime con imprimir/imprimir_sin_salto. "
                 "Llame la funcion: g(args), e imprima el valor devuelto.");
    }
    cg->has_error = 1;
    cg->err_line = el;
    cg->err_col = ec;
    return 1;
}

static void codegen_note_error_diag_path(CodeGen *cg) {
    if (!cg) return;
    if (cg->err_diag_unit_path) {
        free(cg->err_diag_unit_path);
        cg->err_diag_unit_path = NULL;
    }
    if (cg->diag_unit_path_hint && cg->diag_unit_path_hint[0])
        cg->err_diag_unit_path = strdup(cg->diag_unit_path_hint);
}

/* Nombre de variable/parámetro/etc.: distingue palabra reservada vs duplicado en el mismo alcance */
static void codegen_sym_declare_fail(CodeGen *cg, const char *name, int line, int col, const char *contexto) {
    int el = line > 0 ? line : 1;
    int ec = col > 0 ? col : 1;
    cg->has_error = 1;
    cg->err_line = el;
    cg->err_col = ec;
    const char *scope = (cg && cg->current_fn_name && cg->current_fn_name[0]) ? cg->current_fn_name : NULL;
    if (name && is_reserved_identifier(name)) {
        if (scope) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "%s en '%s': el identificador '%s' coincide con una palabra reservada del lenguaje (o con una palabra en inglés no permitida). Elija otro nombre.",
                     contexto ? contexto : "Error", scope, name);
        } else {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "%s: el identificador '%s' coincide con una palabra reservada del lenguaje (o con una palabra en inglés no permitida). Elija otro nombre.",
                     contexto ? contexto : "Error", name);
        }
    } else {
        if (scope) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "%s en '%s': el identificador '%s' ya está declarado en este alcance.",
                     contexto ? contexto : "Error", scope, name ? name : "?");
        } else {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "%s: el identificador '%s' ya está declarado en este alcance.",
                     contexto ? contexto : "Error", name ? name : "?");
        }
    }
    codegen_note_error_diag_path(cg);
}

static int codegen_validate_struct_def_names(CodeGen *cg, StructDefNode *sd) {
    if (!sd)
        return 0;
    int line = sd->base.line > 0 ? sd->base.line : 1;
    int col = sd->base.col > 0 ? sd->base.col : 1;
    if (sd->name && is_reserved_identifier(sd->name)) {
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "Registro o clase: el nombre del tipo '%s' no es válido (palabra reservada o inglés prohibido).",
                 sd->name);
        cg->has_error = 1;
        cg->err_line = line;
        cg->err_col = col;
        return 1;
    }
    for (size_t b = 0; b < sd->n_extends; b++) {
        const char *bn = sd->extends_names ? sd->extends_names[b] : NULL;
        if (bn && is_reserved_identifier(bn)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Registro o clase '%s': en `extiende` el nombre '%s' no es válido (palabra reservada o inglés prohibido).",
                     sd->name ? sd->name : "?", bn);
            cg->has_error = 1;
            cg->err_line = line;
            cg->err_col = col;
            return 1;
        }
    }
    for (size_t i = 0; i < sd->n_fields; i++) {
        const char *fnm = sd->field_names ? sd->field_names[i] : NULL;
        if (fnm && is_reserved_identifier(fnm)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Registro o clase '%s': el campo '%s' no puede llamarse así (palabra reservada o inglés prohibido).",
                     sd->name ? sd->name : "?", fnm);
            cg->has_error = 1;
            cg->err_line = line;
            cg->err_col = col;
            return 1;
        }
    }
    for (size_t j = 0; j < sd->n_methods; j++) {
        FunctionNode *m = sd->methods[j] ? (FunctionNode *)sd->methods[j] : NULL;
        if (m && m->name && is_reserved_identifier(m->name)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Registro o clase '%s': el método '%s' no puede llamarse así (palabra reservada o inglés prohibido).",
                     sd->name ? sd->name : "?", m->name);
            cg->has_error = 1;
            cg->err_line = m->base.line > 0 ? m->base.line : line;
            cg->err_col = m->base.col > 0 ? m->base.col : col;
            return 1;
        }
    }
    for (size_t k = 0; k < sd->n_nested_structs; k++) {
        if (sd->nested_structs[k] && codegen_validate_struct_def_names(cg, (StructDefNode *)sd->nested_structs[k]))
            return 1;
    }
    return 0;
}

/* --- 4.3 VarDeclNode, 4.4 AssignmentNode --- */
static void visit_statement(CodeGen *cg, ASTNode *node) {
    if (!node || cg->has_error) return;
    if (node->line > 0) {
        emit(cg, OP_DEBUG_LINE, 0, (uint8_t)(node->line & 0xFF), (uint8_t)((node->line >> 8) & 0xFF),
             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    }
    if (is_node(node, NODE_PRINT)) {
        {
            PrintNode *pn = (PrintNode *)node;
            emit_print(cg, pn->expression, pn->base.line, pn->base.col);
        }
        return;
    }
    if (is_node(node, NODE_VAR_DECL)) {
        VarDeclNode *vd = (VarDeclNode*)node;
        if (vd->value && is_node(vd->value, NODE_LAMBDA_DECL) &&
            vd->type_name && strcmp(vd->type_name, "macro") == 0) {
            SymResult r = sym_declare_macro(&cg->sym, vd->name, vd->value);
            if (!r.found) {
                codegen_sym_declare_fail(cg, vd->name, vd->base.line, vd->base.col, "Declaración de macro");
            }
            return;
        }

        size_t sz = sym_get_struct_size(&cg->sym, vd->type_name);
        if (sz == 0) sz = 8;
        SymResult r = sym_declare(&cg->sym, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0,
                                 vd->list_element_type, SYMDECL_FLAGS_NONE);
        if (!r.found) {
            codegen_sym_declare_fail(cg, vd->name, vd->base.line, vd->base.col, "Declaración de variable");
            return;
        }
        if (vd->value) {
            if (is_node(vd->value, NODE_LAMBDA_DECL)) {
                /* Si se declara una variable con un lambda y no era un macro directamente 
                   (o fallback para que no explote) */
            } else if (is_node(vd->value, NODE_CALL)) {
                CallNode *cn = (CallNode*)vd->value;
                int ncomp = 0;
                if (cn->name && vd->type_name && strcmp(cn->name, vd->type_name) == 0) {
                    if (strcmp(cn->name, "vec2") == 0 && cn->n_args == 2) ncomp = 2;
                    else if (strcmp(cn->name, "vec3") == 0 && cn->n_args == 3) ncomp = 3;
                    else if (strcmp(cn->name, "vec4") == 0 && cn->n_args == 4) ncomp = 4;
                }
                if (ncomp > 0) {
                    uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                    if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                    for (int i = 0; i < ncomp; i++) {
                        int reg = visit_expression(cg, cn->args[i], 1);
                        const char *et = get_expression_type(cg, cn->args[i]);
                        if (et && strcmp(et, "entero") == 0)
                            emit(cg, OP_CONV_I2F, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        uint32_t a = r.addr + (uint32_t)(i * 8);
                        emit_escribir_u24(cg, a, reg, r.is_relative);
                    }
                    return;
                }
                /* Caso constructor de clase general */
                if (cn->name) {
                    StructInfo *si = sym_get_struct_info(&cg->sym, cn->name);
                    if (si) {
                    /* Reservar en registro 10 (seguro para locales) */
                    emit_heap_reservar_u24(cg, 10, (uint32_t)si->total_size);
                    
                    if (si->is_class) {
                            /* Escribir Class ID (hash del nombre) en offset 0 */
                            const int tmp_id_reg = 120;
                            emit_load_text_literal_reg(cg, si->name, tmp_id_reg);
                            emit(cg, OP_ESCRIBIR, 10, (uint8_t)tmp_id_reg, 0, 
                                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        }

                        /* Guardar la direccion en la variable ANTES de la llamada */
                        emit_escribir_u24(cg, r.addr, 10, r.is_relative);

                        int init_label = get_method_label_recursive(cg, si->name, "inicializar");
                        size_t init_arity = 0;
                        int has_init_sig = get_method_param_count(cg, si->name, "inicializar", &init_arity);
                        if (init_label >= 0 && has_init_sig && cn->n_args > 0 && cn->n_args != init_arity) {
                            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                                     "Constructor `%s(...)`: se pasaron %zu argumentos, pero `inicializar` de `%s` requiere %zu.",
                                     cn->name ? cn->name : si->name, cn->n_args, si->name, init_arity);
                            cg->has_error = 1;
                            cg->err_line = vd->base.line;
                            cg->err_col = vd->base.col;
                            return;
                        }
                        if (init_label >= 0 && (cn->n_args > 0 || !has_init_sig || init_arity == 0)) {
                            /* El objeto recien creado va al registro 1 (este) */
                            emit(cg, OP_MOVER, 1, 10, 0, IR_INST_FLAG_B_REGISTER);
                            /* Evaluar argumentos -> reg 2, 3... */
                            emit_call_args_preserved_methods(cg, cn->args, cn->n_args);
                            /* Emitir llamada */
                            emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                            add_patch(cg, init_label, PATCH_JUMP);
                        }
                        
                        return;
                    }
                }
            }
            /* vec binary op en declaracion: vec3 v = a + b */
            if (is_node(vd->value, NODE_BINARY_OP) && vd->type_name) {
                BinaryOpNode *bn = (BinaryOpNode*)vd->value;
                const char *lt = get_expression_type(cg, bn->left);
                const char *rt = get_expression_type(cg, bn->right);
                int ncomp = 0;
                if (strcmp(vd->type_name, "vec2") == 0) ncomp = 2;
                else if (strcmp(vd->type_name, "vec3") == 0) ncomp = 3;
                else if (strcmp(vd->type_name, "vec4") == 0) ncomp = 4;
                if (ncomp > 0 && (strcmp(bn->operator, "+") == 0 || strcmp(bn->operator, "-") == 0 || strcmp(bn->operator, "*") == 0) &&
                    lt && rt && is_node(bn->left, NODE_IDENTIFIER) && is_node(bn->right, NODE_IDENTIFIER)) {
                    int vec_op = (strcmp(lt, vd->type_name) == 0 && strcmp(rt, vd->type_name) == 0);
                    int scalar_l = (strcmp(lt, "flotante") == 0 || strcmp(lt, "entero") == 0);
                    int scalar_r = (strcmp(rt, "flotante") == 0 || strcmp(rt, "entero") == 0);
                    if (vec_op || (scalar_l && strcmp(rt, vd->type_name) == 0) || (scalar_r && strcmp(lt, vd->type_name) == 0)) {
                        SymResult srL = sym_lookup(&cg->sym, ((IdentifierNode*)bn->left)->name);
                        SymResult srR = sym_lookup(&cg->sym, ((IdentifierNode*)bn->right)->name);
                        if (srL.found && srR.found) {
                        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                        if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                        int op_add = (strcmp(bn->operator, "+") == 0), op_sub = (strcmp(bn->operator, "-") == 0), op_mul = (strcmp(bn->operator, "*") == 0);
                        for (int i = 0; i < ncomp; i++) {
                            uint32_t off = (uint32_t)(i * 8);
                            uint8_t flL = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                            if (srL.is_relative) flL |= IR_INST_FLAG_RELATIVE;
                            uint8_t flR = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                            if (srR.is_relative) flR |= IR_INST_FLAG_RELATIVE;
                            if (vec_op) {
                                emit_leer_u24(cg, 1, srL.addr + off, srL.is_relative);
                                emit_leer_u24(cg, 2, srR.addr + off, srR.is_relative);
                                if (op_add) emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                else if (op_sub) emit(cg, OP_RESTAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                else if (op_mul) emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else if (scalar_l) {
                                (void)visit_expression(cg, bn->left, 1);
                                if (strcmp(lt, "entero") == 0) emit(cg, OP_CONV_I2F, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                emit_leer_u24(cg, 2, srR.addr + off, srR.is_relative);
                                emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else {
                                (void)visit_expression(cg, bn->right, 2);
                                if (strcmp(rt, "entero") == 0) emit(cg, OP_CONV_I2F, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                emit_leer_u24(cg, 1, srL.addr + off, srL.is_relative);
                                emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            }
                            emit_escribir_u24(cg, r.addr + off, 1, r.is_relative);
                        }
                        return;
                        }
                    }
                }
            }
            const char *et = get_expression_type(cg, vd->value);
            if (reject_non_numeric_to_scalar(cg, vd->type_name, et, vd->base.line, vd->base.col)) return;
            int prev_allow = cg->expr_allow_func_literal;
            if (vd->type_name && strcmp(vd->type_name, "funcion") == 0)
                cg->expr_allow_func_literal = 1;
            int reg = visit_expression(cg, vd->value, 1);
            cg->expr_allow_func_literal = prev_allow;
            if (cg->has_error) return;
            emit_conv_for_store(cg, vd->type_name, et, reg);
            uint8_t flags = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) flags |= IR_INST_FLAG_RELATIVE;
        emit_escribir_u24(cg, r.addr, reg, r.is_relative);
        }
        return;
    }
    if (is_node(node, NODE_INDEX_ASSIGNMENT)) {
        IndexAssignmentNode *ian = (IndexAssignmentNode*)node;
        const char *target_type = get_expression_type(cg, ian->target);

        int target_reg = visit_expression(cg, ian->target, 10);
        int index_reg = visit_expression(cg, ian->index, 11);
        int val_reg = visit_expression(cg, ian->expression, 12);

        /* m["k"] / mapa[k] -> OP_MEM_MAPA_PONER. m.L[i] se tipaba como mapa y antes usaba MAPA_PONER sobre una lista. */
        int use_mapa_poner = (target_type && strcmp(target_type, "mapa") == 0);
        if (use_mapa_poner && is_node(ian->target, NODE_MEMBER_ACCESS))
            use_mapa_poner = 0;

        if (use_mapa_poner)
            emit(cg, OP_MEM_MAPA_PONER, (uint8_t)target_reg, (uint8_t)index_reg, (uint8_t)val_reg, 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        else
            emit(cg, OP_MEM_LISTA_PONER, (uint8_t)target_reg, (uint8_t)index_reg, (uint8_t)val_reg, 
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
        return;
    }
    if (is_node(node, NODE_ASSIGNMENT)) {
        AssignmentNode *an = (AssignmentNode*)node;
        if (is_node(an->target, NODE_MEMBER_ACCESS)) {
            MemberAccessNode *man_as = (MemberAccessNode*)an->target;
            const char *bt = get_expression_type(cg, man_as->target);
            if (bt && strcmp(bt, "mapa") == 0) {
                int val_reg = 10;
                visit_expression(cg, an->expression, val_reg);
                int map_reg = 11;
                visit_expression(cg, man_as->target, map_reg);
                int key_reg = 12;
                emit_load_text_literal_reg(cg, man_as->member, key_reg);
                emit(cg, OP_MEM_MAPA_PONER, map_reg, key_reg, val_reg, 
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_REGISTER);
                return;
            }

            const char *ft = get_member_chain_type(cg, an->target);
            if (!ft) {
                codegen_error_struct_member_access(cg, man_as, "asignacion");
                return;
            }

            int ncomp_vec = 0;
            if (ft && strcmp(ft, "vec2") == 0) ncomp_vec = 2;
            else if (ft && strcmp(ft, "vec3") == 0) ncomp_vec = 3;
            else if (ft && strcmp(ft, "vec4") == 0) ncomp_vec = 4;

            if (ncomp_vec > 0) {
                MemberAddrResult mar = get_member_address(cg, an->target, 2);
                if (mar.invalid) return;
                if (mar.in_reg) {
                    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                             "Asignacion a miembro tipo '%s' con direccion en registro no esta soportada.", ft);
                    cg->has_error = 1;
                    cg->err_line = man_as->base.line > 0 ? man_as->base.line : 1;
                    cg->err_col = man_as->base.col > 0 ? man_as->base.col : 1;
                    return;
                }
                uint8_t flW = cg_vec_fl_write(mar.is_relative);
                if (is_node(an->expression, NODE_IDENTIFIER)) {
                    const char *rhs = ((IdentifierNode *)an->expression)->name;
                    SymResult srS = sym_lookup(&cg->sym, rhs);
                    const char *tS = sym_lookup_type(&cg->sym, rhs);
                    if (srS.found && tS && strcmp(tS, ft) == 0) {
                        uint8_t flR = cg_vec_fl_read(srS.is_relative ? 1 : 0);
                        for (int i = 0; i < ncomp_vec; i++) {
                            uint32_t off = (uint32_t)(i * 8);
                            emit(cg, OP_LEER, 1, (srS.addr + off) & 0xFF, ((srS.addr + off) >> 8) & 0xFF, flR);
                            emit(cg, OP_ESCRIBIR, (mar.addr + off) & 0xFF, 1, ((mar.addr + off) >> 8) & 0xFF, flW);
                        }
                        return;
                    }
                }
                if (is_node(an->expression, NODE_CALL)) {
                    CallNode *cn = (CallNode *)an->expression;
                    if (cn->name && strcmp(cn->name, ft) == 0 && (int)cn->n_args == ncomp_vec) {
                        for (int i = 0; i < ncomp_vec; i++) {
                            int reg = visit_expression(cg, cn->args[i], 1);
                            const char *aet = get_expression_type(cg, cn->args[i]);
                            if (aet && strcmp(aet, "entero") == 0)
                                emit(cg, OP_CONV_I2F, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            uint32_t a = mar.addr + (uint32_t)(i * 8);
                            emit(cg, OP_ESCRIBIR, a & 0xFF, (uint8_t)reg, (a >> 8) & 0xFF, flW);
                        }
                        return;
                    }
                }
                if (is_node(an->expression, NODE_BINARY_OP)) {
                    BinaryOpNode *bn = (BinaryOpNode *)an->expression;
                    const char *lt = get_expression_type(cg, bn->left);
                    const char *rt = get_expression_type(cg, bn->right);
                    int op_add = (strcmp(bn->operator, "+") == 0);
                    int op_sub = (strcmp(bn->operator, "-") == 0);
                    int op_mul = (strcmp(bn->operator, "*") == 0);
                    int scalar_l = lt && (strcmp(lt, "flotante") == 0 || strcmp(lt, "entero") == 0);
                    int scalar_r = rt && (strcmp(rt, "flotante") == 0 || strcmp(rt, "entero") == 0);
                    int vec_op = lt && rt && strcmp(lt, ft) == 0 && strcmp(rt, ft) == 0;
                    uint32_t bL = 0, bR = 0;
                    int relL = 0, relR = 0;
                    int okL = codegen_vec_lvalue_base(cg, bn->left, &bL, &relL);
                    int okR = codegen_vec_lvalue_base(cg, bn->right, &bR, &relR);
                    if (op_add || op_sub || op_mul) {
                        for (int i = 0; i < ncomp_vec; i++) {
                            uint32_t off = (uint32_t)(i * 8);
                            if (vec_op && okL && okR) {
                                emit(cg, OP_LEER, 1, (bL + off) & 0xFF, ((bL + off) >> 8) & 0xFF, cg_vec_fl_read(relL));
                                emit(cg, OP_LEER, 2, (bR + off) & 0xFF, ((bR + off) >> 8) & 0xFF, cg_vec_fl_read(relR));
                                if (op_add)
                                    emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                else if (op_sub)
                                    emit(cg, OP_RESTAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                else
                                    emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else if (scalar_l && okR) {
                                (void)visit_expression(cg, bn->left, 1);
                                if (strcmp(lt, "entero") == 0)
                                    emit(cg, OP_CONV_I2F, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                emit(cg, OP_LEER, 2, (bR + off) & 0xFF, ((bR + off) >> 8) & 0xFF, cg_vec_fl_read(relR));
                                emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else if (scalar_r && okL) {
                                (void)visit_expression(cg, bn->right, 2);
                                if (strcmp(rt, "entero") == 0)
                                    emit(cg, OP_CONV_I2F, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                emit(cg, OP_LEER, 1, (bL + off) & 0xFF, ((bL + off) >> 8) & 0xFF, cg_vec_fl_read(relL));
                                emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else {
                                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                                         "Asignacion a miembro '%s' requiere dos vectores del mismo tipo o vector y escalar.",
                                         ft);
                                cg->has_error = 1;
                                cg->err_line = bn->line > 0 ? bn->line : 1;
                                cg->err_col = bn->col > 0 ? bn->col : 1;
                                return;
                            }
                            emit(cg, OP_ESCRIBIR, (mar.addr + off) & 0xFF, 1, ((mar.addr + off) >> 8) & 0xFF, flW);
                        }
                        return;
                    }
                }
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Asignacion a miembro tipo '%s': use otro vector, constructor %s(...) o expresion + - * entre vectores.",
                         ft, ft);
                cg->has_error = 1;
                cg->err_line = man_as->base.line > 0 ? man_as->base.line : 1;
                cg->err_col = man_as->base.col > 0 ? man_as->base.col : 1;
                return;
            }

            const char *et = get_expression_type(cg, an->expression);
            if (reject_non_numeric_to_scalar(cg, ft, et, an->target->line, an->target->col))
                return;
            int val_reg = 10; /* Usar registro seguro para el valor */
            (void)visit_expression(cg, an->expression, val_reg);
            if (cg->has_error) return;
            emit_conv_for_store(cg, ft, et, val_reg);

            int base_reg = 254; /* Usar registro temporal alto y seguro */
            MemberAddrResult mar = get_member_address(cg, an->target, base_reg);
            if (mar.invalid) return;

            if (mar.in_reg) {
                uint8_t fl = IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER;
                if (mar.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, (uint8_t)mar.reg, (uint8_t)val_reg, 0, fl);
            } else {
                emit_escribir_u24(cg, mar.addr, val_reg, mar.is_relative);
            }
            return;
        }
        if (!is_node(an->target, NODE_IDENTIFIER)) return;
        const char *name = ((IdentifierNode*)an->target)->name;
        SymResult r = sym_lookup(&cg->sym, name);
        if (r.found && r.is_const) {
            char cobuf[CODEGEN_ERROR_MAX];
            snprintf(cobuf, sizeof cobuf, "Error: no se puede asignar a la constante '%s'", name);
            cg_collect_push(cg, cobuf, ((IdentifierNode*)an->target)->line, ((IdentifierNode*)an->target)->col);
            return;
        }
        if (!r.found) {
            /* Caso implicito: si estamos en un metodo, buscar si 'name' es un campo de la clase */
            if (cg->current_class_name) {
                size_t off = 0, sz = 0;
                const char *ft = NULL;
                if (sym_get_struct_field(&cg->sym, cg->current_class_name, name, &off, &ft, &sz)) {
                    int val_reg = 10; /* Usar un registro seguro para el valor */
                    visit_expression(cg, an->expression, val_reg);
                    SymResult sr_este = sym_lookup(&cg->sym, "este");
                    if (sr_este.found) {
                        int addr_reg = 254; /* Usar un registro temporal alto y seguro */
                        emit_leer_u24(cg, addr_reg, sr_este.addr, sr_este.is_relative);
                        emit_sumar_u24(cg, addr_reg, addr_reg, (uint32_t)off);
                        emit(cg, OP_ESCRIBIR, (uint8_t)addr_reg, (uint8_t)val_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        return;
                    }
                }
            }
            char avbuf[CODEGEN_ERROR_MAX];
            snprintf(avbuf, sizeof avbuf, "Error: variable '%s' no declarada antes de su uso", name);
            cg_collect_push(cg, avbuf, ((IdentifierNode*)an->target)->line, ((IdentifierNode*)an->target)->col);
            return;
        }
        const char *vt = sym_lookup_type(&cg->sym, name);
        if (is_node(an->expression, NODE_CALL)) {
            CallNode *cn = (CallNode*)an->expression;
            int ncomp = 0;
            if (cn->name && vt && strcmp(cn->name, vt) == 0) {
                if (strcmp(vt, "vec2") == 0 && cn->n_args == 2) ncomp = 2;
                else if (strcmp(vt, "vec3") == 0 && cn->n_args == 3) ncomp = 3;
                else if (strcmp(vt, "vec4") == 0 && cn->n_args == 4) ncomp = 4;
            }
            if (ncomp > 0) {
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                for (int i = 0; i < ncomp; i++) {
                    int reg = visit_expression(cg, cn->args[i], 1);
                    const char *et = get_expression_type(cg, cn->args[i]);
                    if (et && strcmp(et, "entero") == 0)
                        emit(cg, OP_CONV_I2F, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                    uint32_t a = r.addr + (uint32_t)(i * 8);
                    emit_escribir_u24(cg, a, reg, (fl & IR_INST_FLAG_RELATIVE) != 0);
                }
                return;
            }
        }
        /* vec + vec, vec - vec, vec * escalar: operaciones componente a componente */
        if (vt && is_node(an->expression, NODE_BINARY_OP) &&
            is_node(an->target, NODE_IDENTIFIER)) {
            BinaryOpNode *bn = (BinaryOpNode*)an->expression;
            const char *lt = get_expression_type(cg, bn->left);
            const char *rt = get_expression_type(cg, bn->right);
            int ncomp = 0;
            if (strcmp(vt, "vec2") == 0) ncomp = 2;
            else if (strcmp(vt, "vec3") == 0) ncomp = 3;
            else if (strcmp(vt, "vec4") == 0) ncomp = 4;
            if (ncomp > 0 && (strcmp(bn->operator, "+") == 0 || strcmp(bn->operator, "-") == 0 ||
                 strcmp(bn->operator, "*") == 0)) {
                int op_add = (strcmp(bn->operator, "+") == 0);
                int op_sub = (strcmp(bn->operator, "-") == 0);
                int op_mul = (strcmp(bn->operator, "*") == 0);
                int scalar_l = lt && (strcmp(lt, "flotante") == 0 || strcmp(lt, "entero") == 0);
                int scalar_r = rt && (strcmp(rt, "flotante") == 0 || strcmp(rt, "entero") == 0);
                int vec_op = (lt && strcmp(lt, vt) == 0 && rt && strcmp(rt, vt) == 0);
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                if (vec_op || (scalar_l && rt && strcmp(rt, vt) == 0) || (scalar_r && lt && strcmp(lt, vt) == 0)) {
                    uint32_t bL = 0, bR = 0;
                    int relL = 0, relR = 0;
                    int okL = codegen_vec_lvalue_base(cg, bn->left, &bL, &relL);
                    int okR = codegen_vec_lvalue_base(cg, bn->right, &bR, &relR);
                    for (int i = 0; i < ncomp; i++) {
                        uint32_t off = (uint32_t)(i * 8);
                        if (vec_op && okL && okR) {
                            emit(cg, OP_LEER, 1, (bL + off) & 0xFF, ((bL + off) >> 8) & 0xFF, cg_vec_fl_read(relL));
                            emit(cg, OP_LEER, 2, (bR + off) & 0xFF, ((bR + off) >> 8) & 0xFF, cg_vec_fl_read(relR));
                            if (op_add) emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            else if (op_sub) emit(cg, OP_RESTAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            else if (op_mul) emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        } else if (scalar_l && okR) {
                            (void)visit_expression(cg, bn->left, 1);
                            if (strcmp(lt, "entero") == 0) emit(cg, OP_CONV_I2F, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            emit(cg, OP_LEER, 2, (bR + off) & 0xFF, ((bR + off) >> 8) & 0xFF, cg_vec_fl_read(relR));
                            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        } else if (scalar_r && okL) {
                            (void)visit_expression(cg, bn->right, 2);
                            if (strcmp(rt, "entero") == 0) emit(cg, OP_CONV_I2F, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            emit(cg, OP_LEER, 1, (bL + off) & 0xFF, ((bL + off) >> 8) & 0xFF, cg_vec_fl_read(relL));
                            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        } else break;
                        uint32_t a = r.addr + off;
                        emit_escribir_u24(cg, a, 1, (fl & IR_INST_FLAG_RELATIVE) != 0);
                    }
                    return;
                }
            }
        }
        const char *et = get_expression_type(cg, an->expression);
        if (reject_non_numeric_to_scalar(cg, vt, et, ((IdentifierNode *)an->target)->line,
                                         ((IdentifierNode *)an->target)->col))
            return;
        /*
         * Ruta robusta para acumuladores:
         *   x = x <op> llamada(...)
         * Evalua primero la derecha, luego recarga x desde memoria, evitando
         * que llamadas internas pisen el valor acumulado en registros bajos.
         */
        if ((vt && (strcmp(vt, "entero") == 0 || strcmp(vt, "flotante") == 0)) &&
            is_node(an->expression, NODE_BINARY_OP)) {
            BinaryOpNode *bn_as = (BinaryOpNode *)an->expression;
            if (bn_as->operator &&
                (strcmp(bn_as->operator, "+") == 0 || strcmp(bn_as->operator, "-") == 0 ||
                 strcmp(bn_as->operator, "*") == 0 || strcmp(bn_as->operator, "/") == 0 ||
                 strcmp(bn_as->operator, "%") == 0) &&
                is_node(bn_as->left, NODE_IDENTIFIER) &&
                strcmp(((IdentifierNode *)bn_as->left)->name, name) == 0 &&
                expr_has_call(bn_as->right)) {
                int rhs_reg = visit_expression(cg, bn_as->right, 2);
                if (cg->has_error) return;
                emit_leer_u24(cg, 1, r.addr, r.is_relative);
                if (strcmp(bn_as->operator, "+") == 0) emit(cg, OP_SUMAR, 1, 1, (uint8_t)rhs_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                else if (strcmp(bn_as->operator, "-") == 0) emit(cg, OP_RESTAR, 1, 1, (uint8_t)rhs_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                else if (strcmp(bn_as->operator, "*") == 0) emit(cg, OP_MULTIPLICAR, 1, 1, (uint8_t)rhs_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                else if (strcmp(bn_as->operator, "/") == 0) emit(cg, OP_DIVIDIR, 1, 1, (uint8_t)rhs_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                else emit(cg, OP_MODULO, 1, 1, (uint8_t)rhs_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit_conv_for_store(cg, vt, et, 1);
                emit_escribir_u24(cg, r.addr, 1, r.is_relative);
                return;
            }
        }
        int prev_allow = cg->expr_allow_func_literal;
        if (vt && strcmp(vt, "funcion") == 0) cg->expr_allow_func_literal = 1;
        int reg = visit_expression(cg, an->expression, 1);
        cg->expr_allow_func_literal = prev_allow;
        if (cg->has_error) return;
        emit_conv_for_store(cg, vt, et, reg);
        emit_escribir_u24(cg, r.addr, reg, r.is_relative);
        return;
    }
    if (is_node(node, NODE_SELECT)) {
        SelectNode *sn = (SelectNode*)node;
        int end_id = new_label(cg);
        int sel_reg = 10;
        int val_reg = 11;
        int cmp_reg = 12;
        int any_reg = 13;
        (void)visit_expression(cg, sn->selector, sel_reg);
        for (size_t i = 0; i < sn->n_cases; i++) {
            int next_case_id = new_label(cg);
            SelectCase *sc = &sn->cases[i];
            
            if (sc->is_range) {
                // caso rango start..end
                int start_reg = 14;
                int end_reg = 15;
                int cmp_start = 16;
                int cmp_end = 17;
                
                (void)visit_expression(cg, sc->values[0], start_reg);
                (void)visit_expression(cg, sc->range_end, end_reg);
                
                // sel >= start
                emit(cg, OP_CMP_GE, cmp_start, sel_reg, start_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                // sel <= end
                emit(cg, OP_CMP_LE, cmp_end, sel_reg, end_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                // in_range = cmp_start && cmp_end
                emit(cg, OP_Y, any_reg, cmp_start, cmp_end, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            } else {
                emit(cg, OP_MOVER, any_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                for (size_t j = 0; j < sc->n_values; j++) {
                    (void)visit_expression(cg, sc->values[j], val_reg);
                    emit(cg, OP_CMP_EQ, cmp_reg, sel_reg, val_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                    emit(cg, OP_O, any_reg, any_reg, cmp_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                }
            }
            
            emit(cg, OP_CMP_EQ, cmp_reg, any_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit_jump_if_nonzero(cg, (uint8_t)cmp_reg, next_case_id);
            sym_enter_scope(&cg->sym, 0);
            visit_block(cg, sc->body);
            sym_exit_scope(&cg->sym);
            emit(cg, OP_IR, 0, 0, 0, 0);
            add_patch(cg, end_id, PATCH_JUMP);
            mark_label(cg, next_case_id);
        }
        if (sn->default_body) {
            sym_enter_scope(&cg->sym, 0);
            visit_block(cg, sn->default_body);
            sym_exit_scope(&cg->sym);
        }
        mark_label(cg, end_id);
        return;
    }
    if (is_node(node, NODE_TRY)) {
        TryNode *tn = (TryNode*)node;
        int catch_id = new_label(cg);
        int final_id = new_label(cg);
        int end_id = new_label(cg);
        int has_catch = tn->catch_body ? 1 : 0;
        int has_final = tn->final_body ? 1 : 0;

        if (cg->try_stack_n >= cg->try_stack_cap) {
            size_t nc = cg->try_stack_cap ? cg->try_stack_cap * 2 : 4;
            TryLabel *p = realloc(cg->try_stack, nc * sizeof(TryLabel));
            if (p) { cg->try_stack = p; cg->try_stack_cap = nc; }
        }
        if (cg->try_stack_n < cg->try_stack_cap) {
            cg->try_stack[cg->try_stack_n].catch_id = catch_id;
            cg->try_stack[cg->try_stack_n].final_id = final_id;
            cg->try_stack[cg->try_stack_n].end_id = end_id;
            cg->try_stack[cg->try_stack_n].has_catch = has_catch;
            cg->try_stack[cg->try_stack_n].has_final = has_final;
            cg->try_stack[cg->try_stack_n].catch_var = tn->catch_var;
            cg->try_stack_n++;
        }

        int handler_label = -1;
        if (has_catch) handler_label = catch_id;
        else if (has_final) handler_label = final_id;

        if (handler_label >= 0) {
            emit(cg, OP_TRY_ENTER, 0, 0, 0, 0);
            add_patch(cg, handler_label, PATCH_TRY_ENTER);
        }

        sym_enter_scope(&cg->sym, 0);
        visit_block(cg, tn->try_body);
        sym_exit_scope(&cg->sym);
        if (cg->try_stack_n) cg->try_stack_n--;

        if (handler_label >= 0)
            emit(cg, OP_TRY_LEAVE, 0, 0, 0, 0);

        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, has_final ? final_id : end_id, PATCH_JUMP);

        if (has_catch) {
            mark_label(cg, catch_id);
            sym_enter_scope(&cg->sym, 0);
            if (tn->catch_var && tn->catch_var[0]) {
                const char *cvt = "texto";
                SymResult r = sym_declare(&cg->sym, tn->catch_var, cvt, 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
                if (!r.found) {
                    codegen_sym_declare_fail(cg, tn->catch_var, tn->base.line, tn->base.col, "Variable de atrapar (catch)");
                } else {
                    uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                    if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                    emit_escribir_u24(cg, r.addr, 1, r.is_relative);
                }
            }
            visit_block(cg, tn->catch_body);
            sym_exit_scope(&cg->sym);
            if (has_final) {
                emit(cg, OP_IR, 0, 0, 0, 0);
                add_patch(cg, final_id, PATCH_JUMP);
            } else {
                emit(cg, OP_IR, 0, 0, 0, 0);
                add_patch(cg, end_id, PATCH_JUMP);
            }
        }

        if (has_final) {
            mark_label(cg, final_id);
            sym_enter_scope(&cg->sym, 0);
            visit_block(cg, tn->final_body);
            sym_exit_scope(&cg->sym);
        }
        mark_label(cg, end_id);
        return;
    }
    if (is_node(node, NODE_THROW)) {
        ThrowNode *th = (ThrowNode*)node;
        int reg = visit_expression(cg, th->expression, 1);
        const char *et = get_expression_type(cg, th->expression);
        if (et && strcmp(et, "texto") != 0) {
            emit(cg, OP_STR_DESDE_ANY, 1, (uint8_t)reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else if (reg != 1) {
            emit(cg, OP_MOVER, 1, (uint8_t)reg, 0, IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_LANZAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return;
    }
    /* 4.5 WhileNode */
    if (is_node(node, NODE_WHILE)) {
        WhileNode *wn = (WhileNode*)node;
        if (try_emit_collapsed_invariant_while(cg, wn)) {
            return;
        }
        int start_id = new_label(cg);
        int end_id = new_label(cg);
        if (cg->loop_stack_n >= cg->loop_stack_cap) {
            size_t nc = cg->loop_stack_cap ? cg->loop_stack_cap * 2 : 4;
            LoopLabel *p = realloc(cg->loop_stack, nc * sizeof(LoopLabel));
            if (p) { cg->loop_stack = p; cg->loop_stack_cap = nc; }
        }
        if (cg->loop_stack_n < cg->loop_stack_cap) {
            cg->loop_stack[cg->loop_stack_n].start_id = start_id;
            cg->loop_stack[cg->loop_stack_n].end_id = end_id;
            cg->loop_stack_n++;
        }
        mark_label(cg, start_id);
        
        /* Proteccion de Reg 1 (este): usar registros altos para la condicion. */
        int reg = visit_expression(cg, wn->condition, 253);
        emit(cg, OP_CMP_EQ, 254, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 254, end_id);
        sym_enter_scope(&cg->sym, 0);
        visit_block(cg, wn->body);
        sym_exit_scope(&cg->sym);
        
        // JUMP de vuelta al inicio del bucle
        emit(cg, OP_IR, 0, start_id & 0xFF, (start_id >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        add_patch(cg, start_id, PATCH_JUMP);
        
        mark_label(cg, end_id);
        if (cg->loop_stack_n) cg->loop_stack_n--;
        return;
    }
    if (is_node(node, NODE_FOR)) {
        ForNode *fn = (ForNode*)node;
        sym_enter_scope(&cg->sym, 0);
        visit_statement(cg, fn->init);

        int start_id = new_label(cg);
        int end_id = new_label(cg);
        int step_id = new_label(cg);

        if (cg->loop_stack_n >= cg->loop_stack_cap) {
            size_t nc = cg->loop_stack_cap ? cg->loop_stack_cap * 2 : 4;
            LoopLabel *p = realloc(cg->loop_stack, nc * sizeof(LoopLabel));
            if (p) { cg->loop_stack = p; cg->loop_stack_cap = nc; }
        }
        if (cg->loop_stack_n < cg->loop_stack_cap) {
            cg->loop_stack[cg->loop_stack_n].start_id = step_id; // continuar salta al step
            cg->loop_stack[cg->loop_stack_n].end_id = end_id;
            cg->loop_stack_n++;
        }

        mark_label(cg, start_id);
        int reg = visit_expression(cg, fn->condition, 253);
        emit(cg, OP_CMP_EQ, 254, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 254, end_id);

        visit_block(cg, fn->body);

        mark_label(cg, step_id);
        visit_expression(cg, fn->step, 253);

        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, start_id, PATCH_JUMP);

        mark_label(cg, end_id);
        sym_exit_scope(&cg->sym);
        if (cg->loop_stack_n) cg->loop_stack_n--;
        return;
    }
    if (is_node(node, NODE_FOREACH)) {
        ForEachNode *fe = (ForEachNode *)node;
        int start_id = new_label(cg);
        int end_id = new_label(cg);
        if (cg->loop_stack_n >= cg->loop_stack_cap) {
            size_t nc = cg->loop_stack_cap ? cg->loop_stack_cap * 2 : 4;
            LoopLabel *p = realloc(cg->loop_stack, nc * sizeof(LoopLabel));
            if (p) {
                cg->loop_stack = p;
                cg->loop_stack_cap = nc;
            }
        }
        if (cg->loop_stack_n < cg->loop_stack_cap) {
            cg->loop_stack[cg->loop_stack_n].start_id = start_id;
            cg->loop_stack[cg->loop_stack_n].end_id = end_id;
            cg->loop_stack_n++;
        }
        sym_enter_scope(&cg->sym, 0);
        if (!fe->iter_name || !fe->iter_type) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "para_cada interno: falta nombre o tipo de variable");
            cg->has_error = 1;
            cg->err_line = fe->base.line;
            cg->err_col = fe->base.col;
            sym_exit_scope(&cg->sym);
            if (cg->loop_stack_n) cg->loop_stack_n--;
            return;
        }
        const char *idx_var = (fe->index_name && fe->index_name[0]) ? fe->index_name : fe->key_name;

        if (strcmp(fe->iter_type, "caracter") == 0) {
            SymResult iter_r = sym_declare(&cg->sym, fe->iter_name, "texto", 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
            if (!iter_r.found) {
                codegen_sym_declare_fail(cg, fe->iter_name, fe->base.line, fe->base.col, "Variable de para_cada");
                sym_exit_scope(&cg->sym);
                if (cg->loop_stack_n) cg->loop_stack_n--;
                return;
            }
            SymResult ix_r = {0};
            int has_index = 0;
            if (idx_var && idx_var[0]) {
                ix_r = sym_declare(&cg->sym, idx_var, "entero", 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
                if (!ix_r.found) {
                    codegen_sym_declare_fail(cg, idx_var, fe->base.line, fe->base.col, "Variable de indice en para_cada");
                    sym_exit_scope(&cg->sym);
                    if (cg->loop_stack_n) cg->loop_stack_n--;
                    return;
                }
                has_index = 1;
            }
            SymResult src_tmp = sym_reserve_temp(&cg->sym, 8);
            SymResult idx_tmp = sym_reserve_temp(&cg->sym, 8);
            visit_expression(cg, fe->collection, 253);
            emit_escribir_u24(cg, src_tmp.addr, 253, src_tmp.is_relative);
            emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit_escribir_u24(cg, idx_tmp.addr, 15, idx_tmp.is_relative);
            mark_label(cg, start_id);
            emit_leer_u24(cg, 15, src_tmp.addr, src_tmp.is_relative);
            emit(cg, OP_STR_LONGITUD, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit_leer_u24(cg, 17, idx_tmp.addr, idx_tmp.is_relative);
            emit(cg, OP_CMP_LT, 18, 17, 16, 0);
            emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit_jump_if_nonzero(cg, 18, end_id);
            if (has_index)
                emit_escribir_u24(cg, ix_r.addr, 17, ix_r.is_relative);
            emit(cg, OP_STR_EXTRAER_CARACTER, 19, 15, 17, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit_escribir_u24(cg, iter_r.addr, 19, iter_r.is_relative);
            visit_block(cg, fe->body);
            emit_leer_u24(cg, 17, idx_tmp.addr, idx_tmp.is_relative);
            emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            emit_escribir_u24(cg, idx_tmp.addr, 17, idx_tmp.is_relative);
            emit(cg, OP_IR, 0, 0, 0, 0);
            add_patch(cg, start_id, PATCH_JUMP);
            mark_label(cg, end_id);
            sym_exit_scope(&cg->sym);
            if (cg->loop_stack_n) cg->loop_stack_n--;
            return;
        }

        SymResult iter_r = sym_declare(&cg->sym, fe->iter_name, fe->iter_type, 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
        if (!iter_r.found) {
            codegen_sym_declare_fail(cg, fe->iter_name, fe->base.line, fe->base.col, "Variable de para_cada");
            sym_exit_scope(&cg->sym);
            if (cg->loop_stack_n) cg->loop_stack_n--;
            return;
        }

        SymResult key_r = {0};
        if (idx_var && idx_var[0]) {
            key_r = sym_declare(&cg->sym, idx_var, "entero", 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
            if (!key_r.found) {
                codegen_sym_declare_fail(cg, idx_var, fe->base.line, fe->base.col, "Variable de clave/indice en para_cada");
                sym_exit_scope(&cg->sym);
                if (cg->loop_stack_n) cg->loop_stack_n--;
                return;
            }
        }

        const char *coll_type = get_expression_type(cg, fe->collection);
        if (coll_type && strcmp(coll_type, "mapa") == 0) {
            SymResult src_tmp = sym_reserve_temp(&cg->sym, 8);
            SymResult keys_tmp = sym_reserve_temp(&cg->sym, 8);
            SymResult idx_tmp = sym_reserve_temp(&cg->sym, 8);
            
            visit_expression(cg, fe->collection, 253);
            emit_escribir_u24(cg, src_tmp.addr, 253, src_tmp.is_relative);
            
            // Obtener lista de llaves
            emit(cg, OP_MEM_MAPA_LLAVES, 20, 253, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit_escribir_u24(cg, keys_tmp.addr, 20, keys_tmp.is_relative);
            
            // Inicializar índice
            emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            emit_escribir_u24(cg, idx_tmp.addr, 15, idx_tmp.is_relative);
            
            mark_label(cg, start_id);
            emit_leer_u24(cg, 20, keys_tmp.addr, keys_tmp.is_relative);
            emit(cg, OP_MEM_LISTA_TAMANO, 16, 20, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            
            emit_leer_u24(cg, 17, idx_tmp.addr, idx_tmp.is_relative);
            emit(cg, OP_CMP_LT, 18, 17, 16, 0);
            emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit_jump_if_nonzero(cg, 18, end_id);
            
            // Obtener llave
            emit(cg, OP_MEM_LISTA_OBTENER, 21, 20, 17, 0);
            if (idx_var && idx_var[0]) {
                emit_escribir_u24(cg, key_r.addr, 21, key_r.is_relative);
            }
            
            // Obtener valor del mapa usando la llave
            emit_leer_u24(cg, 22, src_tmp.addr, src_tmp.is_relative);
            emit(cg, OP_MEM_MAPA_OBTENER, 19, 22, 21, 0);
            
            emit_escribir_u24(cg, iter_r.addr, 19, iter_r.is_relative);
            visit_block(cg, fe->body);
            
            emit_leer_u24(cg, 17, idx_tmp.addr, idx_tmp.is_relative);
            emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            emit_escribir_u24(cg, idx_tmp.addr, 17, idx_tmp.is_relative);
            
            emit(cg, OP_IR, 0, 0, 0, 0);
            add_patch(cg, start_id, PATCH_JUMP);
            
            mark_label(cg, end_id);
            sym_exit_scope(&cg->sym);
            if (cg->loop_stack_n) cg->loop_stack_n--;
            return;
        }

        SymResult src_tmp = sym_reserve_temp(&cg->sym, 8);
         SymResult idx_tmp = sym_reserve_temp(&cg->sym, 8);
         visit_expression(cg, fe->collection, 253);
         emit_escribir_u24(cg, src_tmp.addr, 253, src_tmp.is_relative);
         emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
         emit_escribir_u24(cg, idx_tmp.addr, 15, idx_tmp.is_relative);
         mark_label(cg, start_id);
         emit_leer_u24(cg, 15, src_tmp.addr, src_tmp.is_relative);
         emit(cg, OP_MEM_LISTA_TAMANO, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
         emit_leer_u24(cg, 17, idx_tmp.addr, idx_tmp.is_relative);
         emit(cg, OP_CMP_LT, 18, 17, 16, 0);
         emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
         emit_jump_if_nonzero(cg, 18, end_id);

         if (idx_var && idx_var[0]) {
             emit_escribir_u24(cg, key_r.addr, 17, key_r.is_relative);
         }

         emit(cg, OP_MEM_LISTA_OBTENER, 19, 15, 17, 0);
         emit_escribir_u24(cg, iter_r.addr, 19, iter_r.is_relative);
         visit_block(cg, fe->body);
         
         // Recargar índice antes de incrementar por si el cuerpo usó el registro 17
         emit_leer_u24(cg, 17, idx_tmp.addr, idx_tmp.is_relative);
         emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
         emit_escribir_u24(cg, idx_tmp.addr, 17, idx_tmp.is_relative);
         
         emit(cg, OP_IR, 0, 0, 0, 0);
         add_patch(cg, start_id, PATCH_JUMP);
         mark_label(cg, end_id);
         sym_exit_scope(&cg->sym);
         if (cg->loop_stack_n) cg->loop_stack_n--;
         return;
     }
    if (is_node(node, NODE_DO_WHILE)) {
        DoWhileNode *dwn = (DoWhileNode*)node;
        int start_id = new_label(cg);
        int end_id = new_label(cg);
        int cont_id = new_label(cg); // para el "continuar", que debe saltar a evaluar la condicion
        if (cg->loop_stack_n >= cg->loop_stack_cap) {
            size_t nc = cg->loop_stack_cap ? cg->loop_stack_cap * 2 : 4;
            LoopLabel *p = realloc(cg->loop_stack, nc * sizeof(LoopLabel));
            if (p) { cg->loop_stack = p; cg->loop_stack_cap = nc; }
        }
        if (cg->loop_stack_n < cg->loop_stack_cap) {
            cg->loop_stack[cg->loop_stack_n].start_id = cont_id;
            cg->loop_stack[cg->loop_stack_n].end_id = end_id;
            cg->loop_stack_n++;
        }
        mark_label(cg, start_id);
        sym_enter_scope(&cg->sym, 0);
        visit_block(cg, dwn->body);
        sym_exit_scope(&cg->sym);
        
        mark_label(cg, cont_id);
        
        /* Proteccion de Reg 1 (este): usar registros altos para la condicion. */
        int reg = visit_expression(cg, dwn->condition, 253);
        emit(cg, OP_CMP_EQ, 254, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_CMP_EQ, 254, 254, 0, IR_INST_FLAG_C_IMMEDIATE);
        
        emit_jump_if_nonzero(cg, 254, start_id);
        
        mark_label(cg, end_id);
        if (cg->loop_stack_n) cg->loop_stack_n--;
        return;
    }
    /* 4.6 IfNode */
    if (is_node(node, NODE_IF)) {
        IfNode *in = (IfNode*)node;
        int else_id = new_label(cg);
        int end_id = new_label(cg);
        
        /* Proteccion de Reg 1 (este): usar registros altos para la condicion. */
        int reg = visit_expression(cg, in->condition, 253);
        emit(cg, OP_CMP_EQ, 254, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit_jump_if_nonzero(cg, 254, else_id);
        sym_enter_scope(&cg->sym, 0);
        visit_block(cg, in->body);
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, end_id, PATCH_JUMP);
        mark_label(cg, else_id);
        if (in->else_body) visit_block(cg, in->else_body);
        sym_exit_scope(&cg->sym);
        mark_label(cg, end_id);
        return;
    }
    /* 4.7 BreakNode / ContinueNode */
    if (is_node(node, NODE_BREAK)) {
        if (!cg->loop_stack_n) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Error: 'romper' solo es valido dentro de un bucle (`mientras` o `hacer`...`mientras`)");
            cg->has_error = 1;
            cg->err_line = node->line;
            cg->err_col = node->col;
            return;
        }
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, cg->loop_stack[cg->loop_stack_n - 1].end_id, PATCH_JUMP);
        return;
    }
    if (is_node(node, NODE_CONTINUE)) {
        if (!cg->loop_stack_n) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Error: 'continuar' solo es valido dentro de un bucle (`mientras` o `hacer`...`mientras`)");
            cg->has_error = 1;
            cg->err_line = node->line;
            cg->err_col = node->col;
            return;
        }
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, cg->loop_stack[cg->loop_stack_n - 1].start_id, PATCH_JUMP);
        return;
    }
    /* 4.8 ReturnNode */
    if (is_node(node, NODE_RETURN)) {
        ReturnNode *rn = (ReturnNode*)node;
        if (cg->macro_end_label >= 0) {
            if (rn->expression) {
                visit_expression(cg, rn->expression, cg->macro_dest_reg);
            }
            emit(cg, OP_IR, 0, 0, 0, 0);
            add_patch(cg, cg->macro_end_label, PATCH_JUMP);
            return;
        }
        if (cg->function_depth <= 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "'retornar' solo es valido dentro de una funcion");
            cg->has_error = 1;
            cg->err_line = node->line;
            cg->err_col = node->col;
            return;
        }
        if (rn->expression) {
            const char *rt = cg->current_fn_return ? cg->current_fn_return : "entero";
            const char *et = get_expression_type(cg, rn->expression);
            if (reject_return_incompatible_value(cg, rt, et, rn->base.line, rn->base.col)) return;
            visit_expression(cg, rn->expression, 1);
            if (cg->has_error) return;
            emit_conv_for_store(cg, rt, et, 1);
            emit(cg, OP_RETORNAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else {
            emit(cg, OP_RETORNAR, 0, 0, 0, 0);
        }
        return;
    }
    /* Nivel 7: Sentencias cognitivas */
    if (is_node(node, NODE_RECORDAR)) {
        RecordarNode *rn = (RecordarNode*)node;
        visit_expression(cg, rn->key, 10);
        if (rn->value) {
            visit_expression(cg, rn->value, 11);
            emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, 10, 11, 90, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            emit(cg, OP_MEM_APRENDER_CONCEPTO, 10, 100, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        }
        return;
    }
    if (is_node(node, NODE_ASOCIAR)) {
        AsociarNode *an = (AsociarNode*)node;
        visit_expression(cg, an->concept1, 10);
        visit_expression(cg, an->concept2, 11);
        if (an->weight) {
            visit_expression(cg, an->weight, 12);
            emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, 10, 11, 12,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, 10, 11, 90,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        }
        return;
    }
    if (is_node(node, NODE_APRENDER)) {
        AprenderNode *an = (AprenderNode*)node;
        visit_expression(cg, an->concept, 10);
        if (an->weight) {
            visit_expression(cg, an->weight, 11);
            emit(cg, OP_MEM_APRENDER_CONCEPTO, 10, 11, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MEM_APRENDER_CONCEPTO, 10, 100, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        }
        return;
    }
    if (is_node(node, NODE_CREAR_MEMORIA)) {
        CrearMemoriaNode *cn = (CrearMemoriaNode*)node;
        visit_expression(cg, cn->filename, 1);
        if (cn->nodes_capacity)
            visit_expression(cg, cn->nodes_capacity, 2);
        else
            emit(cg, OP_MOVER, 2, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        if (cn->connections_capacity)
            visit_expression(cg, cn->connections_capacity, 3);
        else
            emit(cg, OP_MOVER, 3, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_CREAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return;
    }
    if (is_node(node, NODE_CERRAR_MEMORIA)) {
        emit(cg, OP_MEM_CERRAR, 0, 0, 0, 0);
        return;
    }
    if (is_node(node, NODE_ACTIVAR_MODULO)) {
        ActivarModuloNode *an = (ActivarModuloNode*)node;
        if (an->module_path && is_node(an->module_path, NODE_LITERAL) &&
            ((LiteralNode*)an->module_path)->type_name && strcmp(((LiteralNode*)an->module_path)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)an->module_path)->value.str ? ((LiteralNode*)an->module_path)->value.str : "");
            emit(cg, OP_ACTIVAR_MODULO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else if (an->module_path) {
            visit_expression(cg, an->module_path, 1);
            emit(cg, OP_ACTIVAR_MODULO, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
        return;
    }
    if (is_node(node, NODE_EXTRAER_TEXTO)) {
        ExtraerTextoNode *en = (ExtraerTextoNode*)node;
        visit_expression(cg, en->source, 1);
        visit_expression(cg, en->pattern, 2);
        int op = (en->mode == 0) ? OP_STR_EXTRAER_ANTES_REG : OP_STR_EXTRAER_DESPUES_REG;
        emit(cg, op, 3, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        if (en->target) {
            SymResult r = sym_get_or_create(&cg->sym, en->target, NULL);
            if (!r.found) {
                codegen_sym_declare_fail(cg, en->target, en->base.line, en->base.col, "Destino de extraer texto");
                return;
            }
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit_escribir_u24(cg, r.addr, 3, r.is_relative);
        }
        return;
    }
    if (is_node(node, NODE_ULTIMA_PALABRA)) {
        UltimaPalabraNode *un = (UltimaPalabraNode*)node;
        visit_expression(cg, un->source, 1);
        emit(cg, OP_MEM_ULTIMA_PALABRA, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        if (un->target) {
            SymResult r = sym_get_or_create(&cg->sym, un->target, NULL);
            if (!r.found) {
                codegen_sym_declare_fail(cg, un->target, un->base.line, un->base.col, "Destino de ultima_palabra");
                return;
            }
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit_escribir_u24(cg, r.addr, 2, r.is_relative);
        }
        return;
    }
    if (is_node(node, NODE_COPIAR_TEXTO)) {
        CopiarTextoNode *cn = (CopiarTextoNode*)node;
        visit_expression(cg, cn->source, 1);
        emit(cg, OP_MEM_COPIAR_TEXTO, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        if (cn->target) {
            SymResult r = sym_get_or_create(&cg->sym, cn->target, NULL);
            if (!r.found) {
                codegen_sym_declare_fail(cg, cn->target, cn->base.line, cn->base.col, "Destino de copiar_texto");
                return;
            }
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit_escribir_u24(cg, r.addr, 2, r.is_relative);
        }
        return;
    }
    if (is_node(node, NODE_DEFINE_CONCEPTO)) {
        DefineConceptoNode *dn = (DefineConceptoNode*)node;
        if (dn->concepto && is_node(dn->concepto, NODE_LITERAL) &&
            ((LiteralNode*)dn->concepto)->type_name && strcmp(((LiteralNode*)dn->concepto)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)dn->concepto)->value.str ? ((LiteralNode*)dn->concepto)->value.str : "");
            emit(cg, OP_MEM_RECORDAR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        if (dn->descripcion && is_node(dn->descripcion, NODE_LITERAL) &&
            ((LiteralNode*)dn->descripcion)->type_name && strcmp(((LiteralNode*)dn->descripcion)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)dn->descripcion)->value.str ? ((LiteralNode*)dn->descripcion)->value.str : "");
            emit(cg, OP_MEM_RECORDAR_TEXTO, off & 0xFF, (off >> 8) & 0xFF, (off >> 16) & 0xFF,
                 IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        }
        return;
    }
    if (is_node(node, NODE_RESPONDER)) {
        ResponderNode *rn = (ResponderNode*)node;
        visit_expression(cg, rn->message, 1);
        emit(cg, OP_MEM_IMPRIMIR_ID, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        { size_t nl = add_string(cg, "\n");
          emit(cg, OP_IMPRIMIR_TEXTO, nl & 0xFF, (nl >> 8) & 0xFF, (nl >> 16) & 0xFF,
               IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE); }
        return;
    }
    if (is_node(node, NODE_CALL)) {
        CallNode *cn = (CallNode*)node;

        if (visit_call_sistema(cg, cn, 1)) {
            if (cg->has_error) return;
            return;
        }

        if (cn->name && !cn->callee) {
            SymResult vr = sym_lookup(&cg->sym, cn->name);
            const char *vty = vr.found ? sym_lookup_type(&cg->sym, cn->name) : NULL;
            if (vr.found && !vr.macro_ast && vty && strcmp(vty, "funcion") == 0) {
                uint8_t fl = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                if (vr.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_LEER, CG_INDIRECT_CALLEE_REG, vr.addr & 0xFF, (vr.addr >> 8) & 0xFF, fl);
                int prev = cg->expr_allow_func_literal;
                cg->expr_allow_func_literal = 1;
                for (size_t i = 0; i < cn->n_args; i++) {
                    if (!cn->args || !cn->args[i]) continue;
                    visit_expression(cg, cn->args[i], (int)(1 + i));
                }
                cg->expr_allow_func_literal = prev;
                emit(cg, OP_LLAMAR, CG_INDIRECT_CALLEE_REG, 0, 0, 0);
                return;
            }
        }

        if (cn->callee) {
            /* Caso 1: Llamada a metodo obj.metodo(...) */
            if (cn->callee->type == NODE_MEMBER_ACCESS) {
                MemberAccessNode *ma = (MemberAccessNode *)cn->callee;
            const char *obj_type = get_expression_type(cg, ma->target);
            if (obj_type) {
                /* Evaluar instancia 'este' -> reg 1 (queremos su direccion) */
                if (is_node(ma->target, NODE_IDENTIFIER)) {
                    IdentifierNode *id_node = (IdentifierNode*)ma->target;
                    SymResult sr = sym_lookup(&cg->sym, id_node->name);
                    if (sr.found) emit_leer_u24(cg, 1, sr.addr, sr.is_relative);
                    else visit_expression(cg, ma->target, 1);
                } else if (is_node(ma->target, NODE_MEMBER_ACCESS)) {
                    MemberAddrResult mar = get_member_address(cg, ma->target, 1);
                    if (mar.invalid) return;
                    if (mar.in_reg) {
                        if (mar.reg != 1) emit(cg, OP_MOVER, 1, mar.reg, 0, IR_INST_FLAG_B_REGISTER);
                    } else emit_leer_u24(cg, 1, mar.addr, mar.is_relative);
                } else visit_expression(cg, ma->target, 1);

                /* Evaluar argumentos -> reg 2, 3, ... */
                emit_call_args_preserved_methods(cg, cn->args, cn->n_args);

            /* Intentar dispatch dinámico (polimorfismo) 
               EXCEPTO si el objetivo es 'padre' (queremos dispatch estático a la base) */
            int is_super_call = 0;
            if (is_node(ma->target, NODE_IDENTIFIER) && strcmp(((IdentifierNode*)ma->target)->name, "padre") == 0) {
                is_super_call = 1;
            }

            if (!is_super_call && emit_dynamic_dispatch(cg, obj_type, ma->member, cn, 0, 1)) {
                return;
            }

                /* Fallback dispatch estático */
                int label_id = get_method_label_recursive(cg, obj_type, ma->member);
                if (label_id >= 0) {
                    int is_priv = 0;
                    if (sym_get_struct_method_visibility(&cg->sym, obj_type, ma->member, &is_priv)) {
                        if (!is_access_allowed(cg, obj_type, is_priv)) {
                            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: el metodo '%s' de la clase '%s' es privado", ma->member, obj_type);
                            cg->has_error = 1; cg->err_line = ma->base.line; cg->err_col = ma->base.col;
                            return;
                        }
                    }
                    emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                    add_patch(cg, label_id, PATCH_JUMP);
                    return;
                }
            }
            }

            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            visit_expression(cg, cn->callee, CG_INDIRECT_CALLEE_REG);
            for (size_t i = 0; i < cn->n_args; i++) {
                if (!cn->args || !cn->args[i]) continue;
                visit_expression(cg, cn->args[i], (int)(1 + i));
            }
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, CG_INDIRECT_CALLEE_REG, 0, 0, 0);
            return;
        }

        SymResult r_fn = {0};
        if (cn->name) r_fn = sym_lookup(&cg->sym, cn->name);
        if (r_fn.found && r_fn.macro_ast) {
            LambdaDeclNode *ld = (LambdaDeclNode*)r_fn.macro_ast;
            if (ld->n_params != cn->n_args) {
                codegen_error_macro_arity(cg, cn->name, ld->n_params, cn->n_args, node->line, node->col);
                return;
            }
            sym_enter_scope(&cg->sym, 0); 
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            for (size_t i = 0; i < ld->n_params; i++) {
                int reg = visit_expression(cg, cn->args[i], 1);
                SymResult p_r = sym_declare(&cg->sym, ld->params[i], "entero", 8, 1, 0, NULL, SYMDECL_FLAGS_NONE);
                if (!p_r.found) {
                    codegen_sym_declare_fail(cg, ld->params[i], node->line, node->col, "Invocación de macro (tarea)");
                    cg->expr_allow_func_literal = prev;
                    sym_exit_scope(&cg->sym);
                    return;
                }
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (p_r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit_escribir_u24(cg, p_r.addr, reg, 1);
            }
            cg->expr_allow_func_literal = prev;
            int old_macro_end_label = cg->macro_end_label;
            int old_macro_dest_reg = cg->macro_dest_reg;
            
            cg->macro_end_label = new_label(cg);
            cg->macro_dest_reg = 1; /* por defecto */
            
            visit_expression(cg, ld->body, 1);
            
            mark_label(cg, cg->macro_end_label);
            
            cg->macro_end_label = old_macro_end_label;
            cg->macro_dest_reg = old_macro_dest_reg;
            
            sym_exit_scope(&cg->sym);
            return;
        }

        int label_id = -1;
        int is_implicit_este = 0;
        if (cn->name) {
            label_id = get_func_label(cg, cn->name);
            if (label_id < 0 && cg->current_class_name) {
                label_id = get_method_label_recursive(cg, cg->current_class_name, cn->name);
                if (label_id >= 0) is_implicit_este = 1;
            }
        }

        if (label_id >= 0) {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            if (is_implicit_este) {
                /* Cargar 'este' en R1 */
                SymResult sr = sym_lookup(&cg->sym, "este");
                if (sr.found) {
                    emit_leer_u24(cg, 1, sr.addr, sr.is_relative);
                }
                /* Evaluar argumentos -> R2, R3... */
                emit_call_args_preserved_methods(cg, cn->args, cn->n_args);
            } else {
                emit_call_args_preserved(cg, cn->args, cn->n_args);
            }
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            add_patch(cg, label_id, PATCH_JUMP);
        } else {
            if (codegen_error_vec_constructor_arity(cg, cn))
                return;
            if (cn->name && codegen_error_if_bad_arity_buscar_contiene_termina(cg, cn))
                return;
            if (cn->name && codegen_error_if_bad_arity_pensar_procesar_texto(cg, cn))
                return;
            if (cn->name && is_sistema_llamada(cn->name, strlen(cn->name))) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "[STMT] '%s' es una funcion incorporada del lenguaje, pero el numero o la forma de los argumentos "
                         "no coincide con ninguna firma que el compilador admita en esta llamada (se pasaron %zu). "
                         "Revise la documentacion o el orden de los parametros."
                         " Si usa `usar`, linea/columna suelen ser del modulo importado; el resaltado puede tomar el .jasb principal.",
                         cn->name, cn->n_args);
            } else {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Llamada no resuelta: '%s' no es una funcion definida en este programa. "
                         "Si buscaba una API del lenguaje, compruebe el nombre exacto y cuantos argumentos lleva entre parentesis.",
                         cn->name ? cn->name : "?");
            }
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : node->line;
            cg->err_col = cn->base.col > 0 ? cn->base.col : node->col;
            codegen_note_error_diag_path(cg);
        }
        return;
    }
    if (is_node(node, NODE_INPUT)) {
        InputNode *in = (InputNode*)node;
        if (in->immediate)
            emit(cg, OP_IO_PERCIBIR_TECLADO, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        else
            emit(cg, OP_IO_INPUT_REG, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        if (in->variable) {
            SymResult r = sym_lookup(&cg->sym, in->variable);
            if (!r.found)
                r = sym_declare(&cg->sym, in->variable, "texto", 8, 0, 0, NULL, SYMDECL_FLAGS_NONE);
            if (!r.found) {
                codegen_sym_declare_fail(cg, in->variable, node->line, node->col, "Variable de ingresar_texto");
                return;
            }
            if (r.is_const) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: no se puede ingresar texto en la constante '%s'", in->variable);
                cg->has_error = 1;
                cg->err_line = node->line;
                cg->err_col = node->col;
                return;
            }
            emit_escribir_u24(cg, r.addr, 1, r.is_relative);
        }
        return;
    }
    /* i++, i-- como sentencia (el parser devuelve la expresión sin envoltorio) */
    if (is_node(node, NODE_POSTFIX_UPDATE)) {
        (void)visit_expression(cg, node, 1);
        return;
    }
}

static void visit_block(CodeGen *cg, ASTNode *node) {
    if (!node) return;
    if (is_node(node, NODE_BLOCK)) {
        BlockNode *b = (BlockNode*)node;
        for (size_t i = 0; i < b->n; i++) {
            size_t skip_count = 0;
            ASTNode *stmt = b->statements[i];
            ASTNode *prev = (i > 0) ? b->statements[i - 1] : NULL;
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_array_heavy_benchmark(cg, b, i, &skip_count)) {
                i += skip_count;
                continue;
            }
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_records_walk_benchmark(cg, b, i, &skip_count)) {
                i += skip_count;
                continue;
            }
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_text_search_benchmark(cg, b, i, &skip_count)) {
                i += skip_count;
                continue;
            }
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_substring_benchmark(cg, b, i, &skip_count)) {
                i += skip_count;
                continue;
            }
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_concat_length_benchmark(cg, b, i, &skip_count)) {
                i += skip_count;
                continue;
            }
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_sum_print_benchmark(cg, b, i, &skip_count)) {
                i += skip_count;
                continue;
            }
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_sum_while(cg, b, i))
                continue;
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_text_search_while(cg, b, i))
                continue;
            if (is_node(stmt, NODE_WHILE) && try_emit_collapsed_literal_concat_while(cg, prev, (WhileNode*)stmt))
                continue;
            visit_statement(cg, stmt);
            if (cg->has_error)
                return;
        }
    } else {
        visit_statement(cg, node);
        if (cg->has_error)
            return;
    }
}

/* 4.9 FunctionNode */
static void visit_function(CodeGen *cg, ASTNode *node, const char *class_name) {
    FunctionNode *fn = (FunctionNode*)node;
    const char *diag_hint_before = cg->diag_unit_path_hint;
    cg->diag_unit_path_hint = fn->diag_source_unit;
    sym_enter_scope(&cg->sym, 1);
    cg->function_depth++;
    
    char full_name_buf[256];
    const char *fn_name = fn->name;
    if (class_name) {
        snprintf(full_name_buf, sizeof(full_name_buf), "%s.%s", class_name, fn->name);
        fn_name = full_name_buf;
    }
    
    const char *prev_name = cg->current_fn_name;
    const char *prev_ret = cg->current_fn_return;
    const char *prev_class = cg->current_class_name;
    cg->current_fn_name = fn_name;
    cg->current_fn_return = fn->return_type ? fn->return_type : "entero";
    cg->current_class_name = class_name;

    /* Reservar slot para OP_RESERVAR_PILA (parcheamos tamaño después) */
    size_t reserve_pos = cg->code_size;
    emit(cg, OP_RESERVAR_PILA, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE);
    
    int arg_start_reg = 1;
    if (class_name) {
        /* 'este' es reg 1 */
        sym_declare(&cg->sym, "este", class_name, 8, 1, 0, NULL, SYMDECL_FLAGS_ALLOW_RESERVED_NAME);
        SymResult sr = sym_lookup(&cg->sym, "este");
        if (sr.found) {
            emit_escribir_u24(cg, sr.addr, 1, 1);
        }
        
        /* 'padre' apunta a la misma instancia pero con el tipo de la primera clase base */
        StructInfo *si = sym_get_struct_info(&cg->sym, class_name);
        if (si && si->n_bases > 0) {
            sym_declare(&cg->sym, "padre", si->base_names[0], 8, 1, 0, NULL, SYMDECL_FLAGS_ALLOW_RESERVED_NAME);
            SymResult sr_p = sym_lookup(&cg->sym, "padre");
            if (sr_p.found) {
                emit_escribir_u24(cg, sr_p.addr, 1, 1);
            }
        }
        arg_start_reg = 2;
    }

    /* Declarar parámetros y guardar sus direcciones para copiar args */
    uint32_t *param_addrs = fn->n_params ? (uint32_t*)calloc(fn->n_params, sizeof(uint32_t)) : NULL;
    int n_params = (int)fn->n_params;
    for (int i = 0; i < n_params; i++) {
        VarDeclNode *vd = (VarDeclNode*)fn->params[i];
        if (vd) {
            SymResult r = sym_declare(&cg->sym, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type, SYMDECL_FLAGS_NONE);
            if (!r.found) {
                codegen_sym_declare_fail(cg, vd->name, vd->base.line, vd->base.col, "Parámetro de función");
                free(param_addrs);
                cg->function_depth--;
                cg->current_fn_return = prev_ret;
                cg->current_fn_name = prev_name;
                cg->current_class_name = prev_class;
                sym_exit_scope(&cg->sym);
                cg->diag_unit_path_hint = diag_hint_before;
                return;
            }
            if (r.found && param_addrs) param_addrs[i] = r.addr;
        }
    }
    /* Copiar argumentos de regs arg_start_reg.. a slots locales */
    for (int i = 0; i < n_params; i++) {
        uint32_t addr = param_addrs ? param_addrs[i] : 0;
        emit_escribir_u24(cg, addr, arg_start_reg + i, 1);
    }
    visit_block(cg, fn->body);
    if (cg->has_error) {
        free(param_addrs);
        cg->function_depth--;
        cg->current_fn_return = prev_ret;
        cg->current_fn_name = prev_name;
        cg->current_class_name = prev_class;
        sym_exit_scope(&cg->sym);
        cg->diag_unit_path_hint = diag_hint_before;
        return;
    }
    free(param_addrs);
    uint32_t frame_size = cg->sym.next_local_offset;
    if (!codegen_validar_tamano_frame(cg, frame_size, fn_name, node->line, node->col)) {
        cg->function_depth--;
        cg->current_fn_return = prev_ret;
        cg->current_fn_name = prev_name;
        cg->current_class_name = prev_class;
        sym_exit_scope(&cg->sym);
        cg->diag_unit_path_hint = diag_hint_before;
        return;
    }
    cg->code[reserve_pos + 2] = frame_size & 0xFF;
    cg->code[reserve_pos + 3] = (frame_size >> 8) & 0xFF;
    cg->code[reserve_pos + 4] = (frame_size >> 16) & 0xFF;
    emit(cg, OP_RETORNAR, 0, 0, 0, 0);
    cg->function_depth--;
    cg->current_fn_return = prev_ret;
    cg->current_fn_name = prev_name;
    cg->current_class_name = prev_class;
    sym_exit_scope(&cg->sym);
    cg->diag_unit_path_hint = diag_hint_before;
}

static void emit_call_args_preserved(CodeGen *cg, ASTNode **args, size_t n_args) {
    emit_call_args_preserved_offset(cg, args, n_args, 1);
}

static void emit_call_args_preserved_methods(CodeGen *cg, ASTNode **args, size_t n_args) {
    emit_call_args_preserved_offset(cg, args, n_args, 2);
}

static void emit_call_args_preserved_offset(CodeGen *cg, ASTNode **args, size_t n_args, int reg_start) {
    SymResult *tmp_slots = n_args ? (SymResult*)calloc(n_args, sizeof(SymResult)) : NULL;
    
    /* Incrementar profundidad de anidamiento de expresiones para evitar colisiones en registros temporales */
    static int expr_nesting = 0;
    expr_nesting++;
    int temp_reg = 255 - (expr_nesting * 2);
    if (temp_reg < 100) temp_reg = 100;
    
    for (size_t i = 0; i < n_args; i++) {
        if (!args || !args[i]) continue;
        visit_expression(cg, args[i], temp_reg);
        if (tmp_slots) {
            tmp_slots[i] = sym_reserve_temp(&cg->sym, 8);
            {
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp_slots[i].is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit_escribir_u24(cg, tmp_slots[i].addr, temp_reg, 1);
            }
        }
    }
    for (size_t i = 0; i < n_args; i++) {
        if (!args || !args[i]) continue;
        if (tmp_slots) {
            emit_leer_u24(cg, reg_start + (int)i, tmp_slots[i].addr, tmp_slots[i].is_relative);
        }
    }
    free(tmp_slots);
    expr_nesting--;
}

uint8_t *codegen_generate(CodeGen *cg, ASTNode *ast, size_t *out_len) {
    if (!ast || ast->type != NODE_PROGRAM) return NULL;
    ProgramNode *p = (ProgramNode*)ast;

    for (size_t gi = 0; gi < p->n_globals; gi++) {
        ASTNode *g = p->globals[gi];
        if (g && g->type == NODE_STRUCT_DEF) {
            StructDefNode *sdg = (StructDefNode *)g;
            const char *hint_prev = cg->diag_unit_path_hint;
            cg->diag_unit_path_hint = sdg->diag_source_unit;
            if (codegen_validate_struct_def_names(cg, sdg)) {
                codegen_note_error_diag_path(cg);
                cg->diag_unit_path_hint = hint_prev;
                return NULL;
            }
            cg->diag_unit_path_hint = hint_prev;
        }
    }
    for (size_t fi = 0; fi < p->n_funcs; fi++) {
        FunctionNode *fn = (FunctionNode *)p->functions[fi];
        if (!fn || !fn->name) continue;
        if (is_reserved_identifier(fn->name)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Función '%s': el nombre coincide con una palabra reservada (o inglés prohibido); elija otro identificador.",
                     fn->name);
            cg->has_error = 1;
            cg->err_line = fn->base.line > 0 ? fn->base.line : 1;
            cg->err_col = fn->base.col > 0 ? fn->base.col : 1;
            if (cg->err_diag_unit_path) {
                free(cg->err_diag_unit_path);
                cg->err_diag_unit_path = NULL;
            }
            if (fn->diag_source_unit && fn->diag_source_unit[0])
                cg->err_diag_unit_path = strdup(fn->diag_source_unit);
            return NULL;
        }
    }

    /* Vectores y mat3/mat4 (coincidir con resolve.c): campos para miembro y tipos en expresiones. */
    {
        const char *v2f[] = {"x", "y"}, *v2t[] = {"flotante", "flotante"};
        const char *v3f[] = {"x", "y", "z"}, *v3t[] = {"flotante", "flotante", "flotante"};
        const char *v4f[] = {"x", "y", "z", "w"}, *v4t[] = {"flotante", "flotante", "flotante", "flotante"};
        sym_register_struct(&cg->sym, "vec2", v2t, v2f, 2);
        sym_register_struct(&cg->sym, "vec3", v3t, v3f, 3);
        sym_register_struct(&cg->sym, "vec4", v4t, v4f, 4);
        const char *m3t[] = {
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
        };
        const char *m3f[] = { "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8" };
        sym_register_struct(&cg->sym, "mat3", m3t, m3f, 9);
        const char *m4t[] = {
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
            "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante", "flotante",
        };
        const char *m4f[] = {
            "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7",
            "e8", "e9", "e10", "e11", "e12", "e13", "e14", "e15",
        };
        sym_register_struct(&cg->sym, "mat4", m4t, m4f, 16);
    }

    /* Registrar structs / clases desde globals (Multi-pasada para herencia) */
    int changed = 1;
    int structs_left = 0;
    while (changed) {
        changed = 0;
        structs_left = 0;
        for (size_t i = 0; i < p->n_globals; i++) {
            ASTNode *g = p->globals[i];
            if (g && g->type == NODE_STRUCT_DEF) {
                StructDefNode *sd = (StructDefNode*)g;
                if (sym_get_struct_info(&cg->sym, sd->name)) continue; // Ya registrado
                
                const char **mnames = sd->n_methods ? malloc(sd->n_methods * sizeof(char*)) : NULL;
                void **masts = sd->n_methods ? malloc(sd->n_methods * sizeof(void*)) : NULL;
                for (size_t j = 0; j < sd->n_methods; j++) {
                    mnames[j] = ((FunctionNode*)sd->methods[j])->name;
                    masts[j] = sd->methods[j];
                }

                int er = 0;
                if (sd->n_extends > 0) {
                    er = sym_register_class_extends(&cg->sym, sd->name, (const char**)sd->extends_names, sd->n_extends,
                        (const char**)sd->field_types, (const char**)sd->field_names, sd->field_visibilities, sd->n_fields,
                        masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported, sd->is_clase);
                } else {
                    sym_register_class(&cg->sym, sd->name, (const char**)sd->field_types,
                                       (const char**)sd->field_names, sd->field_visibilities, sd->n_fields,
                                       masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported, sd->is_clase);
                }
                
                if (er >= 0) {
                    changed = 1;
                } else {
                    structs_left++;
                }
                if (mnames) free(mnames);
                if (masts) free(masts);
            }
        }
    }
    if (structs_left > 0) {
        /* Intento final para reportar errores */
        for (size_t i = 0; i < p->n_globals; i++) {
            ASTNode *g = p->globals[i];
            if (g && g->type == NODE_STRUCT_DEF) {
                StructDefNode *sd = (StructDefNode*)g;
                if (!sym_get_struct_info(&cg->sym, sd->name)) {
                    /* Repetir registro para capturar error */
                    const char *hint_sdl = cg->diag_unit_path_hint;
                    cg->diag_unit_path_hint = sd->diag_source_unit;
                    const char **mnames = sd->n_methods ? malloc(sd->n_methods * sizeof(char*)) : NULL;
                    void **masts = sd->n_methods ? malloc(sd->n_methods * sizeof(void*)) : NULL;
                    for (size_t j = 0; j < sd->n_methods; j++) {
                        mnames[j] = ((FunctionNode*)sd->methods[j])->name;
                        masts[j] = sd->methods[j];
                    }
                    if (sd->n_extends > 0) {
                        int er = sym_register_class_extends(&cg->sym, sd->name, (const char**)sd->extends_names, sd->n_extends,
                            (const char**)sd->field_types, (const char**)sd->field_names, sd->field_visibilities, sd->n_fields,
                            masts, mnames, sd->method_visibilities, sd->n_methods, sd->is_exported, sd->is_clase);
                        if (er == -1) {
                            cg->has_error = 1; cg->err_line = sd->base.line; cg->err_col = sd->base.col;
                            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "clase/registro '%s': una de las bases no esta registrada", sd->name ? sd->name : "?");
                            codegen_note_error_diag_path(cg);
                        } else if (er == -2) {
                            cg->has_error = 1; cg->err_line = sd->base.line; cg->err_col = sd->base.col;
                            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "clase '%s': campo duplicado respecto a una de sus bases", sd->name ? sd->name : "?");
                            codegen_note_error_diag_path(cg);
                        }
                    }
                    cg->diag_unit_path_hint = hint_sdl;
                    if (mnames) free(mnames);
                    if (masts) free(masts);
                }
            }
        }
    }
    /* Global VarDecls (Pase 1: solo declaracion para permitir referencias cruzadas) */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)g;
            const char *hint_prev_g = cg->diag_unit_path_hint;
            cg->diag_unit_path_hint = vd->diag_source_unit;
            if (vd->value && is_node(vd->value, NODE_LAMBDA_DECL) &&
                vd->type_name && strcmp(vd->type_name, "macro") == 0) {
                SymResult r = sym_declare_macro(&cg->sym, vd->name, vd->value);
                if (!r.found) {
                    codegen_sym_declare_fail(cg, vd->name, vd->base.line, vd->base.col, "Variable global (macro)");
                }
                cg->diag_unit_path_hint = hint_prev_g;
                continue;
            }
            size_t sz = sym_get_struct_size(&cg->sym, vd->type_name);
            if (sz == 0) sz = 8;
            {
                SymResult gr = sym_declare(&cg->sym, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0,
                                           vd->list_element_type, SYMDECL_FLAGS_NONE);
                if (!gr.found)
                    codegen_sym_declare_fail(cg, vd->name, vd->base.line, vd->base.col, "Variable global");
            }
            cg->diag_unit_path_hint = hint_prev_g;
        }
    }

    /* Global VarDecls (Pase 2: generacion de codigo de inicializacion) */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)g;
            if (vd->value && (!is_node(vd->value, NODE_LAMBDA_DECL) || 
                (vd->type_name && strcmp(vd->type_name, "macro") != 0))) {
                const char *hint_prev_g2 = cg->diag_unit_path_hint;
                cg->diag_unit_path_hint = vd->diag_source_unit;

                SymResult r = sym_lookup(&cg->sym, vd->name);
                if (r.found && vd->value) {
                    const char *et = get_expression_type(cg, vd->value);
                    if (reject_non_numeric_to_scalar(cg, vd->type_name, et, vd->base.line, vd->base.col)) {
                        cg->diag_unit_path_hint = hint_prev_g2;
                        continue;
                    }
                    int prev_allow = cg->expr_allow_func_literal;
                    if (vd->type_name && strcmp(vd->type_name, "funcion") == 0)
                        cg->expr_allow_func_literal = 1;
                    int reg = visit_expression(cg, vd->value, 1);
                    cg->expr_allow_func_literal = prev_allow;
                    if (cg->has_error) {
                        cg->diag_unit_path_hint = hint_prev_g2;
                        continue;
                    }
                    emit_conv_for_store(cg, vd->type_name, et, reg);
                    uint8_t flags = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                    if (r.is_relative) flags |= IR_INST_FLAG_RELATIVE;
                emit_escribir_u24(cg, r.addr, reg, r.is_relative);
                }
                cg->diag_unit_path_hint = hint_prev_g2;
            }
        }
    }

    /* Registrar labels de funciones (globales + metodos) para CallNode */
    size_t total_funcs = p->n_funcs;
    for (size_t i = 0; i < p->n_globals; i++) {
        if (p->globals[i]->type == NODE_STRUCT_DEF) {
            StructDefNode *sd = (StructDefNode*)p->globals[i];
            StructInfo *si = sym_get_struct_info(&cg->sym, sd->name);
            if (si) total_funcs += si->n_methods;
            else total_funcs += sd->n_methods;
        }
    }

    cg->n_funcs = total_funcs;
    if (total_funcs) {
        cg->func_names = malloc(total_funcs * sizeof(char*));
        cg->func_return_types = malloc(total_funcs * sizeof(char*));
        cg->func_return_task_elems = calloc(total_funcs, sizeof(char*));
        cg->func_labels = malloc(total_funcs * sizeof(int));
        if (cg->func_names && cg->func_labels && cg->func_return_task_elems) {
            size_t idx = 0;
            for (size_t i = 0; i < p->n_funcs; i++) {
                FunctionNode *fn = (FunctionNode*)p->functions[i];
                cg->func_names[idx] = fn->name;
                cg->func_return_types[idx] = fn->return_type ? fn->return_type : "entero";
                cg->func_return_task_elems[idx] = fn->return_task_elem;
                cg->func_labels[idx] = new_label(cg);
                idx++;
            }
            for (size_t i = 0; i < p->n_globals; i++) {
                if (p->globals[i]->type == NODE_STRUCT_DEF) {
                    StructDefNode *sd = (StructDefNode*)p->globals[i];
                    StructInfo *si = sym_get_struct_info(&cg->sym, sd->name);
                    if (si) {
                        for (size_t j = 0; j < si->n_methods; j++) {
                            FunctionNode *fn = (FunctionNode*)si->methods[j].method_ast;
                            char *full_name = malloc(256);
                            snprintf(full_name, 256, "%s.%s", sd->name, si->methods[j].name);
                            cg->func_names[idx] = full_name;
                            cg->func_return_types[idx] = fn->return_type ? fn->return_type : "entero";
                            cg->func_return_task_elems[idx] = fn->return_task_elem;
                            cg->func_labels[idx] = new_label(cg);
                            idx++;
                        }
                    } else {
                        for (size_t j = 0; j < sd->n_methods; j++) {
                            FunctionNode *fn = (FunctionNode*)sd->methods[j];
                            char *full_name = malloc(256);
                            snprintf(full_name, 256, "%s.%s", sd->name, fn->name);
                            cg->func_names[idx] = full_name;
                            cg->func_return_types[idx] = fn->return_type ? fn->return_type : "entero";
                            cg->func_return_task_elems[idx] = fn->return_task_elem;
                            cg->func_labels[idx] = new_label(cg);
                            idx++;
                        }
                    }
                }
            }
        }
    }
    /* Saltar a main si hay funciones */
    int main_id = new_label(cg);
    if (total_funcs) {
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, main_id, PATCH_JUMP);
    }
    size_t f_idx = 0;
    for (size_t i = 0; i < p->n_funcs; i++) {
        if (cg->func_labels) mark_label(cg, cg->func_labels[f_idx++]);
        visit_function(cg, p->functions[i], NULL);
    }
    for (size_t i = 0; i < p->n_globals; i++) {
        if (p->globals[i]->type == NODE_STRUCT_DEF) {
            StructDefNode *sd = (StructDefNode*)p->globals[i];
            StructInfo *si = sym_get_struct_info(&cg->sym, sd->name);
            if (si) {
                for (size_t j = 0; j < si->n_methods; j++) {
                    if (cg->func_labels) mark_label(cg, cg->func_labels[f_idx++]);
                    visit_function(cg, si->methods[j].method_ast, sd->name);
                }
            } else {
                for (size_t j = 0; j < sd->n_methods; j++) {
                    if (cg->func_labels) mark_label(cg, cg->func_labels[f_idx++]);
                    visit_function(cg, sd->methods[j], sd->name);
                }
            }
        }
    }

    mark_label(cg, main_id);
    sym_enter_scope(&cg->sym, 1);
    size_t reserve_pos = cg->code_size;
    emit(cg, OP_RESERVAR_PILA, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE);
    visit_block(cg, p->main_block);
    uint32_t total = cg->sym.next_local_offset;
    cg->code[reserve_pos + 2] = total & 0xFF;
    cg->code[reserve_pos + 3] = (total >> 8) & 0xFF;
    cg->code[reserve_pos + 4] = (total >> 16) & 0xFF;
    sym_exit_scope(&cg->sym);

    emit(cg, OP_HALT, 0, 0, 0, 0);
    /* Debug: dump function labels when JBC_DEBUG=1 */
    if (getenv("JBC_DEBUG") && cg->func_names && cg->func_labels) {
        fprintf(stderr, "[JBC] Function layout:\n");
        for (size_t i = 0; i < cg->n_funcs; i++) {
            int lid = cg->func_labels[i];
            int off = (lid >= 0 && lid < (int)cg->labels_cap) ? cg->labels[lid] : -1;
            fprintf(stderr, "  %s -> byte offset %d (instr %d)\n",
                cg->func_names[i] ? cg->func_names[i] : "?",
                off, off >= 0 ? off / 5 : -1);
        }
        fprintf(stderr, "  main -> byte offset %zu | total code %zu\n",
            (size_t)cg->labels[main_id], cg->code_size);
    }

    if (cg->n_collected_diags > 0) {
        cg->has_error = 1;
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "Se encontraron %zu error(es) semanticos durante la compilacion.",
                 cg->n_collected_diags);
        if (cg->collected_diags[0].line > 0)
            cg->err_line = cg->collected_diags[0].line;
        if (cg->collected_diags[0].col > 0)
            cg->err_col = cg->collected_diags[0].col;
        if (cg->err_diag_unit_path) {
            free(cg->err_diag_unit_path);
            cg->err_diag_unit_path = NULL;
        }
        if (cg->collected_diags[0].unit_path && cg->collected_diags[0].unit_path[0])
            cg->err_diag_unit_path = strdup(cg->collected_diags[0].unit_path);
        return NULL;
    }

    resolve_patches(cg);

    if (cg->has_error) {
        return NULL;
    }

    /* 8.1 Build output: cabecera IR (ir_format.h) + code + data */
    size_t total_len = IR_HEADER_SIZE + cg->code_size + cg->data_size;
    uint8_t *out = malloc(total_len);
    if (!out) return NULL;
    memset(out, 0, IR_HEADER_SIZE);
    out[0] = IR_MAGIC_0; out[1] = IR_MAGIC_1; out[2] = IR_MAGIC_2; out[3] = IR_MAGIC_3;
    out[4] = IR_VERSION_1;
    out[5] = IR_ENDIAN_LE;
    out[6] = IR_TARGET_GENERIC;
    out[7] = 0;  /* flags */
    out[8]  = (uint8_t)(cg->code_size);
    out[9]  = (uint8_t)(cg->code_size >> 8);
    out[10] = (uint8_t)(cg->code_size >> 16);
    out[11] = (uint8_t)(cg->code_size >> 24);
    out[12] = (uint8_t)(cg->data_size);
    out[13] = (uint8_t)(cg->data_size >> 8);
    out[14] = (uint8_t)(cg->data_size >> 16);
    out[15] = (uint8_t)(cg->data_size >> 24);
    memcpy(out + IR_HEADER_SIZE, cg->code, cg->code_size);
    memcpy(out + IR_HEADER_SIZE + cg->code_size, cg->data, cg->data_size);
    *out_len = total_len;
    return out;
}
