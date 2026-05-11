#include "codegen_c.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static unsigned s_jb_list_lit_seq;

typedef struct {
    FILE *out;
    ProgramNode *prog;
    int indent;
    const char *emitting_class; /* metodo de clase: "MetricasRegresion" o NULL */
    StructDefNode *emitting_struct;
    struct {
        const char *var;
        const char *typ;
    } var_types[192];
    int n_var_types;
    /** 1 en NODE_CALL como sentencia: emitir void(...) sin operador coma ni expr envoltorio. */
    int emit_void_call_as_stmt;
} GenCtx;

static void ind(GenCtx *g) {
    for (int i = 0; i < g->indent; i++)
        fputc(' ', g->out);
}

static StructDefNode *find_struct_def(ProgramNode *p, const char *name) {
    if (!p || !name) return NULL;
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *gl = p->globals[i];
        if (gl && gl->type == NODE_STRUCT_DEF) {
            StructDefNode *sd = (StructDefNode *)gl;
            if (sd->name && strcmp(sd->name, name) == 0) return sd;
        }
    }
    return NULL;
}

static int is_struct_method(ProgramNode *p, const char *cls, const char *meth) {
    StructDefNode *sd;
    if (!p || !cls || !meth) return 0;
    sd = find_struct_def(p, cls);
    if (!sd) return 0;
    for (size_t i = 0; i < sd->n_methods; i++) {
        FunctionNode *f = (FunctionNode *)sd->methods[i];
        if (f && f->name && strcmp(f->name, meth) == 0) return 1;
    }
    return 0;
}

static const char *lookup_var_type(GenCtx *g, const char *var) {
    if (!g || !var) return NULL;
    for (int i = g->n_var_types - 1; i >= 0; i--) {
        if (g->var_types[i].var && strcmp(g->var_types[i].var, var) == 0) return g->var_types[i].typ;
    }
    return NULL;
}

static void push_var_type(GenCtx *g, const char *var, const char *typ) {
    if (!g || !var || !typ || g->n_var_types >= 192) return;
    g->var_types[g->n_var_types].var = var;
    g->var_types[g->n_var_types].typ = typ;
    g->n_var_types++;
}

static int block_ends_with_return(BlockNode *bn) {
    if (!bn || bn->n == 0) return 0;
    ASTNode *last = bn->statements[bn->n - 1u];
    return last && last->type == NODE_RETURN;
}

static int ast_body_ends_with_return(ASTNode *body) {
    if (!body) return 0;
    if (body->type == NODE_BLOCK) return block_ends_with_return((BlockNode *)body);
    return body->type == NODE_RETURN;
}

static int user_func_returns_texto(ProgramNode *p, const char *name) {
    if (!p || !name) return 0;
    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *f = (FunctionNode *)p->functions[i];
        if (f && f->name && strcmp(f->name, name) == 0 && f->return_type && strcmp(f->return_type, "texto") == 0)
            return 1;
    }
    return 0;
}

static int expr_like_texto(GenCtx *g, ASTNode *node) {
    if (!g || !node) return 0;
    if (node->type == NODE_LITERAL) {
        LiteralNode *l = (LiteralNode *)node;
        return l->type_name && strcmp(l->type_name, "texto") == 0;
    }
    if (node->type == NODE_IDENTIFIER) {
        const char *t = lookup_var_type(g, ((IdentifierNode *)node)->name);
        return t && strcmp(t, "texto") == 0;
    }
    if (node->type == NODE_CALL) {
        CallNode *c = (CallNode *)node;
        if (c->callee || !c->name) return 0;
        const char *n = c->name;
        if (user_func_returns_texto(g->prog, n)) return 1;
        if (strcmp(n, "concatenar") == 0) return 1;
        if (strcmp(n, "minusculas") == 0 || strcmp(n, "mayusculas") == 0 || strcmp(n, "str_mayusculas") == 0) return 1;
        if (strcmp(n, "reemplazar") == 0 || strcmp(n, "remplazar") == 0 || strcmp(n, "reemplazar_texto") == 0) return 1;
        if (strcmp(n, "copiar_texto") == 0) return 1;
        if (strcmp(n, "extraer_subtexto") == 0) return 1;
        if (strcmp(n, "str_extraer_caracter") == 0) return 1;
        if (strcmp(n, "caracter_a_texto") == 0) return 1;
        if (strcmp(n, "decimal") == 0) return 1;
        if (strcmp(n, "texto_desde_numero") == 0 || strcmp(n, "str_desde_numero") == 0) return 1;
        if (strcmp(n, "entero_a_texto") == 0) return 1;
        return 0;
    }
    return 0;
}

/** `para cada` sobre mapa: la coleccion es identificador con tipo declarado mapa. */
static int foreach_collection_is_mapa(GenCtx *g, ASTNode *coll) {
    if (!coll || coll->type != NODE_IDENTIFIER) return 0;
    {
        const char *t = lookup_var_type(g, ((IdentifierNode *)coll)->name);
        return t && strcmp(t, "mapa") == 0;
    }
}

static const char *resolve_method_class(ProgramNode *p, const char *cls, const char *meth) {
    if (!p || !cls || !meth) return NULL;
    if (is_struct_method(p, cls, meth)) return cls;
    StructDefNode *sd = find_struct_def(p, cls);
    if (sd && sd->n_extends > 0 && sd->extends_names && sd->extends_names[0])
        return resolve_method_class(p, sd->extends_names[0], meth);
    return NULL;
}

static int is_user_func(ProgramNode *p, const char *name) {
    if (!p || !name) return 0;
    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *f = (FunctionNode *)p->functions[i];
        if (f && f->name && strcmp(f->name, name) == 0) return 1;
    }
    {
        const char *u = strrchr(name, '_');
        if (u && u != name) {
            char clsbuf[256];
            size_t clen = (size_t)(u - name);
            if (clen < sizeof clsbuf) {
                memcpy(clsbuf, name, clen);
                clsbuf[clen] = '\0';
                if (is_struct_method(p, clsbuf, u + 1)) return 1;
            }
        }
    }
    return 0;
}

static void emit_c_str(FILE *out, const char *s) {
    fputc('"', out);
    if (!s) s = "";
    for (; *s; s++) {
        if (*s == '\\' || *s == '"') fputc('\\', out);
        if (*s == '\n') { fputs("\\n", out); continue; }
        if (*s == '\r') { fputs("\\r", out); continue; }
        if (*s == '\t') { fputs("\\t", out); continue; }
        fputc(*s, out);
    }
    fputc('"', out);
}

static void gen_json_append_json_string_char(char *buf, size_t cap, size_t *w, char c) {
    if (*w + 2 >= cap) return;
    if (c == '"' || c == '\\') {
        buf[(*w)++] = '\\';
        buf[(*w)++] = c;
    } else if ((unsigned char)c < 32) {
        /* omitir control en literal pequeno de prueba */
    } else
        buf[(*w)++] = c;
}

static void gen_json_append_json_string_cstr(char *buf, size_t cap, size_t *w, const char *s) {
    if (!s) return;
    for (; *s && *w + 2 < cap; s++)
        gen_json_append_json_string_char(buf, cap, w, *s);
}

static void gen_json_from_map_literal(GenCtx *g, MapLiteralNode *m) {
    char buf[4096];
    size_t w = 0;
    FILE *o = g->out;
    if (w + 2 < sizeof buf) buf[w++] = '{';
    for (size_t i = 0; i < m->n; i++) {
        if (w + 8 >= sizeof buf) break;
        if (i) buf[w++] = ',';
        buf[w++] = '"';
        if (m->keys[i] && m->keys[i]->type == NODE_IDENTIFIER)
            gen_json_append_json_string_cstr(buf, sizeof buf, &w, ((IdentifierNode *)m->keys[i])->name);
        else if (m->keys[i] && m->keys[i]->type == NODE_LITERAL) {
            LiteralNode *lk = (LiteralNode *)m->keys[i];
            if (lk->type_name && strcmp(lk->type_name, "texto") == 0 && lk->value.str)
                gen_json_append_json_string_cstr(buf, sizeof buf, &w, lk->value.str);
            else {
                char t[32];
                snprintf(t, sizeof t, "k%zu", i);
                gen_json_append_json_string_cstr(buf, sizeof buf, &w, t);
            }
        } else {
            char t[32];
            snprintf(t, sizeof t, "k%zu", i);
            gen_json_append_json_string_cstr(buf, sizeof buf, &w, t);
        }
        if (w + 4 >= sizeof buf) break;
        buf[w++] = '"';
        buf[w++] = ':';
        if (m->values[i] && m->values[i]->type == NODE_LITERAL) {
            LiteralNode *lv = (LiteralNode *)m->values[i];
            if (lv->type_name && strcmp(lv->type_name, "texto") == 0 && lv->value.str) {
                buf[w++] = '"';
                gen_json_append_json_string_cstr(buf, sizeof buf, &w, lv->value.str);
                if (w + 1 < sizeof buf) buf[w++] = '"';
            } else if (lv->is_float) {
                int n = snprintf(buf + w, sizeof buf - w, "%.17g", lv->value.f);
                if (n > 0 && (size_t)n < sizeof buf - w) w += (size_t)n;
            } else {
                int n = snprintf(buf + w, sizeof buf - w, "%lld", (long long)lv->value.i);
                if (n > 0 && (size_t)n < sizeof buf - w) w += (size_t)n;
            }
        }
    }
    if (w + 2 < sizeof buf) buf[w++] = '}';
    buf[w] = '\0';
    fputs("jb_json_parse(jb_new_texto(", o);
    emit_c_str(o, buf);
    fputs("))", o);
}

