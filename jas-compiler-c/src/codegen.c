/* CodeGen - emisión de IR .jbo - Niveles 4-8
 * 8.1 Cabecera: Magic JASB, version, endianness, code_size, data_size
 * 8.2 Instrucciones: 5 bytes (opcode, flags, a, b, c)
 * 8.3 Data: strings null-terminated
 * 8.4 Labels/patches: forward refs resueltos en resolve_patches
 */

#include "codegen.h"
#include "opcodes.h"
#include "symbol_table.h"
#include "nodes.h"
#include "sistema_llamadas.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PATCH_JUMP       0
#define PATCH_SI         1
#define PATCH_TRY_ENTER  2

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
    int *func_labels;
    size_t n_funcs;
    /* Funciones externas (módulos usar) solo para tipo de retorno */
    char **ext_func_names;
    char **ext_func_return_types;
    size_t n_ext_funcs;
    int function_depth;
    /* Tipo de retorno de la funcion que se esta compilando (NULL en principal). */
    const char *current_fn_return;
    /* Nombre de la funcion en curso (para diagnosticos de retornar). */
    const char *current_fn_name;

    int macro_end_label; /* label to jump to when 'retornar' is found inside a macro block. -1 if not in macro */
    int macro_dest_reg;  /* target register for 'retornar' inside macro */
    int expr_allow_func_literal; /* 1 en argumentos y asignaciones a tipo funcion: permite nombre de funcion global como valor */
};

static int is_node(const ASTNode *n, NodeType t) {
    return n && n->type == t;
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
    return off;
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

static const char *get_expression_type(CodeGen *cg, ASTNode *node) {
    if (!node) return "entero";
    if (is_node(node, NODE_LITERAL)) return ((LiteralNode*)node)->type_name ? ((LiteralNode*)node)->type_name : "entero";
    if (is_node(node, NODE_IDENTIFIER)) return sym_lookup_type(&cg->sym, ((IdentifierNode*)node)->name) ?: "entero";
    if (is_node(node, NODE_BINARY_OP)) {
        const char *lt = get_expression_type(cg, ((BinaryOpNode*)node)->left);
        const char *rt = get_expression_type(cg, ((BinaryOpNode*)node)->right);
        if (lt && strcmp(lt, "flotante") == 0) return "flotante";
        if (rt && strcmp(rt, "flotante") == 0) return "flotante";
        if (lt && strcmp(lt, "texto") == 0) return "texto";
        if (rt && strcmp(rt, "texto") == 0) return "texto";
        if (lt && rt && strcmp(lt, rt) == 0 && (strcmp(lt, "vec2") == 0 || strcmp(lt, "vec3") == 0 || strcmp(lt, "vec4") == 0))
            return lt;
    }
    if (is_node(node, NODE_POSTFIX_UPDATE))
        return get_expression_type(cg, ((PostfixUpdateNode*)node)->target);
    if (is_node(node, NODE_INDEX_ACCESS)) {
        const char *t = get_expression_type(cg, ((IndexAccessNode*)node)->target);
        return t ? t : "entero";
    }
    if (is_node(node, NODE_TERNARY)) {
        const char *tt = get_expression_type(cg, ((TernaryNode*)node)->true_expr);
        if (tt) return tt;
        return get_expression_type(cg, ((TernaryNode*)node)->false_expr);
    }
    if (is_node(node, NODE_LIST_LITERAL)) return "lista";
    if (is_node(node, NODE_MAP_LITERAL)) return "mapa";
    if (is_node(node, NODE_LAMBDA_DECL)) return "funcion";
    if (is_node(node, NODE_CALL)) {
        CallNode *cn = (CallNode*)node;
        if (cn->callee) return "entero";
        /* Incorporados con retorno texto: deben ir ANTES del lookup como `funcion` en sym (si no, se devuelve "entero"
         * y texto+str_desde_numero vuelve a emitir OP_STR_DESDE_NUMERO sobre el id de cadena -> "5861576"). */
        if (cn->name && !cn->callee) {
            if (strcmp(cn->name, "decimal") == 0 && cn->n_args == 2) return "texto";
            if (strcmp(cn->name, "concatenar") == 0) return "texto";
            if (strcmp(cn->name, "str_desde_numero") == 0 || strcmp(cn->name, "texto_desde_numero") == 0)
                return "texto";
        }
        if (cn->name && !cn->callee) {
            SymResult vr = sym_lookup(&cg->sym, cn->name);
            const char *vty = vr.found ? sym_lookup_type(&cg->sym, cn->name) : NULL;
            if (vr.found && !vr.macro_ast && vty && strcmp(vty, "funcion") == 0)
                return "entero";
        }
        if (cn->name && (strcmp(cn->name, "vec2") == 0 && cn->n_args == 2)) return "vec2";
        if (cn->name && (strcmp(cn->name, "vec3") == 0 && cn->n_args == 3)) return "vec3";
        if (cn->name && (strcmp(cn->name, "vec4") == 0 && cn->n_args == 4)) return "vec4";
        if (cn->name && (strcmp(cn->name, "str_a_entero") == 0 || strcmp(cn->name, "convertir_entero") == 0)) return "entero";
        if (cn->name && (strcmp(cn->name, "str_a_flotante") == 0 || strcmp(cn->name, "convertir_flotante") == 0)) return "flotante";
        if (cn->name && strcmp(cn->name, "entrada_flotante") == 0) return "flotante";
        if (cn->name && (strcmp(cn->name, "lista_mapear") == 0 || strcmp(cn->name, "mem_lista_mapear") == 0 ||
                         strcmp(cn->name, "lista_filtrar") == 0 || strcmp(cn->name, "mem_lista_filtrar") == 0))
            return "lista";
        /* mem_lista_obtener / lista_obtener: mismo tipo que lista<T> si la variable lista declaro T (p. ej. texto). */
        if (cn->name && (strcmp(cn->name, "mem_lista_obtener") == 0 || strcmp(cn->name, "lista_obtener") == 0) &&
            cn->n_args >= 1 && is_node(cn->args[0], NODE_IDENTIFIER)) {
            const char *el = sym_lookup_lista_elem(&cg->sym, ((IdentifierNode *)cn->args[0])->name);
            if (el && el[0]) return el;
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
            for (size_t i = 0; i < cg->n_funcs; i++)
                if (cg->func_names[i] && strcmp(cg->func_names[i], cn->name) == 0)
                    return cg->func_return_types[i] ? cg->func_return_types[i] : "entero";
        }
        if (cn->name && cg->ext_func_return_types) {
            for (size_t i = 0; i < cg->n_ext_funcs; i++)
                if (cg->ext_func_names[i] && strcmp(cg->ext_func_names[i], cn->name) == 0)
                    return cg->ext_func_return_types[i] ? cg->ext_func_return_types[i] : "entero";
        }
        return "entero";
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
        return "entero";
    }
    return "entero";
}

/* Llamadas sistema que ya dejan un id de texto en registro; no aplicar OP_STR_DESDE_NUMERO otra vez en texto+... */
static int expr_already_yields_text_string_id(ASTNode *node) {
    if (!node || !is_node(node, NODE_CALL)) return 0;
    CallNode *cn = (CallNode *)node;
    if (!cn->name || cn->callee) return 0;
    const char *n = cn->name;
    if (strcmp(n, "str_desde_numero") == 0 || strcmp(n, "texto_desde_numero") == 0) return 1;
    if (strcmp(n, "concatenar") == 0) return 1;
    if (strcmp(n, "decimal") == 0) return 1;
    if (strcmp(n, "minusculas") == 0 || strcmp(n, "str_minusculas") == 0) return 1;
    if (strcmp(n, "copiar_texto") == 0 || strcmp(n, "str_copiar") == 0) return 1;
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
    emit(cg, OP_MOVER, dest_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
    add_patch(cg, label_id, PATCH_SI);
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
    emit(cg, OP_SI, CG_STRUCT_COND_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
    add_patch(cg, lbl_indef, PATCH_SI);
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
    emit(cg, OP_SI, CG_STRUCT_COND_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
    add_patch(cg, lbl_indef, PATCH_SI);
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
                emit(cg, OP_SI, CG_STRUCT_COND_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
                add_patch(cg, lbl_indef, PATCH_SI);
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
    if (cg->has_error) return;
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
                emit(cg, OP_SI, CG_STRUCT_COND_TMP, 0, 0, IR_INST_FLAG_A_REGISTER);
                add_patch(cg, lbl_indef, PATCH_SI);
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
            int is_int = ft && (strcmp(ft, "entero") == 0 || strcmp(ft, "bool") == 0 || strcmp(ft, "u32") == 0 ||
                                strcmp(ft, "u8") == 0 || strcmp(ft, "byte") == 0 || strcmp(ft, "u64") == 0);
            emit(cg, OP_STR_DESDE_NUMERO, frag, CG_STRUCT_VAL_TMP, is_int ? 1 : 0,
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
    for (size_t i = 0; i < cg->n_funcs; i++)
        if (cg->func_names[i] && strcmp(cg->func_names[i], name) == 0)
            return cg->func_labels[i];
    return -1;
}

static const char *get_member_chain_type(CodeGen *cg, ASTNode *node) {
    if (is_node(node, NODE_IDENTIFIER))
        return sym_lookup_type(&cg->sym, ((IdentifierNode*)node)->name);
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        const char *base_type = get_member_chain_type(cg, man->target);
        if (!base_type) return NULL;
        size_t off = 0;
        const char *ft = NULL;
        size_t fsz = 0;
        if (sym_get_struct_field(&cg->sym, base_type, man->member, &off, &ft, &fsz))
            return ft;
    }
    return NULL;
}

/* Asignacion o lectura: campo inexistente, tipo base no registro, o cadena .x sin tipo. */
static void codegen_error_struct_member_access(CodeGen *cg, MemberAccessNode *man, const char *contexto) {
    if (!man) return;
    const char *mb = man->member ? man->member : "?";
    const char *base_t = get_member_chain_type(cg, man->target);
    cg->has_error = 1;
    cg->err_line = man->base.line > 0 ? man->base.line : 1;
    cg->err_col = man->base.col > 0 ? man->base.col : 1;
    if (!base_t) {
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "Error en %s: no se conoce el tipo de la expresion a la izquierda de '.%s'.",
                 contexto ? contexto : "acceso", mb);
        return;
    }
    if (sym_get_struct_size(&cg->sym, base_t) == 0) {
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "Error en %s: el tipo '%s' no es un registro (definido con registro ... fin_registro); no tiene el campo '.%s'.",
                 contexto ? contexto : "acceso", base_t, mb);
        return;
    }
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Error en %s: el registro '%s' no declara el campo '%s' (revise el bloque registro o el nombre).",
             contexto ? contexto : "acceso", base_t, mb);
}

static MemberAddrResult get_member_address(CodeGen *cg, ASTNode *node, int dest_reg) {
    MemberAddrResult r = {0, 0, 0, dest_reg};
    if (is_node(node, NODE_IDENTIFIER)) {
        const char *name = ((IdentifierNode*)node)->name;
        SymResult sr = sym_lookup(&cg->sym, name);
        if (!sr.found) {
            /* Check if it's a known function to provide a better error, or just fail */
            if (get_func_label(cg, name) >= 0) {
                /* Es una función real, no una variable ni un macro. */
            } else {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' no declarada antes de su uso", name);
                cg->has_error = 1;
                cg->err_line = ((IdentifierNode*)node)->line;
                cg->err_col = ((IdentifierNode*)node)->col;
            }
            return r; // return empty struct
        }
        r.addr = sr.addr;
        r.is_relative = sr.is_relative;
        r.in_reg = 0;
        return r;
    }
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        MemberAddrResult base = get_member_address(cg, man->target, dest_reg);
        const char *base_type = get_member_chain_type(cg, man->target);
        if (!base_type) return r;
        size_t off = 0;
        const char *ft = NULL;
        size_t fsz = 0;
        if (!sym_get_struct_field(&cg->sym, base_type, man->member, &off, &ft, &fsz))
            return r;
        if (base.in_reg) {
            emit(cg, OP_SUMAR, dest_reg, base.reg, (uint8_t)(off & 0xFF), IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            r.in_reg = 1;
            r.reg = dest_reg;
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
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "'%s' requiere un argumento (%s). Esta llamada no tiene argumentos.",
             nombre_sistema ? nombre_sistema : "?",
             que_pide ? que_pide : "vea la documentacion");
    cg->has_error = 1;
    cg->err_line = line > 0 ? line : 1;
    cg->err_col = col > 0 ? col : 1;
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
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "'%s' es el constructor del tipo vector (no una funcion global). Requiere exactamente %d argumentos numericos; se recibieron %zu. Ejemplo: %s.",
             cn->name, need, cn->n_args, ej);
    cg->has_error = 1;
    cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
    cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
    return 1;
}

static int codegen_error_vector_sistema_arity(CodeGen *cg, CallNode *cn, int need, const char *uso) {
    if ((size_t)need == cn->n_args) return 0;
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Llamada incorrecta a '%s': se esperaban %d argumento(s) y se recibieron %zu. Uso: %s.",
             cn->name ? cn->name : "?", need, cn->n_args, uso);
    cg->has_error = 1;
    cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
    cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
    return 1;
}

static void codegen_error_vector_sistema_bad_identifiers(CodeGen *cg, CallNode *cn, const char *name, const char *uso) {
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Llamada incorrecta a '%s': los argumentos deben ser identificadores (variables vector), no expresiones compuestas. Uso: %s.",
             name ? name : "?", uso);
    cg->has_error = 1;
    cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
    cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
}

/* mat3/mat4 builtins: mismos requisitos de identificadores que vec*, distinto texto. */
static void codegen_error_mat_sistema_bad_identifiers(CodeGen *cg, CallNode *cn, const char *uso) {
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "Llamada incorrecta a '%s': los argumentos deben ser identificadores de variable (matriz o vector segun la operacion), no expresiones compuestas. Uso: %s.",
             cn->name ? cn->name : "?", uso);
    cg->has_error = 1;
    cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
    cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
}

/* mat*_mul_vec*: el vector suele escribirse como vecN(...); mensaje mas claro que "expresiones compuestas". */
static void codegen_error_mat_mul_vec_arg_not_identifier(CodeGen *cg, CallNode *cn, int which_arg, ASTNode *arg,
                                                         const char *ctor_name, const char *uso) {
    if (arg && is_node(arg, NODE_CALL)) {
        CallNode *ac = (CallNode *)arg;
        if (ac->name && strcmp(ac->name, ctor_name) == 0) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Llamada incorrecta a '%s': el argumento %d debe ser el nombre de una variable %s, no %s(...) "
                     "(constructor del tipo; la VM necesita la direccion del vector ya almacenado). "
                     "Asigne antes a una variable y pase el identificador. Uso: %s.",
                     cn->name ? cn->name : "?", which_arg, ctor_name, ctor_name, uso);
            cg->has_error = 1;
            cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
            cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
            return;
        }
    }
    codegen_error_mat_sistema_bad_identifiers(cg, cn, uso);
}

/* Aridad incorrecta en APIs de lista incorporadas: evita el mensaje generico "no existe la funcion". */
static void codegen_error_sistema_lista_arity(CodeGen *cg, const CallNode *cn, const char *nombre,
                                              size_t n_recibidos, size_t n_requeridos,
                                              const char *desc_args, const char *ejemplo) {
    snprintf(cg->last_error, CODEGEN_ERROR_MAX,
             "'%s' es una llamada incorporada del lenguaje: requiere %zu argumento(s) (%s), "
             "pero esta llamada tiene %zu. Ejemplo: %s",
             nombre ? nombre : "?", n_requeridos, desc_args ? desc_args : "", n_recibidos,
             ejemplo ? ejemplo : "(ver documentacion)");
    cg->has_error = 1;
    cg->err_line = cn->base.line;
    cg->err_col = cn->base.col;
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
    if (consejo && consejo[0])
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "%s'%s' es una funcion incorporada: hace falta exactamente %zu (%s); "
                 "aqui hay %zu. Ejemplo: %s %s",
                 balance, nm, n_requeridos, desc_args ? desc_args : "", cn->n_args,
                 ejemplo ? ejemplo : "()", consejo);
    else
        snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                 "%s'%s' es una funcion incorporada: hace falta exactamente %zu (%s); "
                 "aqui hay %zu. Ejemplo: %s",
                 balance, nm, n_requeridos, desc_args ? desc_args : "", cn->n_args,
                 ejemplo ? ejemplo : "()");
    cg->has_error = 1;
    cg->err_line = cn->base.line > 0 ? cn->base.line : 1;
    cg->err_col = cn->base.col > 0 ? cn->base.col : 1;
}