static void gen_expr(GenCtx *g, ASTNode *node);

/* Ruta mapa.m1.m2: root + segs[0]=clave mas interna ... segs[ns-1]=primera desde root */
static int map_path_parts(ASTNode *map_path, const char **rootnm, const char *segs[16], int *nseg) {
    *nseg = 0;
    if (!map_path) return -1;
    if (map_path->type == NODE_IDENTIFIER) {
        *rootnm = ((IdentifierNode *)map_path)->name;
        return 0;
    }
    ASTNode *cur = map_path;
    while (cur && cur->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *mm = (MemberAccessNode *)cur;
        if (*nseg >= 16 || !mm->member) return -1;
        segs[*nseg] = mm->member;
        (*nseg)++;
        cur = mm->target;
    }
    if (!cur || cur->type != NODE_IDENTIFIER) return -1;
    *rootnm = ((IdentifierNode *)cur)->name;
    return 0;
}

/* Emite expresion-statement: ({ ... jb_new_nulo(); }) para mapa_poner en ruta con miembros */
static void emit_map_put_nested(GenCtx *g, ASTNode *map_path, ASTNode *key_e, ASTNode *val_e) {
    FILE *o = g->out;
    const char *rn = NULL;
    const char *segs[16];
    int ns = 0;
    if (map_path_parts(map_path, &rn, segs, &ns) < 0 || !rn) {
        fputs("jb_warn_aot_expr(\"mapa_poner: ruta invalida\")", o);
        return;
    }
    if (ns == 0) {
        fprintf(o, "jb_map_put(&%s, ", rn);
        gen_expr(g, key_e);
        fputs(", ", o);
        gen_expr(g, val_e);
        fputc(')', o);
        return;
    }
    fputs("({ ", o);
    for (int i = 0; i < ns; i++) {
        fprintf(o, "jb_var_t __jb_mp%d = jb_map_get(", i);
        if (i == 0)
            fprintf(o, "%s", rn);
        else
            fprintf(o, "__jb_mp%d", i - 1);
        fputs(", jb_new_texto(", o);
        emit_c_str(o, segs[ns - 1 - i]);
        fputs(")); ", o);
    }
    fprintf(o, "jb_map_put(&__jb_mp%d, ", ns - 1);
    gen_expr(g, key_e);
    fputs(", ", o);
    gen_expr(g, val_e);
    fputs("); ", o);
    for (int i = ns - 1; i >= 1; i--) {
        fprintf(o, "jb_map_put(&__jb_mp%d, jb_new_texto(", i - 1);
        emit_c_str(o, segs[ns - 1 - i]);
        fprintf(o, "), __jb_mp%d); ", i);
    }
    fprintf(o, "jb_map_put(&%s, jb_new_texto(", rn);
    emit_c_str(o, segs[ns - 1]);
    fputs("), __jb_mp0); ", o);
    for (int i = 0; i < ns; i++)
        fprintf(o, "jb_var_clear(&__jb_mp%d); ", i);
    fputs("jb_new_nulo(); })", o);
}

/* lista_agregar / lista_poner sobre mapa.L (lista clonada en mapa) */
static void emit_list_op_on_map_path(GenCtx *g, ASTNode *list_path, const char *op, ASTNode *arg1, ASTNode *arg2) {
    FILE *o = g->out;
    const char *rn = NULL;
    const char *segs[16];
    int ns = 0;
    if (map_path_parts(list_path, &rn, segs, &ns) < 0 || !rn || ns == 0) {
        fputs("jb_warn_aot_expr(\"lista mutada: se esperaba ruta m.campo\")", o);
        return;
    }
    fputs("({ ", o);
    for (int i = 0; i < ns; i++) {
        fprintf(o, "jb_var_t __jb_lp%d = jb_map_get(", i);
        if (i == 0)
            fprintf(o, "%s", rn);
        else
            fprintf(o, "__jb_lp%d", i - 1);
        fputs(", jb_new_texto(", o);
        emit_c_str(o, segs[ns - 1 - i]);
        fputs(")); ", o);
    }
    if (strcmp(op, "agregar") == 0) {
        fprintf(o, "jb_list_push(&__jb_lp%d, ", ns - 1);
        gen_expr(g, arg1);
        fputs("); ", o);
    } else {
        fprintf(o, "jb_list_set(&__jb_lp%d, ", ns - 1);
        gen_expr(g, arg1);
        fputs(", ", o);
        gen_expr(g, arg2);
        fputs("); ", o);
    }
    for (int i = ns - 1; i >= 1; i--) {
        fprintf(o, "jb_map_put(&__jb_lp%d, jb_new_texto(", i - 1);
        emit_c_str(o, segs[ns - 1 - i]);
        fprintf(o, "), __jb_lp%d); ", i);
    }
    fprintf(o, "jb_map_put(&%s, jb_new_texto(", rn);
    emit_c_str(o, segs[ns - 1]);
    fputs("), __jb_lp0); ", o);
    for (int i = 0; i < ns; i++)
        fprintf(o, "jb_var_clear(&__jb_lp%d); ", i);
    fputs("jb_new_nulo(); })", o);
}

/* Asignacion a ruta mapa...clave (ultimo miembro = clave en el mapa contenedor) */
static void emit_nested_map_assign_leaf(GenCtx *g, ASTNode *container_path, const char *leaf_key, ASTNode *val_e) {
    FILE *o = g->out;
    const char *rn = NULL;
    const char *segs[16];
    int ns = 0;
    if (map_path_parts(container_path, &rn, segs, &ns) < 0 || !rn) {
        fputs("jb_warn_aot_expr(\"asignacion miembro mapa invalida\")", o);
        return;
    }
    if (ns == 0) {
        fprintf(o, "jb_put_member_leaf(&%s, ", rn);
        emit_c_str(o, leaf_key);
        fputs(", ", o);
        gen_expr(g, val_e);
        fputs(")", o);
        return;
    }
    fputs("({ ", o);
    for (int i = 0; i < ns; i++) {
        fprintf(o, "jb_var_t __jb_as%d = jb_map_get(", i);
        if (i == 0)
            fprintf(o, "%s", rn);
        else
            fprintf(o, "__jb_as%d", i - 1);
        fputs(", jb_new_texto(", o);
        emit_c_str(o, segs[ns - 1 - i]);
        fputs(")); ", o);
    }
    fprintf(o, "jb_map_put(&__jb_as%d, jb_new_texto(", ns - 1);
    emit_c_str(o, leaf_key);
    fputs("), ", o);
    gen_expr(g, val_e);
    fputs("); ", o);
    for (int i = ns - 1; i >= 1; i--) {
        fprintf(o, "jb_map_put(&__jb_as%d, jb_new_texto(", i - 1);
        emit_c_str(o, segs[ns - 1 - i]);
        fprintf(o, "), __jb_as%d); ", i);
    }
    fprintf(o, "jb_map_put(&%s, jb_new_texto(", rn);
    emit_c_str(o, segs[ns - 1]);
    fputs("), __jb_as0); ", o);
    for (int i = 0; i < ns; i++)
        fprintf(o, "jb_var_clear(&__jb_as%d); ", i);
    fputs("jb_new_nulo(); })", o);
}

static void emit_list_index_set_on_map_path(GenCtx *g, ASTNode *list_path, ASTNode *idx_e, ASTNode *val_e) {
    FILE *o = g->out;
    const char *rn = NULL;
    const char *segs[16];
    int ns = 0;
    if (map_path_parts(list_path, &rn, segs, &ns) < 0 || !rn || ns == 0) {
        fputs("jb_warn_aot_expr(\"lista [] en mapa: ruta invalida\")", o);
        return;
    }
    fputs("({ ", o);
    for (int i = 0; i < ns; i++) {
        fprintf(o, "jb_var_t __jb_ix%d = jb_map_get(", i);
        if (i == 0)
            fprintf(o, "%s", rn);
        else
            fprintf(o, "__jb_ix%d", i - 1);
        fputs(", jb_new_texto(", o);
        emit_c_str(o, segs[ns - 1 - i]);
        fputs(")); ", o);
    }
    fprintf(o, "jb_list_set(&__jb_ix%d, ", ns - 1);
    gen_expr(g, idx_e);
    fputs(", ", o);
    gen_expr(g, val_e);
    fputs("); ", o);
    for (int i = ns - 1; i >= 1; i--) {
        fprintf(o, "jb_map_put(&__jb_ix%d, jb_new_texto(", i - 1);
        emit_c_str(o, segs[ns - 1 - i]);
        fprintf(o, "), __jb_ix%d); ", i);
    }
    fprintf(o, "jb_map_put(&%s, jb_new_texto(", rn);
    emit_c_str(o, segs[ns - 1]);
    fputs("), __jb_ix0); ", o);
    for (int i = 0; i < ns; i++)
        fprintf(o, "jb_var_clear(&__jb_ix%d); ", i);
    fputs("jb_new_nulo(); })", o);
}

static void gen_call(GenCtx *g, CallNode *c) {
    FILE *o = g->out;
    if (!c->name && c->callee && c->callee->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode *)c->callee;
        const char *meth = ma->member;
        const char *cls = NULL;
        if (ma->target && ma->target->type == NODE_IDENTIFIER) {
            const char *tn = ((IdentifierNode *)ma->target)->name;
            if (strcmp(tn, "este") == 0) {
                cls = g->emitting_class;
            } else if (strcmp(tn, "padre") == 0) {
                if (g->emitting_struct && g->emitting_struct->n_extends > 0 && g->emitting_struct->extends_names &&
                    g->emitting_struct->extends_names[0])
                    cls = g->emitting_struct->extends_names[0];
                else
                    cls = g->emitting_class;
            } else
                cls = lookup_var_type(g, tn);
        }
        if (cls && meth) {
            const char *rcls = resolve_method_class(g->prog, cls, meth);
            if (!rcls) rcls = cls;
            fprintf(o, "jbf_%s_%s(", rcls, meth);
            gen_expr(g, ma->target);
            for (size_t i = 0; i < c->n_args; i++) {
                fputs(", ", o);
                gen_expr(g, c->args[i]);
            }
            fputc(')', o);
            return;
        }
        fputs("jb_warn_aot_expr(\"llamada metodo AOT sin tipo de receptor\")", o);
        return;
    }
    if (!c->name) {
        fputs("jb_new_nulo()", o);
        return;
    }
    const char *nm = c->name;
    if (strcmp(nm, "imprimir") == 0) {
        fputs("jb_imprimir(", o);
        if (c->n_args > 0) gen_expr(g, c->args[0]);
        else fputs("jb_new_nulo()", o);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "imprimir_sin_salto") == 0) {
        fputs("jb_imprimir_sin_salto(", o);
        if (c->n_args > 0) gen_expr(g, c->args[0]);
        else fputs("jb_new_nulo()", o);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "imprimir_flotante") == 0) {
        fputs("jb_imprimir_flotante(", o);
        if (c->n_args > 0) gen_expr(g, c->args[0]);
        else fputs("jb_new_nulo()", o);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "leer_entrada") == 0 && c->n_args == 0) {
        fputs("jb_leer_entrada()", o);
        return;
    }
    if (strcmp(nm, "ingresar_texto") == 0 && c->n_args >= 1 && c->args[0] && c->args[0]->type == NODE_IDENTIFIER) {
        fputs("jb_ingresar_texto(&", o);
        fputs(((IdentifierNode *)c->args[0])->name, o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "bit_shl") == 0 && c->n_args >= 2) {
        fputs("jb_bit_shl(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "bit_shr") == 0 && c->n_args >= 2) {
        fputs("jb_bit_shr(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "codigo_caracter") == 0 && c->n_args >= 1) {
        fputs("jb_codigo_caracter(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "caracter_a_texto") == 0 && c->n_args >= 1) {
        fputs("jb_caracter_a_texto(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "str_extraer_caracter") == 0 && c->n_args >= 2) {
        fputs("jb_str_extraer_caracter(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "copiar_texto") == 0 && c->n_args >= 1) {
        fputs("jb_copiar_texto(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "exp") == 0 && c->n_args >= 1) {
        fputs("jb_exp(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "sin") == 0 && c->n_args >= 1) {
        fputs("jb_sin(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "cos") == 0 && c->n_args >= 1) {
        fputs("jb_cos(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "log10") == 0 && c->n_args >= 1) {
        fputs("jb_log10(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if ((strcmp(nm, "segmentar_palabras") == 0 || strcmp(nm, "palabras_de") == 0) && c->n_args >= 2) {
        fputs("jb_dividir_texto(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "abrir_archivo") == 0 && c->n_args >= 2) {
        fputs("jb_fs_abrir(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "escribir_archivo") == 0 && c->n_args >= 2) {
        fputs("jb_fs_escribir(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "cerrar_archivo") == 0 && c->n_args >= 1) {
        fputs("jb_fs_cerrar(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "existe_archivo") == 0 && c->n_args >= 1) {
        fputs("jb_existe_archivo(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_abrir") == 0 && c->n_args >= 2) {
        fputs("jb_fs_abrir(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_cerrar") == 0 && c->n_args >= 1) {
        fputs("jb_fs_cerrar(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_escribir") == 0 && c->n_args >= 2) {
        fputs("jb_fs_escribir(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_escribir_byte") == 0 && c->n_args >= 2) {
        fputs("jb_fs_escribir_byte(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_leer_linea") == 0 && c->n_args >= 1) {
        fputs("jb_fs_leer_linea(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_leer_byte") == 0 && c->n_args >= 1) {
        fputs("jb_fs_leer_byte(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_fin_archivo") == 0 && c->n_args >= 1) {
        fputs("jb_fs_fin_archivo(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_borrar") == 0 && c->n_args >= 1) {
        fputs("jb_fs_borrar(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_mover") == 0 && c->n_args >= 2) {
        fputs("jb_fs_mover(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_copiar") == 0 && c->n_args >= 2) {
        fputs("jb_fs_copiar(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_tamano") == 0 && c->n_args >= 1) {
        fputs("jb_fs_tamano(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_leer_texto") == 0 && c->n_args >= 1) {
        fputs("jb_fs_leer_texto(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "fs_listar") == 0 && c->n_args >= 1) {
        fputs("jb_fs_listar(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_parse") == 0 && c->n_args >= 1) {
        fputs("jb_json_parse(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_tipo") == 0 && c->n_args >= 1) {
        fputs("jb_json_tipo(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_objeto_obtener") == 0 && c->n_args >= 2) {
        fputs("jb_json_objeto_obtener(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_lista_obtener") == 0 && c->n_args >= 2) {
        fputs("jb_json_lista_obtener(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_lista_tamano") == 0 && c->n_args >= 1) {
        fputs("jb_json_lista_tamano(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_stringify") == 0 && c->n_args >= 2) {
        fputs("jb_json_stringify(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_a_entero") == 0 && c->n_args >= 1) {
        fputs("jb_json_a_entero(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_a_flotante") == 0 && c->n_args >= 1) {
        fputs("jb_json_a_flotante(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_a_texto") == 0 && c->n_args >= 1) {
        fputs("jb_json_a_texto(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "json_a_bool") == 0 && c->n_args >= 1) {
        fputs("jb_json_a_bool(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "concatenar") == 0 && c->n_args >= 2) {
        fputs("jb_concat(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "decimal") == 0 && c->n_args >= 2) {
        fputs("jb_decimal(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "formatear_timestamp") == 0 && c->n_args >= 2) {
        fputs("jb_formatear_timestamp(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "nativo_mlp_entrenar") == 0 && c->n_args >= 7) {
        fputs("jb_nativo_mlp_entrenar(", o);
        for (size_t i = 0; i < c->n_args; i++) {
            if (i) fputs(", ", o);
            gen_expr(g, c->args[i]);
        }
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "vec3") == 0 && c->n_args >= 3) {
        fputs("jb_new_vec3_from_jbvals(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "vec4") == 0 && c->n_args >= 4) {
        fputs("jb_new_vec4_from_jbvals(", o);
        for (size_t i = 0; i < 4 && i < c->n_args; i++) {
            if (i) fputs(", ", o);
            gen_expr(g, c->args[i]);
        }
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "vec3_longitud") == 0 && c->n_args >= 1) {
        fputs("jb_vec_longitud(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "vec3_normalizar") == 0 && c->n_args >= 2) {
        fputs("jb_vec3_normalizar(&", o);
        gen_expr(g, c->args[0]);
        fputs(", &", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "mat4_identidad") == 0 && c->n_args >= 1) {
        fputs("jb_mat4_identidad(&", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "mat4_mul_vec4") == 0 && c->n_args >= 3) {
        fputs("jb_mat4_mul_vec4(&", o);
        gen_expr(g, c->args[0]);
        fputs(", &", o);
        gen_expr(g, c->args[1]);
        fputs(", &", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (c->n_args == 0 && find_struct_def(g->prog, nm)) {
        fputs("({ jb_var_t __co = jb_new_map(); ", o);
        if (is_struct_method(g->prog, nm, "inicializar"))
            fprintf(o, "jbf_%s_inicializar(__co); ", nm);
        fputs("__co; })", o);
        return;
    }
    if (strcmp(nm, "str_desde_numero") == 0) {
        fputs("jb_entero_a_texto(", o);
        if (c->n_args > 0) gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "minusculas") == 0 && c->n_args >= 1) {
        fputs("jb_minusculas(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if ((strcmp(nm, "reemplazar") == 0 || strcmp(nm, "remplazar") == 0 || strcmp(nm, "reemplazar_texto") == 0) &&
        c->n_args >= 3) {
        fputs("jb_reemplazar(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "longitud_texto") == 0) {
        fputs("jb_texto_len(", o);
        if (c->n_args > 0) gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "crear_lista") == 0 || strcmp(nm, "lista_crear") == 0) {
        fputs("jb_new_list()", o);
        return;
    }
    if (strcmp(nm, "lista_limpiar") == 0 || strcmp(nm, "mem_lista_limpiar") == 0) {
        if (c->n_args >= 1 && c->args[0] && c->args[0]->type == NODE_IDENTIFIER) {
            const char *vn = ((IdentifierNode *)c->args[0])->name;
            fprintf(o, "(jb_list_clear(&%s), %s)", vn, vn);
            return;
        }
        if (g->emit_void_call_as_stmt)
            fputs("jb_warn_aot(\"lista_limpiar: se esperaba identificador de lista\")", o);
        else
            fputs("jb_warn_aot_expr(\"lista_limpiar: se esperaba identificador de lista\")", o);
        return;
    }
    if (strcmp(nm, "lista_liberar") == 0 || strcmp(nm, "mem_lista_liberar") == 0) {
        if (c->n_args >= 1 && c->args[0] && c->args[0]->type == NODE_IDENTIFIER) {
            const char *vn = ((IdentifierNode *)c->args[0])->name;
            fprintf(o, "(jb_list_release_in_place(&%s), jb_new_entero(0))", vn);
            return;
        }
        if (g->emit_void_call_as_stmt)
            fputs("jb_warn_aot(\"lista_liberar: se esperaba identificador de lista\")", o);
        else
            fputs("jb_warn_aot_expr(\"lista_liberar: se esperaba identificador de lista\")", o);
        return;
    }
    if ((strcmp(nm, "dividir") == 0 || strcmp(nm, "dividir_texto") == 0) && c->n_args >= 2) {
        fputs("jb_dividir_texto(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if ((strcmp(nm, "str_a_entero") == 0 || strcmp(nm, "convertir_entero") == 0) && c->n_args >= 1) {
        fputs("jb_str_a_entero(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if ((strcmp(nm, "str_a_flotante") == 0 || strcmp(nm, "convertir_flotante") == 0) && c->n_args >= 1) {
        fputs("jb_str_a_flotante(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if ((strcmp(nm, "lista_agregar") == 0 || strcmp(nm, "mem_lista_agregar") == 0) && c->n_args >= 2) {
        if (c->args[0] && c->args[0]->type == NODE_MEMBER_ACCESS) {
            emit_list_op_on_map_path(g, c->args[0], "agregar", c->args[1], NULL);
            return;
        }
        fputs("jb_list_push(&", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "lista_tamano") == 0 && c->n_args >= 1) {
        fputs("jb_list_len(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "lista_obtener") == 0 && c->n_args >= 2) {
        fputs("jb_list_get(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if ((strcmp(nm, "lista_poner") == 0 || strcmp(nm, "mem_lista_poner") == 0) && c->n_args >= 3) {
        if (c->args[0] && c->args[0]->type == NODE_MEMBER_ACCESS) {
            emit_list_op_on_map_path(g, c->args[0], "poner", c->args[1], c->args[2]);
            return;
        }
        fputs("jb_list_set(&", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "mapa_crear") == 0) {
        fputs("jb_new_map()", o);
        return;
    }
    if (strcmp(nm, "mapa_poner") == 0 && c->n_args >= 3) {
        emit_map_put_nested(g, c->args[0], c->args[1], c->args[2]);
        return;
    }
    if (strcmp(nm, "mapa_obtener") == 0 && c->n_args >= 2) {
        fputs("jb_map_get(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "mapa_contiene") == 0 && c->n_args >= 2) {
        fputs("jb_map_has(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "mapa_eliminar") == 0 && c->n_args >= 2) {
        fputs("jb_map_remove(&", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "mapa_tamano") == 0 && c->n_args >= 1) {
        fputs("jb_map_len(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "texto_desde_numero") == 0 && c->n_args >= 1) {
        fputs("jb_texto_desde_numero(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "extraer_subtexto") == 0 && c->n_args == 2) {
        fputs("jb_extraer_subtexto(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", jb_sub(jb_texto_len(", o);
        gen_expr(g, c->args[0]);
        fputs("), ", o);
        gen_expr(g, c->args[1]);
        fputs("))", o);
        return;
    }
    if (strcmp(nm, "extraer_subtexto") == 0 && c->n_args >= 3) {
        fputs("jb_extraer_subtexto(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "contiene_texto") == 0 && c->n_args >= 2) {
        fputs("jb_contiene_texto(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "termina_con") == 0 && c->n_args >= 2) {
        fputs("jb_termina_con(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "longitud") == 0 && c->n_args >= 1) {
        fputs("jb_texto_len(", o);
        gen_expr(g, c->args[0]);
        fputc(')', o);
        return;
    }
    if (strcmp(nm, "recordar") == 0 && c->n_args >= 2) {
        fputs("jb_recordar(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "buscar") == 0 && c->n_args >= 1) {
        fputs("jb_buscar(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "crear_memoria") == 0 && c->n_args >= 1) {
        fputs("jb_crear_memoria(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "abrir_memoria") == 0 && c->n_args >= 1) {
        fputs("jb_abrir_memoria(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if ((strcmp(nm, "consolidar_memoria") == 0 || strcmp(nm, "consolidar") == 0 || strcmp(nm, "dormir") == 0)) {
        if (c->n_args != 0) {
            if (g->emit_void_call_as_stmt)
                fprintf(o, "jb_warn_aot(\"%s: solo sin argumentos en AOT\")", nm ? nm : "");
            else
                fprintf(o, "jb_warn_aot_expr(\"%s: solo sin argumentos en AOT\")", nm ? nm : "");
            return;
        }
        if (g->emit_void_call_as_stmt)
            fputs("jb_consolidar_memoria()", o);
        else
            fputs("jb_consolidar_memoria_expr()", o);
        return;
    }
    if ((strcmp(nm, "buscar_asociados") == 0 || strcmp(nm, "asociados_de") == 0) && c->n_args >= 1) {
        fputs("jb_buscar_asociados(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        if (c->n_args >= 2)
            gen_expr(g, c->args[1]);
        else
            fputs("jb_new_flotante(0)", o);
        fputs(")", o);
        return;
    }
    if ((strcmp(nm, "buscar_asociados_lista") == 0 || strcmp(nm, "asociados_lista_de") == 0) && c->n_args >= 2) {
        fputs("jb_buscar_asociados_lista(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        if (c->n_args >= 3)
            gen_expr(g, c->args[2]);
        else
            fputs("jb_new_nulo()", o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "buscar_asociados_rango") == 0 && c->n_args >= 3) {
        fputs("jb_buscar_asociados_rango(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "obtener_relacionados") == 0 && c->n_args >= 1) {
        fputs("jb_obtener_relacionados(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "buscar_peso") == 0 || strcmp(nm, "tiene_asociacion") == 0 || strcmp(nm, "mem_obtener_fuerza") == 0) {
        if (c->n_args >= 2) {
            fputs("jb_mem_obtener_fuerza(", o);
            gen_expr(g, c->args[0]);
            fputs(", ", o);
            gen_expr(g, c->args[1]);
            fputs(")", o);
            return;
        }
        if (c->n_args >= 1 && strcmp(nm, "buscar_peso") == 0) {
            fputs("jb_buscar_peso(", o);
            gen_expr(g, c->args[0]);
            fputs(")", o);
            return;
        }
    }
    if (strcmp(nm, "reforzar") == 0) {
        if (c->n_args >= 3) {
            fputs("jb_reforzar(", o);
            gen_expr(g, c->args[0]);
            fputs(", ", o);
            gen_expr(g, c->args[1]);
            fputs(", ", o);
            gen_expr(g, c->args[2]);
            fputs(")", o);
            return;
        }
        if (c->n_args >= 2) {
            fputs("jb_reforzar_concepto(", o);
            gen_expr(g, c->args[0]);
            fputs(", ", o);
            gen_expr(g, c->args[1]);
            fputs(")", o);
            return;
        }
    }
    if (strcmp(nm, "penalizar") == 0) {
        if (c->n_args >= 3) {
            fputs("jb_penalizar(", o);
            gen_expr(g, c->args[0]);
            fputs(", ", o);
            gen_expr(g, c->args[1]);
            fputs(", ", o);
            gen_expr(g, c->args[2]);
            fputs(")", o);
            return;
        }
        if (c->n_args >= 2) {
            fputs("jb_penalizar_concepto(", o);
            gen_expr(g, c->args[0]);
            fputs(", ", o);
            gen_expr(g, c->args[1]);
            fputs(")", o);
            return;
        }
    }
    if (strcmp(nm, "olvidar") == 0 && c->n_args >= 1) {
        fputs("jb_penalizar_concepto(", o);
        gen_expr(g, c->args[0]);
        fputs(", jb_new_entero(100))", o);
        return;
    }
    if (strcmp(nm, "olvidar_debiles") == 0 && c->n_args >= 1) {
        fputs("jb_olvidar_debiles(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if ((strcmp(nm, "decae_conexiones") == 0 || strcmp(nm, "decaer_conexiones") == 0)) {
        if (c->n_args >= 1) {
            fputs("jb_decaer_conexiones(", o);
            gen_expr(g, c->args[0]);
            fputs(")", o);
            return;
        }
        fputs("jb_decaer_conexiones(jb_new_flotante(0.95))", o);
        return;
    }
    if (strcmp(nm, "obtener_todos_conceptos") == 0 && c->n_args == 0) {
        fputs("jb_obtener_todos_conceptos()", o);
        return;
    }
    if (strcmp(nm, "pensar") == 0 && c->n_args >= 1) {
        fputs("jb_pensar(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "pensar_respuesta") == 0 && c->n_args >= 1) {
        fputs("jb_pensar_respuesta(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        if (c->n_args >= 2)
            gen_expr(g, c->args[1]);
        else
            fputs("jb_new_flotante(0.5)", o);
        fputs(", ", o);
        if (c->n_args >= 3)
            gen_expr(g, c->args[2]);
        else
            fputs("jb_new_flotante(0.1)", o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "procesar_texto") == 0 && c->n_args >= 1) {
        fputs("jb_procesar_texto(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "comparar_patrones") == 0 && c->n_args >= 2) {
        fputs("jb_comparar_patrones(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "resolver_conflictos") == 0 && c->n_args >= 1) {
        fputs("jb_resolver_conflictos(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "resolver_conflictos_de") == 0 && c->n_args >= 1) {
        fputs("jb_resolver_conflictos(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "propagar_activacion") == 0 && c->n_args >= 1) {
        fputs("jb_propagar_activacion(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        if (c->n_args >= 2)
            gen_expr(g, c->args[1]);
        else
            fputs("jb_new_flotante(0.5)", o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "propagar_activacion_de") == 0 && c->n_args >= 1) {
        fputs("jb_propagar_activacion(", o);
        gen_expr(g, c->args[0]);
        fputs(", jb_new_flotante(0.5)", o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "asociar_secuencia") == 0 && c->n_args == 1) {
        fputs("jb_asociar_secuencia_solo(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "asociar_secuencia") == 0 && c->n_args >= 3) {
        fputs("jb_asociar(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "asociar_secuencia") == 0 && c->n_args >= 2) {
        fputs("jb_asociar_secuencia(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "obtener_secuencia") == 0 && c->n_args >= 1) {
        fputs("jb_obtener_secuencia(", o);
        gen_expr(g, c->args[0]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "pensar_siguiente") == 0 && c->n_args >= 1) {
        fputs("jb_pensar_siguiente(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        if (c->n_args >= 2)
            gen_expr(g, c->args[1]);
        else
            fputs("jb_new_nulo()", o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "pensar_anterior") == 0 && c->n_args >= 1) {
        fputs("jb_pensar_anterior(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        if (c->n_args >= 2)
            gen_expr(g, c->args[1]);
        else
            fputs("jb_new_nulo()", o);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "corregir_secuencia") == 0 && c->n_args >= 3) {
        fputs("jb_corregir_secuencia(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "asociar_relacion") == 0 && c->n_args >= 4) {
        fputs("jb_asociar_relacion_4(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(", ", o);
        gen_expr(g, c->args[3]);
        fputs(")", o);
        return;
    }
    if (strcmp(nm, "asociar_similitud") == 0 || strcmp(nm, "asociar_diferencia") == 0) {
        if (c->n_args >= 3) {
            fprintf(o, "jb_%s(", nm);
            gen_expr(g, c->args[0]);
            fputs(", ", o);
            gen_expr(g, c->args[1]);
            fputs(", ", o);
            gen_expr(g, c->args[2]);
            fputs(")", o);
            return;
        }
    }
    if (strcmp(nm, "asociar_relacion") == 0 && c->n_args >= 3) {
        fputs("jb_asociar_relacion(", o);
        gen_expr(g, c->args[0]);
        fputs(", ", o);
        gen_expr(g, c->args[1]);
        fputs(", ", o);
        gen_expr(g, c->args[2]);
        fputs(")", o);
        return;
    }
    if (is_user_func(g->prog, nm)) {
        fprintf(o, "jbf_%s(", nm);
        for (size_t i = 0; i < c->n_args; i++) {
            if (i) fputs(", ", o);
            gen_expr(g, c->args[i]);
        }
        fputs(")", o);
        return;
    }
    if (g->emit_void_call_as_stmt)
        fprintf(o, "jb_warn_aot(\"llamada AOT no implementada: %s\")", nm);
    else
        fprintf(o, "jb_warn_aot_expr(\"llamada AOT no implementada: %s\")", nm);
}

static void gen_expr(GenCtx *g, ASTNode *node) {
    if (!node) {
        fputs("jb_new_nulo()", g->out);
        return;
    }
    FILE *o = g->out;
    switch (node->type) {
        case NODE_LITERAL: {
            LiteralNode *l = (LiteralNode *)node;
            if (l->type_name && strcmp(l->type_name, "nulo") == 0) {
                fputs("jb_new_nulo()", o);
                break;
            }
            if (l->type_name && strcmp(l->type_name, "texto") == 0) {
                fputs("jb_new_texto(", o);
                emit_c_str(o, l->value.str);
                fputc(')', o);
            } else if (l->type_name && (strcmp(l->type_name, "bool") == 0 || strcmp(l->type_name, "booleano") == 0)) {
                fputs(l->value.i ? "jb_new_bool(true)" : "jb_new_bool(false)", o);
            } else if (l->is_float) {
                fprintf(o, "jb_new_flotante(%g)", l->value.f);
            } else {
                fprintf(o, "jb_new_entero(%lld)", (long long)l->value.i);
            }
            break;
        }
        case NODE_IDENTIFIER: {
            const char *nm = ((IdentifierNode *)node)->name;
            if (nm && strcmp(nm, "resultado") == 0)
                fputs("jb_resultado_global", o);
            else if (nm && strcmp(nm, "padre") == 0 && g->emitting_class)
                fputs("este", o);
            else if (nm && strcmp(nm, "nulo") == 0)
                fputs("jb_new_nulo()", o);
            else
                fprintf(o, "%s", nm ? nm : "_");
            break;
        }
        case NODE_CALL:
            gen_call(g, (CallNode *)node);
            break;
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode *)node;
            const char *op = b->operator ? b->operator : "";
            if (strcmp(op, "+") == 0) {
                if (expr_like_texto(g, b->left) || expr_like_texto(g, b->right)) {
                    fputs("jb_concat(", o);
                    gen_expr(g, b->left);
                    fputs(", ", o);
                    gen_expr(g, b->right);
                    fputs(")", o);
                } else {
                    fputs("jb_add(", o);
                    gen_expr(g, b->left);
                    fputs(", ", o);
                    gen_expr(g, b->right);
                    fputs(")", o);
                }
            } else if (strcmp(op, "-") == 0) {
                fputs("jb_sub(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "*") == 0) {
                fputs("jb_mul(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "/") == 0) {
                fputs("jb_div(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "%") == 0) {
                fputs("jb_mod(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "==") == 0) {
                fputs("jb_eq(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "!=") == 0) {
                fputs("jb_ne(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "<") == 0) {
                fputs("jb_lt(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, ">") == 0) {
                fputs("jb_gt(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "<=") == 0) {
                fputs("jb_le(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, ">=") == 0) {
                fputs("jb_ge(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "y") == 0) {
                fputs("jb_land(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "o") == 0) {
                fputs("jb_lor(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, "<<") == 0) {
                fputs("jb_bit_shl(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else if (strcmp(op, ">>") == 0) {
                fputs("jb_bit_shr(", o);
                gen_expr(g, b->left);
                fputs(", ", o);
                gen_expr(g, b->right);
                fputs(")", o);
            } else {
                fprintf(o, "jb_warn_aot_expr(\"operador binario AOT: %s\")", op);
            }
            break;
        }
        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode *)node;
            const char *op = u->operator ? u->operator : "";
            if (strcmp(op, "-") == 0 || strcmp(op, "negativo") == 0) {
                fputs("jb_sub(jb_new_entero(0), ", o);
                gen_expr(g, u->expression);
                fputs(")", o);
            } else if (strcmp(op, "no") == 0 || strcmp(op, "!") == 0) {
                fputs("jb_not(", o);
                gen_expr(g, u->expression);
                fputs(")", o);
            } else {
                fputs("jb_warn_aot_expr(\"unario AOT\")", o);
            }
            break;
        }
        case NODE_TERNARY: {
            TernaryNode *t = (TernaryNode *)node;
            fputs("(jb_truthy(", o);
            gen_expr(g, t->condition);
            fputs(") ? ", o);
            gen_expr(g, t->true_expr);
            fputs(" : ", o);
            gen_expr(g, t->false_expr);
            fputs(")", o);
            break;
        }
        case NODE_POSTFIX_UPDATE: {
            PostfixUpdateNode *p = (PostfixUpdateNode *)node;
            if (p->target && p->target->type == NODE_IDENTIFIER) {
                const char *vn = ((IdentifierNode *)p->target)->name;
                if (p->delta > 0)
                    fprintf(o, "(jb_assign(&%s, jb_add(%s, jb_new_entero(1))), %s)", vn, vn, vn);
                else
                    fprintf(o, "(jb_assign(&%s, jb_sub(%s, jb_new_entero(1))), %s)", vn, vn, vn);
            } else {
                fputs("jb_new_nulo()", o);
            }
            break;
        }
        case NODE_LIST_LITERAL: {
            ListLiteralNode *ll = (ListLiteralNode *)node;
            size_t ei;
            unsigned sid = ++s_jb_list_lit_seq;
            fprintf(o, "({ jb_var_t _jb_ll_%u = jb_new_list(); ", sid);
            for (ei = 0; ei < ll->n; ei++) {
                fputs("jb_list_push(&_jb_ll_", o);
                fprintf(o, "%u, ", sid);
                gen_expr(g, ll->elements[ei]);
                fputs("); ", o);
            }
            fprintf(o, "_jb_ll_%u; })", sid);
            break;
        }
        case NODE_MAP_LITERAL:
        case NODE_JSON_LITERAL:
            gen_json_from_map_literal(g, (MapLiteralNode *)node);
            break;
        case NODE_INDEX_ACCESS: {
            IndexAccessNode *ia = (IndexAccessNode *)node;
            fputs("jb_list_get(", o);
            gen_expr(g, ia->target);
            fputs(", ", o);
            gen_expr(g, ia->index);
            fputs(")", o);
            break;
        }
        case NODE_MEMBER_ACCESS: {
            MemberAccessNode *mm = (MemberAccessNode *)node;
            fputs("jb_member_get(", o);
            gen_expr(g, mm->target);
            fputs(", ", o);
            emit_c_str(o, mm->member);
            fputs(")", o);
            break;
        }
        default:
            fprintf(o, "jb_warn_aot_expr(\"expr AOT tipo=%d\")", (int)node->type);
            break;
    }
}

static void gen_lvalue_addr(GenCtx *g, ASTNode *target) {
    if (target && target->type == NODE_IDENTIFIER) {
        const char *nm = ((IdentifierNode *)target)->name;
        if (nm && strcmp(nm, "resultado") == 0)
            fputs("&jb_resultado_global", g->out);
        else
            fprintf(g->out, "&%s", nm ? nm : "_");
    } else
        fputs("(void*)0 /* lvalue invalido */", g->out);
}

static void gen_stmt(GenCtx *g, ASTNode *node);

static void gen_block(GenCtx *g, BlockNode *b) {
    if (!b) return;
    for (size_t i = 0; i < b->n; i++)
        gen_stmt(g, b->statements[i]);
}

static void gen_stmt(GenCtx *g, ASTNode *node) {
    if (!node) return;
    FILE *o = g->out;
    switch (node->type) {
        case NODE_BLOCK:
            gen_block(g, (BlockNode *)node);
            break;
        case NODE_PRINT: {
            ind(g);
            fputs("jb_imprimir(", o);
            gen_expr(g, ((PrintNode *)node)->expression);
            fputs(");\n", o);
            break;
        }
        case NODE_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode *)node;
            ind(g);
            fprintf(o, "jb_var_t %s = ", v->name);
            /* `texto x = y` debe clonar: la copia estructural de jb_var_t comparte el buffer de y. */
            if (v->value && v->value->type == NODE_IDENTIFIER) {
                fputs("jb_var_clone(", o);
                gen_expr(g, v->value);
                fputs(")", o);
            } else if (v->value)
                gen_expr(g, v->value);
            else if (v->type_name && strcmp(v->type_name, "mat4") == 0)
                fputs("jb_new_mat4_zero()", o);
            else if (v->type_name && find_struct_def(g->prog, v->type_name))
                fputs("jb_new_map()", o);
            else
                fputs("jb_new_nulo()", o);
            fputs(";\n", o);
            if (v->name && v->type_name) push_var_type(g, v->name, v->type_name);
            break;
        }
        case NODE_ASSIGNMENT: {
            AssignmentNode *a = (AssignmentNode *)node;
            if (a->target && a->target->type == NODE_MEMBER_ACCESS) {
                MemberAccessNode *mm = (MemberAccessNode *)a->target;
                ind(g);
                emit_nested_map_assign_leaf(g, mm->target, mm->member, a->expression);
                fputs(";\n", o);
            } else {
                ind(g);
                fputs("jb_assign(", o);
                gen_lvalue_addr(g, a->target);
                fputs(", ", o);
                gen_expr(g, a->expression);
                fputs(");\n", o);
            }
            break;
        }
        case NODE_INDEX_ASSIGNMENT: {
            IndexAssignmentNode *ia = (IndexAssignmentNode *)node;
            ind(g);
            if (ia->target && ia->target->type == NODE_MEMBER_ACCESS) {
                emit_list_index_set_on_map_path(g, ia->target, ia->index, ia->expression);
                fputs(";\n", o);
            } else {
                fputs("jb_list_set(&", o);
                gen_expr(g, ia->target);
                fputs(", ", o);
                gen_expr(g, ia->index);
                fputs(", ", o);
                gen_expr(g, ia->expression);
                fputs(");\n", o);
            }
            break;
        }
        case NODE_CALL: {
            ind(g);
            g->emit_void_call_as_stmt = 1;
            gen_call(g, (CallNode *)node);
            g->emit_void_call_as_stmt = 0;
            fputs(";\n", o);
            break;
        }
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode *)node;
            ind(g);
            fputs("{\n", o);
            g->indent += 4;
            ind(g);
            fputs("jb_var_t __sel = ", o);
            gen_expr(g, sn->selector);
            fputs(";\n", o);
            for (size_t ci = 0; ci < sn->n_cases; ci++) {
                SelectCase *sc = &sn->cases[ci];
                ind(g);
                fputs(ci == 0 ? "if (jb_truthy(" : "else if (jb_truthy(", o);
                if (sc->is_range && sc->n_values > 0 && sc->range_end) {
                    fputs("jb_land(jb_ge(__sel, ", o);
                    gen_expr(g, sc->values[0]);
                    fputs("), jb_le(__sel, ", o);
                    gen_expr(g, sc->range_end);
                    fputs("))", o);
                } else if (sc->n_values == 0)
                    fputs("jb_new_bool(0)", o);
                else if (sc->n_values == 1) {
                    fputs("jb_eq(__sel, ", o);
                    gen_expr(g, sc->values[0]);
                    fputs(")", o);
                } else {
                    for (size_t k = 1; k < sc->n_values; k++) fputs("jb_lor(", o);
                    fputs("jb_eq(__sel, ", o);
                    gen_expr(g, sc->values[0]);
                    fputs(")", o);
                    for (size_t vi = 1; vi < sc->n_values; vi++) {
                        fputs(", jb_eq(__sel, ", o);
                        gen_expr(g, sc->values[vi]);
                        fputs(")", o);
                    }
                    for (size_t k = 1; k < sc->n_values; k++) fputc(')', o);
                }
                fputs(")) {\n", o);
                g->indent += 4;
                if (sc->body && sc->body->type == NODE_BLOCK)
                    gen_block(g, (BlockNode *)sc->body);
                else if (sc->body)
                    gen_stmt(g, sc->body);
                g->indent -= 4;
                ind(g);
                fputs("}\n", o);
            }
            if (sn->default_body) {
                ind(g);
                fputs("else {\n", o);
                g->indent += 4;
                if (sn->default_body->type == NODE_BLOCK)
                    gen_block(g, (BlockNode *)sn->default_body);
                else
                    gen_stmt(g, sn->default_body);
                g->indent -= 4;
                ind(g);
                fputs("}\n", o);
            }
            g->indent -= 4;
            ind(g);
            fputs("}\n", o);
            break;
        }
        case NODE_RECORDAR: {
            RecordarNode *rn = (RecordarNode *)node;
            ind(g);
            if (rn->value) {
                fputs("jb_recordar(", o);
                gen_expr(g, rn->key);
                fputs(", ", o);
                gen_expr(g, rn->value);
                fputs(");\n", o);
            } else {
                /* VM: recordar sin valor -> reforzar peso del nodo (magnitud implicita 100 = 1.0). */
                fputs("jb_aprender_concepto(", o);
                gen_expr(g, rn->key);
                fputs(", jb_new_flotante(1.0));\n", o);
            }
            break;
        }
        case NODE_CREAR_MEMORIA: {
            CrearMemoriaNode *cm = (CrearMemoriaNode *)node;
            ind(g);
            fputs("jb_crear_memoria(", o);
            gen_expr(g, cm->filename);
            fputs(");\n", o);
            break;
        }
        case NODE_RESPONDER: {
            ResponderNode *rn = (ResponderNode *)node;
            ind(g);
            fputs("jb_imprimir_id(", o);
            gen_expr(g, rn->message);
            fputs(");\n", o);
            break;
        }
        case NODE_BUSCAR_PESO: {
            BuscarPesoNode *bn = (BuscarPesoNode *)node;
            ind(g);
            fputs("(void)jb_buscar_peso(", o);
            gen_expr(g, bn->concept);
            fputs(");\n", o);
            break;
        }
        case NODE_DEFINE_CONCEPTO: {
            DefineConceptoNode *dn = (DefineConceptoNode *)node;
            ind(g);
            fputs("jb_define_concepto(", o);
            gen_expr(g, dn->concepto);
            fputs(", ", o);
            gen_expr(g, dn->descripcion);
            fputs(");\n", o);
            break;
        }
        case NODE_ASOCIAR: {
            AsociarNode *an = (AsociarNode *)node;
            ind(g);
            fputs("jb_asociar(", o);
            gen_expr(g, an->concept1);
            fputs(", ", o);
            gen_expr(g, an->concept2);
            fputs(", ", o);
            if (an->weight)
                gen_expr(g, an->weight);
            else
                fputs("jb_new_flotante(0.9)", o);
            fputs(");\n", o);
            break;
        }
        case NODE_APRENDER: {
            AprenderNode *an = (AprenderNode *)node;
            ind(g);
            fputs("jb_aprender_concepto(", o);
            gen_expr(g, an->concept);
            fputs(", ", o);
            if (an->weight) gen_expr(g, an->weight);
            else fputs("jb_new_flotante(0.1)", o);
            fputs(");\n", o);
            break;
        }
        case NODE_CERRAR_MEMORIA:
            ind(g);
            fputs("jb_cerrar_memoria();\n", o);
            break;
        case NODE_IF: {
            IfNode *in = (IfNode *)node;
            ind(g);
            fputs("if (jb_truthy(", o);
            gen_expr(g, in->condition);
            fputs(")) {\n", o);
            g->indent += 4;
            if (in->body && in->body->type == NODE_BLOCK)
                gen_block(g, (BlockNode *)in->body);
            else
                gen_stmt(g, in->body);
            g->indent -= 4;
            ind(g);
            fputs("}", o);
            if (in->else_body) {
                fputs(" else {\n", o);
                g->indent += 4;
                if (in->else_body->type == NODE_BLOCK)
                    gen_block(g, (BlockNode *)in->else_body);
                else
                    gen_stmt(g, in->else_body);
                g->indent -= 4;
                ind(g);
                fputs("}", o);
            }
            fputs("\n", o);
            break;
        }
        case NODE_WHILE: {
            WhileNode *w = (WhileNode *)node;
            ind(g);
            fputs("while (jb_truthy(", o);
            gen_expr(g, w->condition);
            fputs(")) {\n", o);
            g->indent += 4;
            if (w->body && w->body->type == NODE_BLOCK)
                gen_block(g, (BlockNode *)w->body);
            else
                gen_stmt(g, w->body);
            g->indent -= 4;
            ind(g);
            fputs("}\n", o);
            break;
        }
        case NODE_FOR: {
            ForNode *fn = (ForNode *)node;
            ind(g);
            fputs("for (", o);
            if (fn->init) gen_stmt(g, fn->init);
            else fputs("; ", o);
            if (fn->condition) {
                gen_expr(g, fn->condition);
            } else fputs("1", o);
            fputs("; ", o);
            if (fn->step) gen_stmt(g, fn->step);
            else fputs(" ", o);
            fputs(") {\n", o);
            g->indent += 4;
            if (fn->body && fn->body->type == NODE_BLOCK)
                gen_block(g, (BlockNode *)fn->body);
            else
                gen_stmt(g, fn->body);
            g->indent -= 4;
            ind(g);
            fputs("}\n", o);
            break;
        }
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode *)node;
            const char *itn = fe->iter_name ? fe->iter_name : "_it";
            const char *idx_nm = (fe->index_name && fe->index_name[0]) ? fe->index_name
                                   : (fe->key_name && fe->key_name[0]) ? fe->key_name : NULL;
            ind(g);
            fputs("{\n", o);
            g->indent += 4;
            if (fe->iter_type && strcmp(fe->iter_type, "caracter") == 0) {
                ind(g);
                fputs("jb_var_t __s = ", o);
                gen_expr(g, fe->collection);
                fputs(";\n", o);
                ind(g);
                fputs("int64_t __slen = jb_texto_len(__s).u.i64;\n", o);
                ind(g);
                fputs("for (int64_t __i = 0; __i < __slen; __i++) {\n", o);
                g->indent += 4;
                ind(g);
                fprintf(o, "jb_var_t %s = jb_str_extraer_caracter(__s, jb_new_entero(__i));\n", itn);
                if (idx_nm) {
                    ind(g);
                    fprintf(o, "jb_var_t %s = jb_new_entero(__i);\n", idx_nm);
                }
                if (fe->body && fe->body->type == NODE_BLOCK)
                    gen_block(g, (BlockNode *)fe->body);
                else
                    gen_stmt(g, fe->body);
                g->indent -= 4;
                ind(g);
                fputs("}\n", o);
            } else if (foreach_collection_is_mapa(g, fe->collection)) {
                ind(g);
                fputs("jb_var_t __map = ", o);
                gen_expr(g, fe->collection);
                fputs(";\n", o);
                ind(g);
                fputs("jb_var_t __maplen = jb_map_len(__map);\n", o);
                ind(g);
                fputs("for (int64_t __i = 0; __i < __maplen.u.i64; __i++) {\n", o);
                g->indent += 4;
                ind(g);
                fprintf(o, "jb_var_t %s = jb_map_val_at(__map, jb_new_entero(__i));\n", itn);
                if (fe->key_name && fe->key_name[0]) {
                    ind(g);
                    fprintf(o, "jb_var_t %s = jb_map_key_at(__map, jb_new_entero(__i));\n", fe->key_name);
                } else if (fe->index_name && fe->index_name[0]) {
                    ind(g);
                    fprintf(o, "jb_var_t %s = jb_new_entero(__i);\n", fe->index_name);
                }
                if (fe->body && fe->body->type == NODE_BLOCK)
                    gen_block(g, (BlockNode *)fe->body);
                else
                    gen_stmt(g, fe->body);
                g->indent -= 4;
                ind(g);
                fputs("}\n", o);
            } else {
                ind(g);
                fputs("jb_var_t __coll = ", o);
                gen_expr(g, fe->collection);
                fputs(";\n", o);
                ind(g);
                fputs("jb_var_t __len = jb_list_len(__coll);\n", o);
                ind(g);
                fputs("for (int64_t __i = 0; __i < __len.u.i64; __i++) {\n", o);
                g->indent += 4;
                ind(g);
                fprintf(o, "jb_var_t %s = jb_list_get(__coll, jb_new_entero(__i));\n", itn);
                if (idx_nm) {
                    ind(g);
                    fprintf(
                        o,
                        "jb_var_t %s __attribute__((__unused__)) = jb_new_entero(__i);\n",
                        idx_nm);
                }
                if (fe->body && fe->body->type == NODE_BLOCK)
                    gen_block(g, (BlockNode *)fe->body);
                else
                    gen_stmt(g, fe->body);
                g->indent -= 4;
                ind(g);
                fputs("}\n", o);
            }
            g->indent -= 4;
            ind(g);
            fputs("}\n", o);
            break;
        }
        case NODE_RETURN: {
            ReturnNode *r = (ReturnNode *)node;
            ind(g);
            fputs("return ", o);
            if (r->expression) gen_expr(g, r->expression);
            else fputs("jb_new_nulo()", o);
            fputs(";\n", o);
            break;
        }
        case NODE_BREAK:
            ind(g);
            fputs("break;\n", o);
            break;
        case NODE_CONTINUE:
            ind(g);
            fputs("continue;\n", o);
            break;
        case NODE_INPUT: {
            InputNode *in = (InputNode *)node;
            ind(g);
            fputs("jb_ingresar_texto(&", o);
            fprintf(o, "%s", in->variable ? in->variable : "_tmp");
            fputs(");\n", o);
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode *)node;
            ind(g);
            fputs("{\n", o);
            g->indent += 4;
            ind(g);
            fputs("jb_try_depth++;\n", o);
            ind(g);
            fputs("if (setjmp(jb_try_stack[jb_try_depth - 1]) == 0) {\n", o);
            g->indent += 4;
            if (tn->try_body && tn->try_body->type == NODE_BLOCK)
                gen_block(g, (BlockNode *)tn->try_body);
            else
                gen_stmt(g, tn->try_body);
            g->indent -= 4;
            ind(g);
            fputs("jb_try_depth--;\n", o);
            ind(g);
            fputs("} else {\n", o);
            g->indent += 4;
            ind(g);
            if (tn->catch_var && tn->catch_var[0]) {
                fprintf(o, "jb_var_t %s = jb_var_clone(g_last_throw);\n", tn->catch_var);
            }
            if (tn->catch_body && tn->catch_body->type == NODE_BLOCK)
                gen_block(g, (BlockNode *)tn->catch_body);
            else if (tn->catch_body)
                gen_stmt(g, tn->catch_body);
            ind(g);
            fputs("jb_try_depth--;\n", o);
            g->indent -= 4;
            ind(g);
            fputs("}\n", o);
            g->indent -= 4;
            ind(g);
            fputs("}\n", o);
            break;
        }
        case NODE_THROW:
            ind(g);
            fputs("jb_throw_val(", o);
            gen_expr(g, ((ThrowNode *)node)->expression);
            fputs(");\n", o);
            break;
        case NODE_ACTIVAR_MODULO:
        case NODE_BIBLIOTECA:
            ind(g);
            fputs("jb_warn_aot(\"usar/biblioteca: fusion de modulos no soportada en AOT; usar un solo .jasb o la VM\");\n", o);
            break;
        case NODE_STRUCT_DEF:
            break;
        case NODE_FUNCTION:
            /* top-level only */
            break;
        default:
            ind(g);
            fprintf(o, "jb_warn_aot(\"sentencia AOT tipo=%d\");\n", (int)node->type);
            break;
    }
}

static void gen_class_method(GenCtx *g, StructDefNode *sd, FunctionNode *f) {
    FILE *o = g->out;
    const char *cls = sd ? sd->name : NULL;
    if (!cls || !f || !f->name) return;
    int saved_ty = g->n_var_types;
    g->emitting_class = cls;
    g->emitting_struct = sd;
    fprintf(o, "static jb_var_t jbf_%s_%s(jb_var_t este", cls, f->name);
    for (size_t i = 0; i < f->n_params; i++) {
        fputs(", jb_var_t ", o);
        VarDeclNode *pv = (VarDeclNode *)f->params[i];
        fprintf(o, "%s", pv->name ? pv->name : "_p");
    }
    fputs(") {\n", o);
    for (size_t i = 0; i < f->n_params; i++) {
        VarDeclNode *pv = (VarDeclNode *)f->params[i];
        if (pv && pv->name && pv->type_name) push_var_type(g, pv->name, pv->type_name);
    }
    g->indent = 4;
    if (f->body && f->body->type == NODE_BLOCK)
        gen_block(g, (BlockNode *)f->body);
    else
        gen_stmt(g, f->body);
    if (!ast_body_ends_with_return(f->body))
        fputs("    return jb_new_nulo();\n", o);
    fputs("}\n\n", o);
    g->n_var_types = saved_ty;
    g->indent = 0;
    g->emitting_class = NULL;
    g->emitting_struct = NULL;
}

static void gen_func(GenCtx *g, FunctionNode *f) {
    FILE *o = g->out;
    int saved_ty = g->n_var_types;
    fprintf(o, "static jb_var_t jbf_%s(", f->name ? f->name : "anon");
    for (size_t i = 0; i < f->n_params; i++) {
        VarDeclNode *p = (VarDeclNode *)f->params[i];
        if (i) fputs(", ", o);
        fprintf(o, "jb_var_t %s", p->name ? p->name : "_p");
    }
    fputs(") {\n", o);
    for (size_t i = 0; i < f->n_params; i++) {
        VarDeclNode *p = (VarDeclNode *)f->params[i];
        if (p && p->name && p->type_name) push_var_type(g, p->name, p->type_name);
    }
    g->indent = 4;
    if (f->body && f->body->type == NODE_BLOCK)
        gen_block(g, (BlockNode *)f->body);
    else
        gen_stmt(g, f->body);
    if (!ast_body_ends_with_return(f->body))
        fputs("    return jb_new_nulo();\n", o);
    fputs("}\n\n", o);
    g->n_var_types = saved_ty;
    g->indent = 0;
}

static void gen_program(GenCtx *g, ProgramNode *p) {
    FILE *o = g->out;
    fputs("#include \"jasboot_rt.h\"\n#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n#include <setjmp.h>\n\n", o);
    fputs("#ifdef __GNUC__\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wunused-function\"\n#endif\n", o);
    fputs("#ifdef _MSC_VER\n#pragma warning(push)\n#pragma warning(disable:4505)\n#endif\n\n", o);

    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *gl = p->globals[i];
        if (!gl || gl->type != NODE_STRUCT_DEF) continue;
        StructDefNode *sd = (StructDefNode *)gl;
        if (!sd->is_clase || !sd->name) continue;
        for (size_t m = 0; m < sd->n_methods; m++) {
            FunctionNode *fn = (FunctionNode *)sd->methods[m];
            if (!fn || !fn->name) continue;
            fprintf(o, "static jb_var_t jbf_%s_%s(jb_var_t este", sd->name, fn->name);
            for (size_t j = 0; j < fn->n_params; j++) {
                fputs(", jb_var_t ", o);
                VarDeclNode *pv = (VarDeclNode *)fn->params[j];
                fprintf(o, "%s", pv->name ? pv->name : "_p");
            }
            fputs(");\n", o);
        }
    }

    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *f = (FunctionNode *)p->functions[i];
        fprintf(o, "static jb_var_t jbf_%s(", f->name ? f->name : "anon");
        for (size_t j = 0; j < f->n_params; j++) {
            VarDeclNode *pv = (VarDeclNode *)f->params[j];
            if (j) fputs(", ", o);
            fprintf(o, "jb_var_t %s", pv->name ? pv->name : "_p");
        }
        fputs(");\n", o);
    }

    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *gl = p->globals[i];
        if (gl && gl->type == NODE_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)gl;
            fprintf(o, "static jb_var_t %s;\n", v->name ? v->name : "_g");
        }
    }
    fputs("\n", o);

    for (size_t i = 0; i < p->n_funcs; i++)
        gen_func(g, (FunctionNode *)p->functions[i]);

    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *gl = p->globals[i];
        if (!gl || gl->type != NODE_STRUCT_DEF) continue;
        StructDefNode *sd = (StructDefNode *)gl;
        if (!sd->is_clase) continue;
        for (size_t m = 0; m < sd->n_methods; m++) {
            FunctionNode *fn = (FunctionNode *)sd->methods[m];
            if (fn) gen_class_method(g, sd, fn);
        }
    }

    fputs("#ifdef __GNUC__\n#pragma GCC diagnostic pop\n#endif\n", o);
    fputs("#ifdef _MSC_VER\n#pragma warning(pop)\n#endif\n\n", o);
    fputs("int main(int argc, char **argv) {\n    jb_init();\n    jb_set_argv(argc, argv);\n", o);
    g->indent = 4;
    GenCtx gm = *g;
    gm.indent = 4;
    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *gl = p->globals[i];
        if (gl && gl->type == NODE_VAR_DECL) {
            VarDeclNode *v = (VarDeclNode *)gl;
            ind(&gm);
            fprintf(o, "%s = ", v->name ? v->name : "_g");
            if (v->value) gen_expr(&gm, v->value);
            else fputs("jb_new_nulo()", o);
            fputs(";\n", o);
        }
    }
    if (p->main_block && p->main_block->type == NODE_BLOCK)
        gen_block(&gm, (BlockNode *)p->main_block);
    fputs("    jb_cleanup();\n    return 0;\n}\n", o);
}

void jbc_generate_c(ASTNode *node, FILE *out) {
    jbc_generate_c_opts(node, out, NULL, 0);
}

void jbc_generate_c_opts(ASTNode *node, FILE *out, const char *out_path, int strict) {
    (void)out_path;
    (void)strict;
    if (!node || node->type != NODE_PROGRAM) {
        fprintf(out, "#error \"Se esperaba nodo NODE_PROGRAM\"\n");
        return;
    }
    GenCtx g = { out, (ProgramNode *)node, 0 };
    gen_program(&g, (ProgramNode *)node);
}