/* Misma convención que mem_lista_agregar: temporales y registros 1, 2. */
static void codegen_emit_mem_lista_agregar_from_regs(CodeGen *cg, uint8_t list_reg, uint8_t val_reg) {
    SymResult ag_tmp = sym_reserve_temp(&cg->sym, 8);
    uint8_t fl_agw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
    if (ag_tmp.is_relative) fl_agw |= IR_INST_FLAG_RELATIVE;
    emit(cg, OP_ESCRIBIR, ag_tmp.addr & 0xFF, list_reg, (ag_tmp.addr >> 8) & 0xFF, fl_agw);
    uint8_t fl_agr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
    if (ag_tmp.is_relative) fl_agr |= IR_INST_FLAG_RELATIVE;
    emit(cg, OP_LEER, 1, ag_tmp.addr & 0xFF, (ag_tmp.addr >> 8) & 0xFF, fl_agr);
    emit(cg, OP_MOVER, 2, val_reg, 0, IR_INST_FLAG_B_REGISTER);
    emit(cg, OP_MEM_LISTA_AGREGAR, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
}

/* --- Nivel 6: visit_call_sistema --- */
static int visit_call_sistema(CodeGen *cg, CallNode *cn, int dest_reg) {
    const char *name = cn->name;
    if (!name) return 0;
    if (!is_sistema_llamada(name, strlen(name))) return 0;

#define ARG0 (cn->n_args > 0 ? cn->args[0] : NULL)
#define ARG1 (cn->n_args > 1 ? cn->args[1] : NULL)
#define ARG2 (cn->n_args > 2 ? cn->args[2] : NULL)
#define ARG3 (cn->n_args > 3 ? cn->args[3] : NULL)

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
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_STR_CONCATENAR_REG, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
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
    if (strcmp(name, "dividir") == 0 || strcmp(name, "dividir_texto") == 0) {
        if (cn->n_args != 2) {
            int es_dt = (strcmp(name, "dividir_texto") == 0);
            const char *ej = es_dt
                ? "dividir_texto(\"uno,dos\", \",\")"
                : "dividir(\"x|y\", \"|\")";
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
    if (strcmp(name, "buscar_en_texto") == 0 || strcmp(name, "contiene_texto") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_CONTIENE_TEXTO_REG, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "termina_con") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_TERMINA_CON_REG, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "extraer_subtexto") == 0) {
        if (!ARG0 || !ARG1) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        if (ARG2) visit_expression(cg, ARG2, 3);
        else emit(cg, OP_MOVER, 3, 0xFF, 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_STR_SUBTEXTO, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "extraer_antes_de") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_STR_EXTRAER_ANTES_REG, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "extraer_despues_de") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_STR_EXTRAER_DESPUES_REG, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
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
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_COPIAR_TEXTO, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 2, 0, IR_INST_FLAG_B_REGISTER);
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
    if (strcmp(name, "str_extraer_caracter") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        emit(cg, OP_STR_EXTRAER_CARACTER, dest_reg, dest_reg, dest_reg + 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "codigo_caracter") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_STR_CODIGO_CARACTER, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "caracter_a_texto") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_STR_DESDE_CODIGO, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "byte_a_caracter") == 0 || strcmp(name, "caracter_a_byte") == 0) {
        if (!ARG0) return 0;
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
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_SIN, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "cos") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "angulo en radianes (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_COS, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tan") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "angulo en radianes (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_TAN, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "atan2") == 0 || strcmp(name, "arcotangente2") == 0) {
        if (codegen_error_vector_sistema_arity(cg, cn, 2, "atan2(y, x) o arcotangente2(y, x) — radianes, y primero"))
            return 1;
        visit_expression(cg, ARG0, 1);  /* y */
        visit_expression(cg, ARG1, 2);  /* x */
        emit(cg, OP_ATAN2, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "exp") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "exponente (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_EXP, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "log") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "argumento > 0 (flotante, logaritmo natural)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_LOG, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "log10") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "argumento > 0 (flotante)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_LOG10, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
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
        emit(cg, OP_LEER, 1, sr.addr & 0xFF, (sr.addr >> 8) & 0xFF, fl);
        emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 1; i < n; i++) {
            uint32_t a = sr.addr + (uint32_t)(i * 8);
            emit(cg, OP_LEER, 2, a & 0xFF, (a >> 8) & 0xFF, fl);
            emit(cg, OP_MULTIPLICAR_FLT, 2, 2, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_RAIZ, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
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
        emit(cg, OP_LEER, 1, src.addr & 0xFF, (src.addr >> 8) & 0xFF, flS);
        emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 1; i < n; i++) {
            uint32_t a = src.addr + (uint32_t)(i * 8);
            emit(cg, OP_LEER, 2, a & 0xFF, (a >> 8) & 0xFF, flS);
            emit(cg, OP_MULTIPLICAR_FLT, 2, 2, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        emit(cg, OP_RAIZ, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 0; i < n; i++) {
            uint32_t a = src.addr + (uint32_t)(i * 8);
            emit(cg, OP_LEER, 1, a & 0xFF, (a >> 8) & 0xFF, flS);
            emit(cg, OP_DIVIDIR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            a = dst.addr + (uint32_t)(i * 8);
            /* OP_ESCRIBIR: B es registro fuente; no mezclar B_IMMEDIATE de flD (romperia el store). */
            uint8_t flW = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | IR_INST_FLAG_B_REGISTER;
            if (dst.is_relative) flW |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, a & 0xFF, 1, (a >> 8) & 0xFF, flW);
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
        emit(cg, OP_LEER, 1, sa.addr & 0xFF, (sa.addr >> 8) & 0xFF, flA);
        emit(cg, OP_LEER, 2, sb.addr & 0xFF, (sb.addr >> 8) & 0xFF, flB);
        emit(cg, OP_MULTIPLICAR_FLT, acc, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (int i = 1; i < n; i++) {
            uint32_t oa = sa.addr + (uint32_t)(i * 8), ob = sb.addr + (uint32_t)(i * 8);
            emit(cg, OP_LEER, 1, oa & 0xFF, (oa >> 8) & 0xFF, flA);
            emit(cg, OP_LEER, 2, ob & 0xFF, (ob >> 8) & 0xFF, flB);
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
            emit(cg, OP_LEER, 1, (uint8_t)(aj & 0xFF), (uint8_t)((aj >> 8) & 0xFF), flA);
            emit(cg, OP_LEER, 2, (uint8_t)(bk & 0xFF), (uint8_t)((bk >> 8) & 0xFF), flB);
            emit(cg, OP_MULTIPLICAR_FLT, 3, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_LEER, 1, (uint8_t)(ak & 0xFF), (uint8_t)((ak >> 8) & 0xFF), flA);
            emit(cg, OP_LEER, 2, (uint8_t)(bj & 0xFF), (uint8_t)((bj >> 8) & 0xFF), flB);
            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_RESTAR_FLT, 1, 3, 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            uint32_t d = dst.addr + (uint32_t)(i * 8);
            uint8_t flW = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE | IR_INST_FLAG_B_REGISTER;
            if (dst.is_relative) flW |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, (uint8_t)(d & 0xFF), 1, (uint8_t)((d >> 8) & 0xFF), flW);
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
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_APRENDER_PESO_REG, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "reforzar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        int mag = 10;
        if (cn->n_args >= 2 && ARG1 && is_node(ARG1, NODE_LITERAL) &&
            ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG1)->value.i;
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            mag = (int)v;
        }
        emit(cg, OP_MEM_REFORZAR_CONCEPTO, 1, 0, (uint8_t)mag,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "penalizar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        int mag = 10;
        if (cn->n_args >= 2 && ARG1 && is_node(ARG1, NODE_LITERAL) &&
            ((LiteralNode*)ARG1)->type_name && strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG1)->value.i;
            if (v < 1) v = 1;
            if (v > 100) v = 100;
            mag = (int)v;
        }
        emit(cg, OP_MEM_PENALIZAR_CONCEPTO, 1, 0, (uint8_t)mag,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "decae_conexiones") == 0 || strcmp(name, "decaer_conexiones") == 0) {
        emit(cg, OP_MEM_DECAE_CONEXIONES, (uint8_t)dest_reg, 5, 10,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "consolidar_memoria") == 0 || strcmp(name, "dormir") == 0 ||
        strcmp(name, "consolidar") == 0) {
        emit(cg, OP_MEM_CONSOLIDAR_SUENO, (uint8_t)dest_reg, 5, 10,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "olvidar_debiles") == 0) {
        emit(cg, OP_MEM_OLVIDAR_DEBILES, (uint8_t)dest_reg, 0, 10,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
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
        if (!ARG0) return 0;
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
        if (!ARG0) return 0;
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
            strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t k = ((LiteralNode*)ARG0)->value.i;
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            emit(cg, OP_PERCEPCION_ANTERIOR, (uint8_t)dest_reg, (uint8_t)k, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 1);
            emit(cg, OP_PERCEPCION_ANTERIOR, (uint8_t)dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "percepcion_recientes") == 0) {
        emit(cg, OP_PERCEPCION_LISTA, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
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
        if (!ARG0) return 0;
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
            strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t k = ((LiteralNode*)ARG0)->value.i;
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            emit(cg, OP_RASTRO_ACTIVACION_OBTENER, (uint8_t)dest_reg, (uint8_t)k, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 1);
            emit(cg, OP_RASTRO_ACTIVACION_OBTENER, (uint8_t)dest_reg, 1, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "rastro_activacion_peso") == 0) {
        if (!ARG0) return 0;
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name &&
            strcmp(((LiteralNode*)ARG0)->type_name, "entero") == 0) {
            int64_t k = ((LiteralNode*)ARG0)->value.i;
            if (k < 0) k = 0;
            if (k > 255) k = 255;
            emit(cg, OP_RASTRO_ACTIVACION_PESO, (uint8_t)dest_reg, (uint8_t)k, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 1);
            emit(cg, OP_RASTRO_ACTIVACION_PESO, (uint8_t)dest_reg, 1, 0,
                 IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "rastro_activacion_lista") == 0 || strcmp(name, "rastro_activacion_recientes") == 0) {
        emit(cg, OP_RASTRO_ACTIVACION_LISTA, (uint8_t)dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "propagar_activacion") == 0 || strcmp(name, "propagar_activacion_de") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 2);
        uint32_t tipo = 0, K = 8, prof = 3;
        if (strcmp(name, "propagar_activacion_de") == 0 && cn->n_args >= 2 && ARG1 &&
            is_node(ARG1, NODE_LITERAL) && ((LiteralNode*)ARG1)->type_name &&
            strcmp(((LiteralNode*)ARG1)->type_name, "entero") == 0) {
            int64_t v = ((LiteralNode*)ARG1)->value.i;
            if (v >= 0 && v <= 10) tipo = (uint32_t)v;
        }
        uint32_t pack = tipo | (K << 8) | (prof << 16);
        emit(cg, OP_MOVER, 3, (uint8_t)(pack & 0xFFu), (uint8_t)((pack >> 8) & 0xFFu),
             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        {
            uint8_t hi = (uint8_t)((pack >> 16) & 0xFFu);
            if (hi != 0) {
                emit(cg, OP_MOVER, 4, hi, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_BIT_SHL, 4, 4, 16, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
                emit(cg, OP_SUMAR, 3, 3, 4, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            }
        }
        emit(cg, OP_MEM_PROPAGAR_ACTIVACION, (uint8_t)dest_reg, 2, 3,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "elegir_por_peso") == 0 || strcmp(name, "elegir_por_peso_segun") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 2);
        visit_expression(cg, ARG1, 1);
        emit(cg, OP_MEM_ELEGIR_POR_PESO_IDX, (uint8_t)dest_reg, 1, 2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "elegir_por_peso_id") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 2);
        visit_expression(cg, ARG1, 1);
        emit(cg, OP_MEM_ELEGIR_POR_PESO_ID, (uint8_t)dest_reg, 1, 2,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "elegir_por_peso_semilla") == 0 || strcmp(name, "elegir_por_peso_seed") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_ELEGIR_POR_PESO_SEMILLA, (uint8_t)dest_reg, 1, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        SymResult r = sym_get_or_create(&cg->sym, "resultado", NULL);
        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 1, (r.addr >> 8) & 0xFF, fl);
        return 1;
    }
    if (strcmp(name, "buscar") == 0) {
        if (!ARG0) return 0;
        if (is_node(ARG0, NODE_LITERAL) && ((LiteralNode*)ARG0)->type_name && strcmp(((LiteralNode*)ARG0)->type_name, "texto") == 0) {
            size_t off = add_string(cg, ((LiteralNode*)ARG0)->value.str ? ((LiteralNode*)ARG0)->value.str : "");
            emit(cg, OP_LOAD_STR_HASH, 1, off & 0xFF, (off >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            visit_expression(cg, ARG0, 1);
        }
        emit(cg, OP_MEM_OBTENER_VALOR, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        SymResult r = sym_get_or_create(&cg->sym, "resultado", NULL);
        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 1, (r.addr >> 8) & 0xFF, fl);
        return 1;
    }
    if (strcmp(name, "procesar_texto") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, 0xDE, dest_reg, dest_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "obtener_relacionados") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_OBTENER_RELACIONADOS, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "es_variable_sistema") == 0) {
        if (!ARG0) return 0;
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
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, dest_reg);
        emit(cg, OP_MEM_REGISTRAR_PATRON, (uint8_t)dest_reg, (uint8_t)dest_reg, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "asociar_secuencia") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_STR_ASOCIAR_SECUENCIA, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar_siguiente") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 10);
        emit(cg, OP_MEM_PENSAR_SIGUIENTE, (uint8_t)dest_reg, 10, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "pensar_anterior") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 10);
        emit(cg, OP_MEM_PENSAR_ANTERIOR, (uint8_t)dest_reg, 10, 0,
             IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "buscar_asociados") == 0 || strcmp(name, "asociados_de") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 10);
        if (ARG1) visit_expression(cg, ARG1, 11);
        else emit(cg, OP_MOVER, 11, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_BUSCAR_ASOCIADOS, (uint8_t)dest_reg, 10, 11, 0);
        return 1;
    }
    if (strcmp(name, "buscar_asociados_lista") == 0 || strcmp(name, "asociados_lista_de") == 0) {
        if (cn->n_args < 2) return 0;
        /* Regs 10–15: no pisar dest_reg (suele ser 1) ni args en 2–4 */
        visit_expression(cg, ARG0, 10);
        visit_expression(cg, ARG1, 11);
        if (cn->n_args >= 3 && ARG2) visit_expression(cg, ARG2, 12);
        else emit(cg, OP_MOVER, 12, 16, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MOVER, 13, 8, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_BIT_SHL, 14, 12, 13, 0);
        emit(cg, OP_SUMAR, 15, 11, 14, 0);
        emit(cg, OP_MEM_BUSCAR_ASOCIADOS_LISTA, (uint8_t)dest_reg, 10, 15, 0);
        return 1;
    }
    if (strcmp(name, "comparar_patrones") == 0) {
        if (cn->n_args < 2) return 0;
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
        return 1;
    }
    if (strcmp(name, "ahora") == 0 || strcmp(name, "obtener_ahora") == 0 || strcmp(name, "obtener_timestamp") == 0) {
        emit(cg, OP_SYS_TIMESTAMP, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "diferencia_en_segundos") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_RESTAR, dest_reg, dest_reg + 2, dest_reg + 1, 0);
        return 1;
    }

    /* 6.6 Archivos */
    if (strcmp(name, "abrir_archivo") == 0 || strcmp(name, "fs_abrir") == 0) {
        if (!ARG0) return 0;
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
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        if (ARG1) { visit_expression(cg, ARG1, 2); emit(cg, OP_FS_ESCRIBIR, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); }
        else emit(cg, OP_FS_ESCRIBIR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "fin_archivo") == 0) {
        if (ARG0) { visit_expression(cg, ARG0, dest_reg + 1); emit(cg, OP_FS_FIN_ARCHIVO, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER); }
        else emit(cg, OP_FS_FIN_ARCHIVO, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "existe_archivo") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_FS_EXISTE, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "listar_archivos") == 0 || strcmp(name, "fs_listar") == 0) {
        if (!ARG0) return 0;
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
        if (!ARG0) return 0;
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
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_FS_LEER_TEXTO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
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
            int id = ++cg->literal_counter;
            emit(cg, OP_MOVER, dest_reg, id & 0xFF, (id >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
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
        emit(cg, OP_ESCRIBIR, ag_tmp.addr & 0xFF, 1, (ag_tmp.addr >> 8) & 0xFF, fl_agw);
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
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_MEM_LISTA_OBTENER, dest_reg, dest_reg + 1, dest_reg + 2, 0);
        return 1;
    }
    if (strcmp(name, "lista_tamano") == 0 || strcmp(name, "mem_lista_tamano") == 0) {
        if (cn->n_args < 1 || !ARG0) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 1,
                "lista",
                "mem_lista_tamano(mi_lista) o lista_tamano(mi_lista)");
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_LISTA_TAMANO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
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
        emit(cg, OP_ESCRIBIR, src_tmp.addr & 0xFF, 1, (src_tmp.addr >> 8) & 0xFF, flw);
        int nid = ++cg->literal_counter;
        emit(cg, OP_MOVER, 2, nid & 0xFF, (nid >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_LISTA_CREAR, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flwo = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flwo |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, out_tmp.addr & 0xFF, 2, (out_tmp.addr >> 8) & 0xFF, flwo);
        emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        uint8_t fli = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) fli |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, idx_tmp.addr & 0xFF, 15, (idx_tmp.addr >> 8) & 0xFF, fli);
        int loop_id = new_label(cg);
        int end_id = new_label(cg);
        mark_label(cg, loop_id);
        uint8_t flr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flr |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 15, src_tmp.addr & 0xFF, (src_tmp.addr >> 8) & 0xFF, flr);
        emit(cg, OP_MEM_LISTA_TAMANO, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flri = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) flri |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_CMP_LT, 18, 17, 16, 0);
        emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 18, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, end_id, PATCH_SI);
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
        emit(cg, OP_ESCRIBIR, idx_tmp.addr & 0xFF, 17, (idx_tmp.addr >> 8) & 0xFF, fli);
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
        emit(cg, OP_ESCRIBIR, src_tmp.addr & 0xFF, 1, (src_tmp.addr >> 8) & 0xFF, flw);
        int nid = ++cg->literal_counter;
        emit(cg, OP_MOVER, 2, nid & 0xFF, (nid >> 8) & 0xFF, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_LISTA_CREAR, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flwo = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flwo |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, out_tmp.addr & 0xFF, 2, (out_tmp.addr >> 8) & 0xFF, flwo);
        emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        uint8_t fli = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) fli |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, idx_tmp.addr & 0xFF, 15, (idx_tmp.addr >> 8) & 0xFF, fli);
        int loop_id = new_label(cg);
        int end_id = new_label(cg);
        int skip_agr_id = new_label(cg);
        mark_label(cg, loop_id);
        uint8_t flr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flr |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 15, src_tmp.addr & 0xFF, (src_tmp.addr >> 8) & 0xFF, flr);
        emit(cg, OP_MEM_LISTA_TAMANO, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flri = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) flri |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_CMP_LT, 18, 17, 16, 0);
        emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 18, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, end_id, PATCH_SI);
        emit(cg, OP_MEM_LISTA_OBTENER, 19, 15, 17, 0);
        emit(cg, OP_MOVER, 1, 19, 0, IR_INST_FLAG_B_REGISTER);
        int prev2 = cg->expr_allow_func_literal;
        cg->expr_allow_func_literal = 1;
        emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        add_patch(cg, fn_lab, PATCH_JUMP);
        cg->expr_allow_func_literal = prev2;
        emit(cg, OP_CMP_EQ, 22, 1, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 22, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, skip_agr_id, PATCH_SI);
        uint8_t flro = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (out_tmp.is_relative) flro |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 20, out_tmp.addr & 0xFF, (out_tmp.addr >> 8) & 0xFF, flro);
        codegen_emit_mem_lista_agregar_from_regs(cg, 20, 19);
        mark_label(cg, skip_agr_id);
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_ESCRIBIR, idx_tmp.addr & 0xFF, 17, (idx_tmp.addr >> 8) & 0xFF, fli);
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
                "mapa, clave (entero) y valor (entero o funcion)",
                "mapa_poner(m, clave, valor)");
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg);
        visit_expression(cg, ARG1, dest_reg + 1);
        {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            visit_expression(cg, ARG2, dest_reg + 2);
            cg->expr_allow_func_literal = prev;
        }
        emit(cg, OP_MEM_MAPA_PONER, dest_reg, dest_reg + 1, dest_reg + 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mapa_obtener") == 0) {
        if (cn->n_args < 2) {
            codegen_error_sistema_lista_arity(cg, cn, name, cn->n_args, 2,
                "mapa y clave (enteros)",
                "mapa_obtener(m, clave)");
            return 1;
        }
        visit_expression(cg, ARG0, dest_reg + 1);
        visit_expression(cg, ARG1, dest_reg + 2);
        emit(cg, OP_MEM_MAPA_OBTENER, dest_reg, dest_reg + 1, dest_reg + 2, 0);
        return 1;
    }

    /* 6.9 Memoria neuronal */
    if (strcmp(name, "mem_crear") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        if (ARG1) visit_expression(cg, ARG1, 2); else emit(cg, OP_MOVER, 2, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        if (ARG2) visit_expression(cg, ARG2, 3); else emit(cg, OP_MOVER, 3, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_CREAR, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "mem_cerrar") == 0) {
        emit(cg, OP_MEM_CERRAR, 0, 0, 0, 0);
        return 1;
    }
    if (strcmp(name, "mem_asociar") == 0) {
        if (cn->n_args < 3) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        visit_expression(cg, ARG2, 3);
        emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, 1, 2, 3, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "tiene_asociacion") == 0 || strcmp(name, "mem_obtener_fuerza") == 0) {
        if (cn->n_args < 2) return 0;
        visit_expression(cg, ARG0, 1);
        visit_expression(cg, ARG1, 2);
        emit(cg, OP_MEM_OBTENER_FUERZA, dest_reg, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }

    /* 6.10 Sistema, 6.11 Conversiones */
    if (strcmp(name, "sistema_ejecutar") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_SYS_EXEC, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "sys_argc") == 0) {
        emit(cg, OP_SYS_ARGC, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "sys_argv") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, dest_reg + 1);
        emit(cg, OP_SYS_ARGV, dest_reg, dest_reg + 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_a_entero") == 0 || strcmp(name, "convertir_entero") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto: cadena a interpretar como entero", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_STR_A_ENTERO, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_a_flotante") == 0 || strcmp(name, "convertir_flotante") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "texto: cadena a interpretar como flotante", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_STR_A_FLOTANTE, dest_reg, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return 1;
    }
    if (strcmp(name, "str_desde_numero") == 0) {
        if (!ARG0) return 0;
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_STR_DESDE_NUMERO, dest_reg, 1, 1, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        return 1;
    }
    if (strcmp(name, "decimal") == 0) {
        if (cn->n_args < 2 || !ARG0 || !ARG1) return 0;
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
                visit_expression(cg, expr, 1);
                emit(cg, OP_IMPRIMIR_FLOTANTE, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            } else {
                visit_expression(cg, expr, 1);
                emit(cg, OP_IMPRIMIR_NUMERO, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
            }
        } else {
            const char *t = get_expression_type(cg, expr);
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
            int reg = visit_expression(cg, expr, 1);
            if (t && strcmp(t, "texto") == 0)
                emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else if (t && strcmp(t, "caracter") == 0) {
                emit(cg, OP_STR_DESDE_CODIGO, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            } else if (t && strcmp(t, "flotante") == 0)
                emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            else
                emit(cg, OP_IMPRIMIR_NUMERO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
        return 1;
    }
    if (strcmp(name, "imprimir_id") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "identificador de concepto (expresion)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_MEM_IMPRIMIR_ID, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        return 1;
    }
    if (strcmp(name, "imprimir_flotante") == 0) {
        if (!ARG0) {
            sistema_error_sin_argumentos(cg, name, "expresion de tipo flotante (o convertible)", cn->base.line, cn->base.col);
            return 1;
        }
        visit_expression(cg, ARG0, 1);
        emit(cg, OP_IMPRIMIR_FLOTANTE, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
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
    return 0;
}

CodeGen *codegen_create(void) {
    CodeGen *cg = calloc(1, sizeof(CodeGen));
    if (!cg) return NULL;
    sym_init(&cg->sym);
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
    free(cg->func_labels);
    for (size_t i = 0; i < cg->n_ext_funcs; i++) {
        free(cg->ext_func_names[i]);
        free(cg->ext_func_return_types[i]);
    }
    free(cg->ext_func_names);
    free(cg->ext_func_return_types);
    free(cg);
}

const char *codegen_get_error(CodeGen *cg, int *out_line, int *out_col) {
    if (!cg || !cg->has_error) return NULL;
    if (out_line) *out_line = cg->err_line;
    if (out_col) *out_col = cg->err_col;
    return cg->last_error[0] ? cg->last_error : "Error desconocido";
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
    int tmp_reg = (dest_reg == 1) ? 2 : 1;
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
                emit(cg, OP_ESCRIBIR, tmp_dest.addr & 0xFF, dest_reg, (tmp_dest.addr >> 8) & 0xFF, fl_w);
            }
            if (type_is_user_struct(cg, tchk)) {
                MemberAddrResult mbase = get_member_address(cg, expr_node, 2);
                if (cg->has_error) {
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
                    int is_int = (t && strcmp(t, "entero") == 0);
                    emit(cg, OP_STR_DESDE_NUMERO, expr_reg, expr_reg, is_int ? 1 : 0,
                         IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
                }
                if (!first) {
                    uint8_t fl_r = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                    if (tmp_dest.is_relative) fl_r |= IR_INST_FLAG_RELATIVE;
                    emit(cg, OP_LEER, dest_reg, tmp_dest.addr & 0xFF, (tmp_dest.addr >> 8) & 0xFF, fl_r);
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
                uint8_t fl_r = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp_dest.is_relative) fl_r |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_LEER, dest_reg, tmp_dest.addr & 0xFF, (tmp_dest.addr >> 8) & 0xFF, fl_r);
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
            } else if (t && strcmp(t, "lista") == 0) {
                emit_imprimir_lista_resumen(cg, expr_node);
            } else if (t && strcmp(t, "mapa") == 0) {
                emit_imprimir_mapa_resumen(cg, expr_node);
            } else {
                int reg = visit_expression(cg, expr_node, 1);
                if (t && strcmp(t, "texto") == 0)
                    emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
                else if (t && strcmp(t, "flotante") == 0)
                    emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
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
    visit_expression(cg, expr, 1);
    if (cg->has_error) return;
    emit(cg, OP_MEM_LISTA_TAMANO, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
    emit_print_cstr(cg, "[lista id=");
    emit(cg, OP_IMPRIMIR_NUMERO, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
    emit_print_cstr(cg, " tamano=");
    emit(cg, OP_IMPRIMIR_NUMERO, 2, 0, 0, IR_INST_FLAG_A_REGISTER);
    emit_print_cstr(cg, "]");
}

static void emit_imprimir_mapa_resumen(CodeGen *cg, ASTNode *expr) {
    visit_expression(cg, expr, 1);
    if (cg->has_error) return;
    emit(cg, OP_MEM_MAPA_TAMANO, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
    emit_print_cstr(cg, "[mapa id=");
    emit(cg, OP_IMPRIMIR_NUMERO, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
    emit_print_cstr(cg, " tamano=");
    emit(cg, OP_IMPRIMIR_NUMERO, 2, 0, 0, IR_INST_FLAG_A_REGISTER);
    emit_print_cstr(cg, "]");
}

/* --- 4.1 PrintNode --- */
static void emit_print(CodeGen *cg, ASTNode *expr, int stmt_line, int stmt_col) {
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
            int reg = visit_expression(cg, expr, 1);
            emit(cg, OP_STR_DESDE_CODIGO, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else if (ln->is_float) {
            int reg = visit_expression(cg, expr, 1);
            emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        } else {
            int reg = visit_expression(cg, expr, 1);
            emit(cg, OP_IMPRIMIR_NUMERO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
    } else {
        const char *t = get_expression_type(cg, expr);
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
        int reg = visit_expression(cg, expr, 1);
        if (cg->has_error) return;
        if (t && strcmp(t, "texto") == 0)
            emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        else if (t && strcmp(t, "caracter") == 0) {
            emit(cg, OP_STR_DESDE_CODIGO, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
            emit(cg, OP_IMPRIMIR_TEXTO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }         else if (t && strcmp(t, "flotante") == 0)
            emit(cg, OP_IMPRIMIR_FLOTANTE, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        else
            emit(cg, OP_IMPRIMIR_NUMERO, reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        }
    }
    size_t nl = add_string(cg, "\n");
    emit(cg, OP_IMPRIMIR_TEXTO, nl & 0xFF, (nl >> 8) & 0xFF, (nl >> 16) & 0xFF,
         IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
}

/* --- visit_expression: Nivel 5 completo --- */
static int visit_expression(CodeGen *cg, ASTNode *node, int dest_reg) {
    if (!node) return dest_reg;
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
        const int list_reg = 2;
        const int el_reg = 4;
        int id = ++cg->literal_counter;
        emit(cg, OP_MOVER, list_reg, id & 0xFF, (id >> 8) & 0xFF,
             IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_LISTA_CREAR, list_reg, list_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        for (size_t i = 0; i < lln->n; i++) {
            visit_expression(cg, lln->elements[i], el_reg);
            emit(cg, OP_MEM_LISTA_AGREGAR, list_reg, el_reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        }
        if (list_reg != dest_reg)
            emit(cg, OP_MOVER, dest_reg, list_reg, 0, IR_INST_FLAG_B_REGISTER);
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
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, skip_lbl, PATCH_JUMP);
        mark_label(cg, entry_lbl);
        sym_enter_scope(&cg->sym, 1);
        const char *prev_ret = cg->current_fn_return;
        const char *prev_name = cg->current_fn_name;
        int prev_fd = cg->function_depth;
        cg->function_depth++;
        cg->current_fn_name = "__lambda";
        {
            const char *infer_ret = get_expression_type(cg, ld->body);
            if (!infer_ret || strcmp(infer_ret, "funcion") == 0) infer_ret = "entero";
            cg->current_fn_return = infer_ret;
        }
        size_t reserve_pos = cg->code_size;
        emit(cg, OP_RESERVAR_PILA, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE);
        uint32_t param_addrs[4] = {0};
        int n_params = (int)(ld->n_params < 4 ? ld->n_params : 4);
        for (int i = 0; i < n_params; i++) {
            const char *pt = (ld->types && ld->types[i]) ? ld->types[i] : "entero";
            SymResult pr = sym_declare(&cg->sym, ld->params[i], pt, 8, 1, 0, NULL);
            if (pr.found) param_addrs[i] = pr.addr;
        }
        for (int i = 0; i < n_params; i++) {
            uint32_t addr = param_addrs[i];
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            fl |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, addr & 0xFF, 1 + i, (addr >> 8) & 0xFF, fl);
        }
        if (is_node(ld->body, NODE_BLOCK))
            visit_block(cg, ld->body);
        else
            (void)visit_expression(cg, ld->body, 1);
        uint32_t frame_size = cg->sym.next_local_offset;
        cg->code[reserve_pos + 2] = frame_size & 0xFF;
        cg->code[reserve_pos + 3] = (frame_size >> 8) & 0xFF;
        cg->code[reserve_pos + 4] = (frame_size >> 16) & 0xFF;
        emit(cg, OP_RETORNAR, 0, 0, 0, 0);
        cg->function_depth = prev_fd;
        cg->current_fn_return = prev_ret;
        cg->current_fn_name = prev_name;
        sym_exit_scope(&cg->sym);
        mark_label(cg, skip_lbl);
        emit_load_label_addr(cg, dest_reg, entry_lbl);
        return dest_reg;
    }
    if (is_node(node, NODE_IDENTIFIER)) {
        IdentifierNode *id_node = (IdentifierNode*)node;
        const char *name = id_node->name;
        SymResult r = sym_lookup(&cg->sym, name);
        if (r.found) {
            if (r.macro_ast) {
                emit(cg, OP_MOVER, dest_reg, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
                return dest_reg;
            }
            uint8_t flags = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) flags |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_LEER, dest_reg, r.addr & 0xFF, (r.addr >> 8) & 0xFF, flags);
            return dest_reg;
        }
        int fl = get_func_label(cg, name);
        if (fl >= 0) {
            if (!cg->expr_allow_func_literal) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Error: '%s' es una funcion global; como valor solo puede usarse donde se espera tipo funcion "
                         "(por ejemplo argumento de otra funcion, o `funcion f = %s`).",
                         name, name);
                cg->has_error = 1;
                cg->err_line = id_node->line;
                cg->err_col = id_node->col;
                return dest_reg;
            }
            emit_load_label_addr(cg, dest_reg, fl);
            return dest_reg;
        }
        snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' no declarada antes de su uso", name);
        cg->has_error = 1;
        cg->err_line = id_node->line;
        cg->err_col = id_node->col;
        return dest_reg;
    }
    if (is_node(node, NODE_POSTFIX_UPDATE)) {
        PostfixUpdateNode *pu = (PostfixUpdateNode*)node;
        if (!is_node(pu->target, NODE_IDENTIFIER)) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                     "Error: '++' y '--' postfijos requieren una variable simple (identificador); use 'i = i + 1' para otros casos");
            cg->has_error = 1;
            cg->err_line = pu->base.line;
            cg->err_col = pu->base.col;
            return dest_reg;
        }
        IdentifierNode *id_node = (IdentifierNode*)pu->target;
        SymResult r = sym_lookup(&cg->sym, id_node->name);
        if (!r.found) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' no declarada antes de su uso", id_node->name);
            cg->has_error = 1;
            cg->err_line = id_node->line;
            cg->err_col = id_node->col;
            return dest_reg;
        }
        (void)visit_expression(cg, pu->target, dest_reg);
        int tmp = (dest_reg == 1) ? 2 : 1;
        if (pu->delta == 1)
            emit(cg, OP_SUMAR, tmp, dest_reg, 1, IR_INST_FLAG_C_IMMEDIATE);
        else
            emit(cg, OP_RESTAR, tmp, dest_reg, 1, IR_INST_FLAG_C_IMMEDIATE);
        uint8_t wflags = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) wflags |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, r.addr & 0xFF, tmp, (r.addr >> 8) & 0xFF, wflags);
        return dest_reg;
    }
    if (is_node(node, NODE_UNARY_OP)) {
        UnaryOpNode *un = (UnaryOpNode*)node;
        int r = visit_expression(cg, un->expression, dest_reg);
        if (strcmp(un->operator, "no") == 0) {
            emit(cg, OP_CMP_EQ, dest_reg, r, 0, IR_INST_FLAG_C_IMMEDIATE);
        } else if (strcmp(un->operator, "-") == 0) {
            const char *t = get_expression_type(cg, un->expression);
            if (t && strcmp(t, "flotante") == 0) {
                int tmp = (dest_reg == 1) ? 2 : 1;
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
        int is_texto = (lt && (strcmp(lt, "texto") == 0 || strcmp(lt, "concepto") == 0 || strcmp(lt, "caracter") == 0)) ||
                       (rt && (strcmp(rt, "texto") == 0 || strcmp(rt, "concepto") == 0 || strcmp(rt, "caracter") == 0));
        int is_flt = (lt && strcmp(lt, "flotante") == 0) || (rt && strcmp(rt, "flotante") == 0);

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
            emit(cg, OP_ESCRIBIR, tmp.addr & 0xFF, rL, (tmp.addr >> 8) & 0xFF, fl_w);
            
            rR_reg = rL == 1 ? 3 : 1;
            (void)visit_expression(cg, bn->right, rR_reg);
            
            uint8_t fl_r = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
            if (tmp.is_relative) fl_r |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_LEER, rL, tmp.addr & 0xFF, (tmp.addr >> 8) & 0xFF, fl_r);
        } else if (strcmp(op, "y") == 0) {
            int end_label = new_label(cg);
            rL = visit_expression(cg, bn->left, dest_reg);
            if (cg->has_error) return dest_reg;
            
            emit(cg, OP_CMP_EQ, dest_reg, rL, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            
            int tmp = dest_reg == 1 ? 2 : 1;
            emit(cg, OP_CMP_EQ, tmp, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_SI, tmp, 0, 0, IR_INST_FLAG_A_REGISTER);
            add_patch(cg, end_label, PATCH_SI);
            
            int rR = visit_expression(cg, bn->right, dest_reg);
            if (cg->has_error) return dest_reg;
            
            if (rR != dest_reg) {
                emit(cg, OP_MOVER, dest_reg, rR, 0, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            }
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            
            mark_label(cg, end_label);
            return dest_reg;
        } else if (strcmp(op, "o") == 0) {
            int end_label = new_label(cg);
            rL = visit_expression(cg, bn->left, dest_reg);
            if (cg->has_error) return dest_reg;
            
            emit(cg, OP_CMP_EQ, dest_reg, rL, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            
            emit(cg, OP_SI, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            add_patch(cg, end_label, PATCH_SI);
            
            int rR = visit_expression(cg, bn->right, dest_reg);
            if (cg->has_error) return dest_reg;
            
            if (rR != dest_reg) {
                emit(cg, OP_MOVER, dest_reg, rR, 0, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
            }
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            emit(cg, OP_CMP_EQ, dest_reg, dest_reg, 0, IR_INST_FLAG_C_IMMEDIATE);
            
            mark_label(cg, end_label);
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
                emit(cg, OP_ESCRIBIR, tmp.addr & 0xFF, rR_reg, (tmp.addr >> 8) & 0xFF, fl_w);
                
                rL = visit_expression(cg, bn->left, dest_reg);
                if (cg->has_error) return dest_reg;
                
                uint8_t fl_r = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp.is_relative) fl_r |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_LEER, rR_reg, tmp.addr & 0xFF, (tmp.addr >> 8) & 0xFF, fl_r);
            } else {
                rL = visit_expression(cg, bn->left, dest_reg);
                if (cg->has_error) return dest_reg;
                
                SymResult tmp = sym_reserve_temp(&cg->sym, 8);
                uint8_t fl_w = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp.is_relative) fl_w |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, tmp.addr & 0xFF, rL, (tmp.addr >> 8) & 0xFF, fl_w);
                
                rR_reg = visit_expression(cg, bn->right, next_safe_reg);
                if (cg->has_error) return dest_reg;
                
                uint8_t fl_r = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                if (tmp.is_relative) fl_r |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_LEER, rL, tmp.addr & 0xFF, (tmp.addr >> 8) & 0xFF, fl_r);
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
            if (lt && (lt_is_int || lt_is_flt || lt_is_car) && !expr_already_yields_text_string_id(bn->left))
                emit(cg, lt_is_car ? OP_STR_DESDE_CODIGO : OP_STR_DESDE_NUMERO, rL, rL, lt_is_int ? 1 : 0,
                     IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | (lt_is_car ? 0 : IR_INST_FLAG_C_IMMEDIATE));
            if (rt && (rt_is_int || rt_is_flt || rt_is_car) && !expr_already_yields_text_string_id(bn->right))
                emit(cg, rt_is_car ? OP_STR_DESDE_CODIGO : OP_STR_DESDE_NUMERO, rR_reg, rR_reg, rt_is_int ? 1 : 0,
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
        int reg = visit_expression(cg, tn->condition, 1);
        emit(cg, OP_CMP_EQ, 2, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 2, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, else_id, PATCH_SI);
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
        
        if (visit_call_sistema(cg, cn, dest_reg)) {
            if (cg->has_error) return dest_reg;
            return dest_reg;
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
                for (size_t i = 0; i < cn->n_args && i < 4; i++) {
                    if (!cn->args || !cn->args[i]) continue;
                    visit_expression(cg, cn->args[i], (int)(1 + i));
                }
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
            for (size_t i = 0; i < cn->n_args && i < 4; i++) {
                if (!cn->args || !cn->args[i]) continue;
                visit_expression(cg, cn->args[i], (int)(1 + i));
            }
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, CG_INDIRECT_CALLEE_REG, 0, 0, 0);
            emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
            return dest_reg;
        }

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
                if (!arg_type) arg_type = "entero"; // default fallback
                SymResult p_r = sym_declare(&cg->sym, ld->params[i], arg_type, 8, 1, 0, NULL); 
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (p_r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, p_r.addr & 0xFF, reg, (p_r.addr >> 8) & 0xFF, fl);
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

        int label_id = cn->name ? get_func_label(cg, cn->name) : -1;
        if (label_id >= 0) {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            for (size_t i = 0; i < cn->n_args && i < 4; i++) {
                if (!cn->args || !cn->args[i]) continue;
                visit_expression(cg, cn->args[i], (int)(1 + i));
            }
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            add_patch(cg, label_id, PATCH_JUMP);
            emit(cg, OP_MOVER, dest_reg, 1, 0, IR_INST_FLAG_B_REGISTER);
        } else {
            if (codegen_error_vec_constructor_arity(cg, cn))
                return dest_reg;
            if (cn->name && is_sistema_llamada(cn->name, strlen(cn->name))) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "'%s' es una funcion incorporada del lenguaje, pero el numero o la forma de los argumentos "
                         "no coincide con ninguna firma que el compilador admita en esta llamada (se pasaron %zu). "
                         "Revise la documentacion o el orden de los parametros.",
                         cn->name, cn->n_args);
            } else {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Llamada no resuelta: '%s' no es una funcion definida en este programa. "
                         "Si buscaba una API del lenguaje, compruebe el nombre exacto y cuantos argumentos lleva entre parentesis.",
                         cn->name ? cn->name : "?");
            }
            cg->has_error = 1;
            cg->err_line = node->line;
            cg->err_col = node->col;
        }
        return dest_reg;
    }
    if (is_node(node, NODE_INDEX_ACCESS)) {
        IndexAccessNode *ian = (IndexAccessNode*)node;
        int target_reg = visit_expression(cg, ian->target, dest_reg);
        int index_reg = visit_expression(cg, ian->index, dest_reg == 1 ? 2 : 1);
        const char *t = get_expression_type(cg, ian->target);
        if (t && strcmp(t, "mapa") == 0)
            emit(cg, OP_MEM_MAPA_OBTENER, dest_reg, target_reg, index_reg, 0);
        else
            emit(cg, OP_MEM_LISTA_OBTENER, dest_reg, target_reg, index_reg, 0);
        return dest_reg;
    }
    if (is_node(node, NODE_MEMBER_ACCESS)) {
        MemberAccessNode *man = (MemberAccessNode*)node;
        const char *t = get_expression_type(cg, man->target);
        if ((strcmp(t, "lista") == 0 || strcmp(t, "mapa") == 0) &&
            (strcmp(man->member, "medida") == 0 || strcmp(man->member, "tamano") == 0 || strcmp(man->member, "size") == 0)) {
            int tr = visit_expression(cg, man->target, dest_reg);
            emit(cg, OP_MEM_LISTA_TAMANO, dest_reg, tr, 0, IR_INST_FLAG_B_REGISTER);
            return dest_reg;
        }
        MemberAddrResult mar = get_member_address(cg, node, dest_reg == 1 ? 2 : 1);
        if (mar.in_reg) {
            emit(cg, OP_LEER, dest_reg, mar.reg, 0, IR_INST_FLAG_B_REGISTER);
        } else {
            uint8_t flags = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
            if (mar.is_relative) flags |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_LEER, dest_reg, mar.addr & 0xFF, (mar.addr >> 8) & 0xFF, flags);
        }
        return dest_reg;
    }
    if (is_node(node, NODE_CONTIENE_TEXTO)) {
        ContieneTextoNode *cn = (ContieneTextoNode*)node;
        visit_expression(cg, cn->source, dest_reg);
        int r2 = dest_reg == 1 ? 2 : 1;
        visit_expression(cg, cn->pattern, r2);
        emit(cg, OP_MEM_CONTIENE_TEXTO_REG, dest_reg, dest_reg, r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return dest_reg;
    }
    if (is_node(node, NODE_TERMINA_CON)) {
        TerminaConNode *tn = (TerminaConNode*)node;
        visit_expression(cg, tn->source, dest_reg);
        int r2 = dest_reg == 1 ? 2 : 1;
        visit_expression(cg, tn->suffix, r2);
        emit(cg, OP_MEM_TERMINA_CON_REG, dest_reg, dest_reg, r2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        return dest_reg;
    }
    if (is_node(node, NODE_MAP_LITERAL)) {
        MapLiteralNode *mln = (MapLiteralNode*)node;
        emit(cg, OP_MEM_MAPA_CREAR, dest_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
        for (size_t i = 0; i < mln->n; i++) {
            int k_reg = visit_expression(cg, mln->keys[i], dest_reg == 1 ? 2 : 1);
            int v_reg = visit_expression(cg, mln->values[i], dest_reg == 1 ? 3 : (dest_reg == 2 ? 3 : 2));
            emit(cg, OP_MEM_MAPA_PONER, dest_reg, k_reg, v_reg, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
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

/* Conversion implicita al guardar: entero <- flotante (F2I), flotante <- entero (I2F). */
static void emit_conv_for_store(CodeGen *cg, const char *dest_type, const char *expr_type, int reg) {
    if (!dest_type || !expr_type) return;
    if (strcmp(dest_type, "entero") == 0 && strcmp(expr_type, "flotante") == 0)
        emit(cg, OP_CONV_F2I, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
    else if (strcmp(dest_type, "flotante") == 0 && strcmp(expr_type, "entero") == 0)
        emit(cg, OP_CONV_I2F, reg, reg, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
}

/* Solo entero<->flotante es implicito; otros tipos a numerico exigen API explicita (str_a_*, etc.). */
static int reject_non_numeric_to_scalar(CodeGen *cg, const char *dest_type, const char *expr_type, int line, int col) {
    if (!dest_type || !expr_type) return 0;
    if (strcmp(dest_type, "entero") != 0 && strcmp(dest_type, "flotante") != 0) return 0;
    if (strcmp(expr_type, "entero") == 0 || strcmp(expr_type, "flotante") == 0) return 0;
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
    if (strcmp(expr_type, "entero") == 0 || strcmp(expr_type, "flotante") == 0) return 0;
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

/* --- 4.3 VarDeclNode, 4.4 AssignmentNode --- */
static void visit_statement(CodeGen *cg, ASTNode *node) {
    if (!node) return;
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
                cg->has_error = 1;
                cg->err_line = vd->base.line;
                cg->err_col = vd->base.col;
                snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' ya declarada en este alcance", vd->name);
            }
            return;
        }

        size_t sz = sym_get_struct_size(&cg->sym, vd->type_name);
        if (sz == 0) sz = 8;
        SymResult r = sym_declare(&cg->sym, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0,
                                 vd->list_element_type);
        if (!r.found) {
            cg->has_error = 1;
            cg->err_line = vd->base.line;
            cg->err_col = vd->base.col;
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' ya declarada en este alcance", vd->name);
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
                        emit(cg, OP_ESCRIBIR, a & 0xFF, reg, (a >> 8) & 0xFF, fl);
                    }
                    return;
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
                                emit(cg, OP_LEER, 1, (srL.addr + off) & 0xFF, ((srL.addr + off) >> 8) & 0xFF, flL);
                                emit(cg, OP_LEER, 2, (srR.addr + off) & 0xFF, ((srR.addr + off) >> 8) & 0xFF, flR);
                                if (op_add) emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                else if (op_sub) emit(cg, OP_RESTAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                else emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else if (scalar_l) {
                                (void)visit_expression(cg, bn->left, 1);
                                if (strcmp(lt, "entero") == 0) emit(cg, OP_CONV_I2F, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                emit(cg, OP_LEER, 2, (srR.addr + off) & 0xFF, ((srR.addr + off) >> 8) & 0xFF, flR);
                                emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            } else {
                                (void)visit_expression(cg, bn->right, 2);
                                if (strcmp(rt, "entero") == 0) emit(cg, OP_CONV_I2F, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                                emit(cg, OP_LEER, 1, (srL.addr + off) & 0xFF, ((srL.addr + off) >> 8) & 0xFF, flL);
                                emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            }
                            emit(cg, OP_ESCRIBIR, (r.addr + off) & 0xFF, 1, ((r.addr + off) >> 8) & 0xFF, fl);
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
            emit(cg, OP_ESCRIBIR, r.addr & 0xFF, reg, (r.addr >> 8) & 0xFF, flags);
        }
        return;
    }
    if (is_node(node, NODE_ASSIGNMENT)) {
        AssignmentNode *an = (AssignmentNode*)node;
        if (is_node(an->target, NODE_MEMBER_ACCESS)) {
            MemberAccessNode *man_as = (MemberAccessNode*)an->target;
            const char *ft = get_member_chain_type(cg, an->target);
            if (!ft) {
                codegen_error_struct_member_access(cg, man_as, "asignacion");
                return;
            }
            MemberAddrResult mar = get_member_address(cg, an->target, 2);
            if (cg->has_error) return;
            const char *et = get_expression_type(cg, an->expression);
            if (reject_non_numeric_to_scalar(cg, ft, et, an->target->line, an->target->col))
                return;
            int val_reg = 1;
            (void)visit_expression(cg, an->expression, val_reg);
            if (cg->has_error) return;
            emit_conv_for_store(cg, ft, et, val_reg);
            if (mar.in_reg) {
                uint8_t fl = IR_INST_FLAG_B_REGISTER;
                if (mar.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, (uint8_t)mar.reg, (uint8_t)val_reg, 0, fl);
            } else {
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (mar.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, mar.addr & 0xFF, (uint8_t)val_reg, (mar.addr >> 8) & 0xFF, fl);
            }
            return;
        }
        if (!is_node(an->target, NODE_IDENTIFIER)) return;
        const char *name = ((IdentifierNode*)an->target)->name;
        SymResult r = sym_lookup(&cg->sym, name);
        if (r.found && r.is_const) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: no se puede asignar a la constante '%s'", name);
            cg->has_error = 1;
            cg->err_line = ((IdentifierNode*)an->target)->line;
            cg->err_col = ((IdentifierNode*)an->target)->col;
            return;
        }
        if (!r.found) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' no declarada antes de su uso", name);
            cg->has_error = 1;
            cg->err_line = ((IdentifierNode*)an->target)->line;
            cg->err_col = ((IdentifierNode*)an->target)->col;
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
                    emit(cg, OP_ESCRIBIR, a & 0xFF, reg, (a >> 8) & 0xFF, fl);
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
                    SymResult srL = {0}, srR = {0};
                    int has_addr_l = 0, has_addr_r = 0;
                    if (is_node(bn->left, NODE_IDENTIFIER)) {
                        srL = sym_lookup(&cg->sym, ((IdentifierNode*)bn->left)->name);
                        if (!srL.found) srL = sym_get_or_create(&cg->sym, ((IdentifierNode*)bn->left)->name, NULL);
                        has_addr_l = 1;
                    }
                    if (is_node(bn->right, NODE_IDENTIFIER)) {
                        srR = sym_lookup(&cg->sym, ((IdentifierNode*)bn->right)->name);
                        if (!srR.found) srR = sym_get_or_create(&cg->sym, ((IdentifierNode*)bn->right)->name, NULL);
                        has_addr_r = 1;
                    }
                    for (int i = 0; i < ncomp; i++) {
                        uint32_t off = (uint32_t)(i * 8);
                        if (vec_op && has_addr_l && has_addr_r) {
                            uint8_t flL = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                            if (srL.is_relative) flL |= IR_INST_FLAG_RELATIVE;
                            uint8_t flR = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                            if (srR.is_relative) flR |= IR_INST_FLAG_RELATIVE;
                            emit(cg, OP_LEER, 1, (srL.addr + off) & 0xFF, ((srL.addr + off) >> 8) & 0xFF, flL);
                            emit(cg, OP_LEER, 2, (srR.addr + off) & 0xFF, ((srR.addr + off) >> 8) & 0xFF, flR);
                            if (op_add) emit(cg, OP_SUMAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            else if (op_sub) emit(cg, OP_RESTAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            else if (op_mul) emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        } else if (scalar_l && has_addr_r) {
                            (void)visit_expression(cg, bn->left, 1);
                            if (strcmp(lt, "entero") == 0) emit(cg, OP_CONV_I2F, 1, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            uint8_t flR = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                            if (srR.is_relative) flR |= IR_INST_FLAG_RELATIVE;
                            emit(cg, OP_LEER, 2, (srR.addr + off) & 0xFF, ((srR.addr + off) >> 8) & 0xFF, flR);
                            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        } else if (scalar_r && has_addr_l) {
                            (void)visit_expression(cg, bn->right, 2);
                            if (strcmp(rt, "entero") == 0) emit(cg, OP_CONV_I2F, 2, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                            uint8_t flL = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
                            if (srL.is_relative) flL |= IR_INST_FLAG_RELATIVE;
                            emit(cg, OP_LEER, 1, (srL.addr + off) & 0xFF, ((srL.addr + off) >> 8) & 0xFF, flL);
                            emit(cg, OP_MULTIPLICAR_FLT, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
                        } else break;
                        uint32_t a = r.addr + off;
                        emit(cg, OP_ESCRIBIR, a & 0xFF, 1, (a >> 8) & 0xFF, fl);
                    }
                    return;
                }
            }
        }
        const char *et = get_expression_type(cg, an->expression);
        if (reject_non_numeric_to_scalar(cg, vt, et, ((IdentifierNode *)an->target)->line,
                                         ((IdentifierNode *)an->target)->col))
            return;
        int prev_allow = cg->expr_allow_func_literal;
        if (vt && strcmp(vt, "funcion") == 0) cg->expr_allow_func_literal = 1;
        int reg = visit_expression(cg, an->expression, 1);
        cg->expr_allow_func_literal = prev_allow;
        if (cg->has_error) return;
        emit_conv_for_store(cg, vt, et, reg);
        uint8_t flags = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) flags |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, r.addr & 0xFF, reg, (r.addr >> 8) & 0xFF, flags);
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
            emit(cg, OP_SI, cmp_reg, 0, 0, IR_INST_FLAG_A_REGISTER);
            add_patch(cg, next_case_id, PATCH_SI);
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
            if (tn->catch_var && tn->catch_var[0]) {
                SymResult rcv = sym_lookup(&cg->sym, tn->catch_var);
                if (!rcv.found) rcv = sym_declare(&cg->sym, tn->catch_var, "texto", 8, 0, 0, NULL);
                uint8_t flw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (rcv.is_relative) flw |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, rcv.addr & 0xFF, 1, (rcv.addr >> 8) & 0xFF, flw);
            }
            sym_enter_scope(&cg->sym, 0);
            visit_block(cg, tn->catch_body);
            sym_exit_scope(&cg->sym);
            emit(cg, OP_IR, 0, 0, 0, 0);
            add_patch(cg, has_final ? final_id : end_id, PATCH_JUMP);
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
        if (!cg->try_stack_n) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: 'lanzar' solo puede usarse dentro de un bloque 'intentar'");
            cg->has_error = 1;
            cg->err_line = th->base.line;
            cg->err_col = th->base.col;
            return;
        }
        (void)visit_expression(cg, th->expression, 1);
        TryLabel *tc = &cg->try_stack[cg->try_stack_n - 1];
        emit(cg, OP_IR, 0, 0, 0, 0);
        if (tc->has_catch) add_patch(cg, tc->catch_id, PATCH_JUMP);
        else if (tc->has_final) add_patch(cg, tc->final_id, PATCH_JUMP);
        else add_patch(cg, tc->end_id, PATCH_JUMP);
        return;
    }
    /* 4.5 WhileNode */
    if (is_node(node, NODE_WHILE)) {
        WhileNode *wn = (WhileNode*)node;
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
        int reg = visit_expression(cg, wn->condition, 1);
        emit(cg, OP_CMP_EQ, 2, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 2, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, end_id, PATCH_SI);
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
        SymResult iter_r = sym_declare(&cg->sym, fe->iter_name, fe->iter_type, 8, 0, 0, NULL);
        if (!iter_r.found) {
            snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' ya declarada en este alcance", fe->iter_name);
            cg->has_error = 1;
            cg->err_line = fe->base.line;
            cg->err_col = fe->base.col;
            sym_exit_scope(&cg->sym);
            if (cg->loop_stack_n) cg->loop_stack_n--;
            return;
        }
        SymResult src_tmp = sym_reserve_temp(&cg->sym, 8);
        SymResult idx_tmp = sym_reserve_temp(&cg->sym, 8);
        visit_expression(cg, fe->collection, 1);
        uint8_t flw = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flw |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, src_tmp.addr & 0xFF, 1, (src_tmp.addr >> 8) & 0xFF, flw);
        emit(cg, OP_MOVER, 15, 0, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        uint8_t fli = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) fli |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, idx_tmp.addr & 0xFF, 15, (idx_tmp.addr >> 8) & 0xFF, fli);
        mark_label(cg, start_id);
        uint8_t flr = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (src_tmp.is_relative) flr |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 15, src_tmp.addr & 0xFF, (src_tmp.addr >> 8) & 0xFF, flr);
        emit(cg, OP_MEM_LISTA_TAMANO, 16, 15, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        uint8_t flri = IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE;
        if (idx_tmp.is_relative) flri |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_CMP_LT, 18, 17, 16, 0);
        emit(cg, OP_CMP_EQ, 18, 18, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 18, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, end_id, PATCH_SI);
        emit(cg, OP_MEM_LISTA_OBTENER, 19, 15, 17, 0);
        uint8_t flit = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (iter_r.is_relative) flit |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, iter_r.addr & 0xFF, 19, (iter_r.addr >> 8) & 0xFF, flit);
        visit_block(cg, fe->body);
        emit(cg, OP_LEER, 17, idx_tmp.addr & 0xFF, (idx_tmp.addr >> 8) & 0xFF, flri);
        emit(cg, OP_SUMAR, 17, 17, 1, IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_ESCRIBIR, idx_tmp.addr & 0xFF, 17, (idx_tmp.addr >> 8) & 0xFF, fli);
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
        int reg = visit_expression(cg, dwn->condition, 1);
        emit(cg, OP_CMP_EQ, 2, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_CMP_EQ, 2, 2, 0, IR_INST_FLAG_C_IMMEDIATE); // 2 = !(!reg) (verdadero si reg!=0)
        
        // Si reg es verdadero (2=1), salta al start_id (vuelve al inicio)
        emit(cg, OP_SI, 2, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, start_id, PATCH_SI);
        
        mark_label(cg, end_id);
        if (cg->loop_stack_n) cg->loop_stack_n--;
        return;
    }
    /* 4.6 IfNode */
    if (is_node(node, NODE_IF)) {
        IfNode *in = (IfNode*)node;
        int else_id = new_label(cg);
        int end_id = new_label(cg);
        int reg = visit_expression(cg, in->condition, 1);
        emit(cg, OP_CMP_EQ, 1, reg, 0, IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_SI, 1, 0, 0, IR_INST_FLAG_A_REGISTER);
        add_patch(cg, else_id, PATCH_SI);
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
        visit_expression(cg, rn->key, 1);
        if (rn->value) {
            visit_expression(cg, rn->value, 2);
            emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, 1, 2, 90, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE);
        } else {
            emit(cg, OP_MEM_APRENDER_CONCEPTO, 1, 100, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
        }
        return;
    }
    if (is_node(node, NODE_ASOCIAR)) {
        AsociarNode *an = (AsociarNode*)node;
        visit_expression(cg, an->concept1, 1);
        visit_expression(cg, an->concept2, 2);
        if (an->weight)
            visit_expression(cg, an->weight, 3);
        else
            emit(cg, OP_MOVER, 3, 90, 0, IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
        emit(cg, OP_MEM_ASOCIAR_CONCEPTOS, 1, 2, 3, 0);
        return;
    }
    if (is_node(node, NODE_APRENDER)) {
        AprenderNode *an = (AprenderNode*)node;
        visit_expression(cg, an->concept, 1);
        if (an->weight) {
            visit_expression(cg, an->weight, 2);
            emit(cg, OP_MEM_APRENDER_CONCEPTO, 1, 2, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        } else {
            emit(cg, OP_MEM_APRENDER_CONCEPTO, 1, 100, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_IMMEDIATE);
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
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 3, (r.addr >> 8) & 0xFF, fl);
        }
        return;
    }
    if (is_node(node, NODE_CONTIENE_TEXTO)) {
        ContieneTextoNode *cn = (ContieneTextoNode*)node;
        visit_expression(cg, cn->source, 1);
        visit_expression(cg, cn->pattern, 2);
        emit(cg, OP_MEM_CONTIENE_TEXTO_REG, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        SymResult r = sym_get_or_create(&cg->sym, "resultado", NULL);
        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 1, (r.addr >> 8) & 0xFF, fl);
        return;
    }
    if (is_node(node, NODE_TERMINA_CON)) {
        TerminaConNode *tn = (TerminaConNode*)node;
        visit_expression(cg, tn->source, 1);
        visit_expression(cg, tn->suffix, 2);
        emit(cg, OP_MEM_TERMINA_CON_REG, 1, 1, 2, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        SymResult r = sym_get_or_create(&cg->sym, "resultado", NULL);
        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 1, (r.addr >> 8) & 0xFF, fl);
        return;
    }
    if (is_node(node, NODE_ULTIMA_PALABRA)) {
        UltimaPalabraNode *un = (UltimaPalabraNode*)node;
        visit_expression(cg, un->source, 1);
        emit(cg, OP_MEM_ULTIMA_PALABRA, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        if (un->target) {
            SymResult r = sym_get_or_create(&cg->sym, un->target, NULL);
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 2, (r.addr >> 8) & 0xFF, fl);
        }
        return;
    }
    if (is_node(node, NODE_COPIAR_TEXTO)) {
        CopiarTextoNode *cn = (CopiarTextoNode*)node;
        visit_expression(cg, cn->source, 1);
        emit(cg, OP_MEM_COPIAR_TEXTO, 2, 1, 0, IR_INST_FLAG_A_REGISTER | IR_INST_FLAG_B_REGISTER);
        if (cn->target) {
            SymResult r = sym_get_or_create(&cg->sym, cn->target, NULL);
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 2, (r.addr >> 8) & 0xFF, fl);
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
                for (size_t i = 0; i < cn->n_args && i < 4; i++) {
                    if (!cn->args || !cn->args[i]) continue;
                    visit_expression(cg, cn->args[i], (int)(1 + i));
                }
                cg->expr_allow_func_literal = prev;
                emit(cg, OP_LLAMAR, CG_INDIRECT_CALLEE_REG, 0, 0, 0);
                return;
            }
        }

        if (cn->callee) {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            visit_expression(cg, cn->callee, CG_INDIRECT_CALLEE_REG);
            for (size_t i = 0; i < cn->n_args && i < 4; i++) {
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
                SymResult p_r = sym_declare(&cg->sym, ld->params[i], "entero", 8, 1, 0, NULL); 
                uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (p_r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, p_r.addr & 0xFF, reg, (p_r.addr >> 8) & 0xFF, fl);
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

        int label_id = cn->name ? get_func_label(cg, cn->name) : -1;
        if (label_id >= 0) {
            int prev = cg->expr_allow_func_literal;
            cg->expr_allow_func_literal = 1;
            for (size_t i = 0; i < cn->n_args && i < 4; i++) {
                if (!cn->args || !cn->args[i]) continue;
                visit_expression(cg, cn->args[i], (int)(1 + i));
            }
            cg->expr_allow_func_literal = prev;
            emit(cg, OP_LLAMAR, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE);
            add_patch(cg, label_id, PATCH_JUMP);
        } else {
            if (codegen_error_vec_constructor_arity(cg, cn))
                return;
            if (cn->name && is_sistema_llamada(cn->name, strlen(cn->name))) {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "'%s' es una funcion incorporada del lenguaje, pero el numero o la forma de los argumentos "
                         "no coincide con ninguna firma que el compilador admita en esta llamada (se pasaron %zu). "
                         "Revise la documentacion o el orden de los parametros.",
                         cn->name, cn->n_args);
            } else {
                snprintf(cg->last_error, CODEGEN_ERROR_MAX,
                         "Llamada no resuelta: '%s' no es una funcion definida en este programa. "
                         "Si buscaba una API del lenguaje, compruebe el nombre exacto y cuantos argumentos lleva entre parentesis.",
                         cn->name ? cn->name : "?");
            }
            cg->has_error = 1;
            cg->err_line = node->line;
            cg->err_col = node->col;
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
                r = sym_declare(&cg->sym, in->variable, "texto", 8, 0, 0, NULL);
            uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
            if (r.is_relative) fl |= IR_INST_FLAG_RELATIVE;
            emit(cg, OP_ESCRIBIR, r.addr & 0xFF, 1, (r.addr >> 8) & 0xFF, fl);
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
        for (size_t i = 0; i < b->n; i++)
            visit_statement(cg, b->statements[i]);
    } else {
        visit_statement(cg, node);
    }
}

/* 4.9 FunctionNode */
static void visit_function(CodeGen *cg, ASTNode *node) {
    FunctionNode *fn = (FunctionNode*)node;
    sym_enter_scope(&cg->sym, 1);
    cg->function_depth++;
    const char *prev_name = cg->current_fn_name;
    const char *prev_ret = cg->current_fn_return;
    cg->current_fn_name = fn->name;
    cg->current_fn_return = fn->return_type ? fn->return_type : "entero";
    /* Reservar slot para OP_RESERVAR_PILA (parcheamos tamaño después) */
    size_t reserve_pos = cg->code_size;
    emit(cg, OP_RESERVAR_PILA, 0, 0, 0, IR_INST_FLAG_A_IMMEDIATE);
    /* Declarar parámetros y guardar sus direcciones para copiar args (regs 1,2,3,4) */
    uint32_t param_addrs[4] = {0};
    int n_params = (int)(fn->n_params < 4 ? fn->n_params : 4);
    for (int i = 0; i < n_params; i++) {
        VarDeclNode *vd = (VarDeclNode*)fn->params[i];
        if (vd) {
            SymResult r = sym_declare(&cg->sym, vd->name, vd->type_name, 8, 1, 0, vd->list_element_type);
            if (r.found) param_addrs[i] = r.addr;
        }
    }
    /* Copiar argumentos de regs 1..n a slots locales de parámetros */
    for (int i = 0; i < n_params; i++) {
        uint32_t addr = param_addrs[i];
        uint8_t fl = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
        fl |= IR_INST_FLAG_RELATIVE;
        emit(cg, OP_ESCRIBIR, addr & 0xFF, 1 + i, (addr >> 8) & 0xFF, fl);
    }
    visit_block(cg, fn->body);
    uint32_t frame_size = cg->sym.next_local_offset;
    cg->code[reserve_pos + 2] = frame_size & 0xFF;
    cg->code[reserve_pos + 3] = (frame_size >> 8) & 0xFF;
    cg->code[reserve_pos + 4] = (frame_size >> 16) & 0xFF;
    emit(cg, OP_RETORNAR, 0, 0, 0, 0);
    cg->function_depth--;
    cg->current_fn_return = prev_ret;
    cg->current_fn_name = prev_name;
    sym_exit_scope(&cg->sym);
}

uint8_t *codegen_generate(CodeGen *cg, ASTNode *ast, size_t *out_len) {
    if (!ast || ast->type != NODE_PROGRAM) return NULL;
    ProgramNode *p = (ProgramNode*)ast;

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

    /* Registrar structs desde globals */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_STRUCT_DEF) {
            StructDefNode *sd = (StructDefNode*)g;
            sym_register_struct(&cg->sym, sd->name, (const char**)sd->field_types,
                               (const char**)sd->field_names, sd->n_fields);
        }
    }
    /* Global VarDecls + init (macros/lambdas: misma ruta que visit_statement) */
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *g = p->globals[i];
        if (g && g->type == NODE_VAR_DECL) {
            VarDeclNode *vd = (VarDeclNode*)g;
            if (vd->value && is_node(vd->value, NODE_LAMBDA_DECL) &&
                vd->type_name && strcmp(vd->type_name, "macro") == 0) {
                SymResult r = sym_declare_macro(&cg->sym, vd->name, vd->value);
                if (!r.found) {
                    cg->has_error = 1;
                    cg->err_line = vd->base.line;
                    cg->err_col = vd->base.col;
                    snprintf(cg->last_error, CODEGEN_ERROR_MAX, "Error: variable '%s' ya declarada en este alcance", vd->name);
                }
                continue;
            }
            size_t sz = sym_get_struct_size(&cg->sym, vd->type_name);
            if (sz == 0) sz = 8;
            SymResult r = sym_declare(&cg->sym, vd->name, vd->type_name, sz, 0, vd->is_const ? 1 : 0,
                                     vd->list_element_type);
            if (r.found && vd->value) {
                const char *et = get_expression_type(cg, vd->value);
                if (reject_non_numeric_to_scalar(cg, vd->type_name, et, vd->base.line, vd->base.col))
                    continue;
                int prev_allow = cg->expr_allow_func_literal;
                if (vd->type_name && strcmp(vd->type_name, "funcion") == 0)
                    cg->expr_allow_func_literal = 1;
                int reg = visit_expression(cg, vd->value, 1);
                cg->expr_allow_func_literal = prev_allow;
                if (cg->has_error) continue;
                emit_conv_for_store(cg, vd->type_name, et, reg);
                uint8_t flags = IR_INST_FLAG_A_IMMEDIATE | IR_INST_FLAG_B_REGISTER | IR_INST_FLAG_C_IMMEDIATE;
                if (r.is_relative) flags |= IR_INST_FLAG_RELATIVE;
                emit(cg, OP_ESCRIBIR, r.addr & 0xFF, reg, (r.addr >> 8) & 0xFF, flags);
            }
        }
    }

    /* Registrar labels de funciones para CallNode */
    cg->n_funcs = p->n_funcs;
    if (p->n_funcs) {
        cg->func_names = malloc(p->n_funcs * sizeof(char*));
        cg->func_return_types = malloc(p->n_funcs * sizeof(char*));
        cg->func_labels = malloc(p->n_funcs * sizeof(int));
        if (cg->func_names && cg->func_labels) {
            for (size_t i = 0; i < p->n_funcs; i++) {
                FunctionNode *fn = (FunctionNode*)p->functions[i];
                cg->func_names[i] = fn->name;
                cg->func_return_types[i] = fn->return_type ? fn->return_type : "entero";
                cg->func_labels[i] = new_label(cg);
            }
        }
    }
    /* Saltar a main si hay funciones */
    int main_id = new_label(cg);
    if (p->n_funcs) {
        emit(cg, OP_IR, 0, 0, 0, 0);
        add_patch(cg, main_id, PATCH_JUMP);
    }
    for (size_t i = 0; i < p->n_funcs; i++) {
        if (cg->func_labels) mark_label(cg, cg->func_labels[i]);
        visit_function(cg, p->functions[i]);
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
