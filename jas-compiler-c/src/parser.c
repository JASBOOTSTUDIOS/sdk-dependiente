/* Parser - construye AST desde tokens */

#include "parser.h"
#include "lexer.h"
#include "keywords.h"
#include "sistema_llamadas.h"
#include "diagnostic.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ERROR_MAX 768

static void format_token_desc(const Token *t, char *buf, size_t buflen) {
    if (!t || buflen < 8) {
        if (buflen) buf[0] = '\0';
        return;
    }
    switch (t->type) {
    case TOK_EOF:
        snprintf(buf, buflen, "fin de archivo");
        break;
    case TOK_NUMBER:
        if (t->is_float)
            snprintf(buf, buflen, "literal numerico (%g)", t->value.f);
        else
            snprintf(buf, buflen, "literal numerico (%lld)", (long long)t->value.i);
        break;
    case TOK_STRING: {
        const char *s = t->value.str ? t->value.str : "";
        size_t L = strlen(s);
        if (L <= 48)
            snprintf(buf, buflen, "literal de texto \"%s\"", s);
        else
            snprintf(buf, buflen, "literal de texto (primeros 48 caracteres) \"%.*s...\"", 48, s);
        break;
    }
    case TOK_CONCEPT: {
        const char *s = t->value.str ? t->value.str : "";
        snprintf(buf, buflen, "concepto '%s'", s);
        break;
    }
    case TOK_KEYWORD:
        snprintf(buf, buflen, "palabra clave `%s`", t->value.str ? t->value.str : "");
        break;
    case TOK_IDENTIFIER:
        snprintf(buf, buflen, "identificador `%s`", t->value.str ? t->value.str : "");
        break;
    case TOK_OPERATOR: {
        const char *o = t->value.str ? t->value.str : "";
        if (strcmp(o, "\n") == 0)
            snprintf(buf, buflen, "salto de linea (operador)");
        else
            snprintf(buf, buflen, "operador `%s`", o);
        break;
    }
    default:
        snprintf(buf, buflen, "(token desconocido)");
        break;
    }
}

static void set_error_vc(Parser *p, int loc_line, int loc_col, const char *fmt, va_list ap) {
    char head[3072];
    vsnprintf(head, sizeof head, fmt, ap);
    char *final;
    if (loc_line > 0 && loc_col > 0 && p->source_text)
        final = diag_attach_snippet(p->source_text, loc_line, loc_col, head);
    else
        final = strdup(head);
    if (!final) final = strdup(head);
    if (p->last_error) free(p->last_error);
    p->last_error = final;
}

static void set_error_at(Parser *p, int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    set_error_vc(p, line, col, fmt, ap);
    va_end(ap);
}

static void set_error_here(Parser *p, const Token *where, const char *msg) {
    int line = where ? where->line : 0;
    int col = where ? where->column : 0;
    if (p->source_path && p->source_path[0])
        set_error_at(p, line, col, "Archivo %s, linea %d, columna %d: %s", p->source_path, line, col, msg);
    else
        set_error_at(p, line, col, "linea %d, columna %d: %s", line, col, msg);
}

/* Tras `=>`, aparece `)` sin expresion ni bloque (p. ej. ((x) => )(1)). */
static void set_error_lambda_empty_body_after_arrow(Parser *p, const Token *where) {
    if (p->last_error || !where) return;
    set_error_here(p, where,
        "Falta el cuerpo de la funcion flecha despues de `=>`: debe haber una expresion o un bloque `{ ... }` antes del `)` "
        "que cierra la lambda. Ejemplo valido: ((x) => x)(1). Invalido: ((x) => )(1).");
}

/* IIFE: marcar '(' de la lista de parametros y '(' de la lista de argumentos. */
static void set_error_lambda_arity_iife(Parser *p, const Token *param_list_open, const Token *args_list_open) {
    const char *msg = "funcion flecha: cantidad de argumentos no coincide con los parametros.";
    if (!param_list_open || !args_list_open) {
        const Token *fb = (args_list_open && args_list_open->line > 0) ? args_list_open : param_list_open;
        if (fb)
            set_error_here(p, fb, msg);
        return;
    }
    int pl = param_list_open->line, pc = param_list_open->column;
    int al = args_list_open->line, ac = args_list_open->column;
    char head[3072];
    if (p->source_path && p->source_path[0]) {
        if (pl == al)
            snprintf(head, sizeof head,
                     "Archivo %s, linea %d: %s\n"
                     "  Declaracion de parametros (columna %d); argumentos en la llamada (columna %d).",
                     p->source_path, pl, msg, pc, ac);
        else
            snprintf(head, sizeof head,
                     "Archivo %s: %s\n"
                     "  Parametros: linea %d, columna %d. Argumentos: linea %d, columna %d.",
                     p->source_path, msg, pl, pc, al, ac);
    } else {
        if (pl == al)
            snprintf(head, sizeof head,
                     "linea %d: %s\n  Parametros: columna %d; argumentos: columna %d.", pl, msg, pc, ac);
        else
            snprintf(head, sizeof head,
                     "%s\n  Parametros: linea %d, col %d. Argumentos: linea %d, col %d.", msg, pl, pc, al, ac);
    }
    char *final = diag_attach_snippet_two(p->source_text, pl, pc, al, ac, head);
    if (!final)
        final = strdup(head);
    if (!final)
        return;
    if (p->last_error)
        free(p->last_error);
    p->last_error = final;
}

static const Token *peek(const Parser *p, size_t offset) {
    if (!p || !p->tokens) return NULL;
    size_t i = p->pos + offset;
    if (i >= token_vec_size(p->tokens)) return NULL;
    return token_vec_get(p->tokens, i);
}

static const Token *advance(Parser *p) {
    const Token *t = token_vec_get(p->tokens, p->pos);
    if (p->pos < token_vec_size(p->tokens))
        p->pos++;
    return t;
}

static int match(Parser *p, TokenType tt, const char *val) {
    const Token *t = peek(p, 0);
    if (!t) return 0;
    if (t->type != tt) return 0;
    if (val && t->value.str && strcmp(t->value.str, val) != 0) return 0;
    advance(p);
    return 1;
}

static int expect(Parser *p, TokenType tt, const char *val, const char *msg) {
    if (match(p, tt, val)) return 1;
    const Token *t = peek(p, 0);
    int line = t ? t->line : 0;
    int col = t ? t->column : 0;
    char got[192];
    format_token_desc(t, got, sizeof got);
    const char *expl = msg ? msg : "token inesperado";
    if (p->source_path && p->source_path[0])
        set_error_at(p, line, col, "Archivo %s, linea %d, columna %d: error de sintaxis: %s - se encontro %s.",
                  p->source_path, line, col, expl, got);
    else
        set_error_at(p, line, col, "linea %d, columna %d: error de sintaxis: %s - se encontro %s.",
                  line, col, expl, got);
    return 0;
}

static const char *tok_str(const Token *t) {
    if (!t) return "EOF";
    if (t->type == TOK_STRING || t->type == TOK_IDENTIFIER || t->type == TOK_KEYWORD || t->type == TOK_OPERATOR)
        return t->value.str ? t->value.str : "";
    if (t->type == TOK_NUMBER) {
        static char buf[64];
        if (t->is_float) snprintf(buf, sizeof buf, "%g", t->value.f);
        else snprintf(buf, sizeof buf, "%lld", (long long)t->value.i);
        return buf;
    }
    return "?";
}

static char *strdup_safe(const char *s) {
    return s ? strdup(s) : NULL;
}

/* Palabras que el lexer marca como TOK_KEYWORD pero se permiten como nombre de variable/identificador
   por compatibilidad historica (p. ej. "a" en formulas) o constructores vec/mat. Las llamadas sistema
   se escriben como identificador-primario en parse_primary. */
static int keyword_ok_as_user_identifier(const char *s) {
    if (!s) return 0;
    size_t L = strlen(s);
    return (strcmp(s, "vec2") == 0 || strcmp(s, "vec3") == 0 ||
            strcmp(s, "vec4") == 0 || strcmp(s, "mat4") == 0 || strcmp(s, "mat3") == 0 ||
            is_sistema_llamada(s, L));
}

/* Variable, parametro, constante, campo: no usar KEYWORDS salvo excepciones anteriores. */
static int validate_user_defined_name_tok(Parser *p, const Token *tok) {
    if (!tok || !tok->value.str) return 1;
    const char *s = tok->value.str;
    size_t L = strlen(s);
    if (is_keyword(s, L) && !keyword_ok_as_user_identifier(s)) {
        if (p->source_path && p->source_path[0])
            set_error_at(p, tok->line, tok->column,
                      "Archivo %s, linea %d, columna %d: '%s' es palabra reservada del lenguaje: aqui se esperaba un "
                      "nombre elegido por usted (variable, parametro, campo o nombre de una funcion suya), no esa palabra clave.",
                      p->source_path, tok->line, tok->column, s);
        else
            set_error_at(p, tok->line, tok->column,
                      "linea %d, columna %d: '%s' es palabra reservada del lenguaje: aqui se esperaba un nombre elegido "
                      "por usted (variable, parametro, campo o nombre de una funcion suya), no esa palabra clave.",
                      tok->line, tok->column, s);
        return 0;
    }
    return 1;
}

static int is_decl_type_token(const Token *t) {
    if (!t || !t->value.str) return 0;
    const char *s = t->value.str;
    return (strcmp(s, "entero") == 0 || strcmp(s, "texto") == 0 ||
            strcmp(s, "flotante") == 0 || strcmp(s, "caracter") == 0 ||
            strcmp(s, "bool") == 0 || strcmp(s, "lista") == 0 ||
            strcmp(s, "mapa") == 0 || strcmp(s, "u32") == 0 ||
            strcmp(s, "u64") == 0 || strcmp(s, "u8") == 0 ||
            strcmp(s, "byte") == 0 || strcmp(s, "vec2") == 0 ||
            strcmp(s, "vec3") == 0 || strcmp(s, "vec4") == 0 ||
            strcmp(s, "mat4") == 0 || strcmp(s, "mat3") == 0 ||
            strcmp(s, "funcion") == 0);
}

/* Tras consumir el token `lista`: si sigue `<T>`, lo parsea (obligatorio cerrar bien). Si no hay `<`, devuelve NULL. */
static char *parse_optional_lista_element_type(Parser *p) {
    const Token *nx = peek(p, 0);
    if (!nx || nx->type != TOK_OPERATOR || !nx->value.str || strcmp(nx->value.str, "<") != 0)
        return NULL;
    if (!match(p, TOK_OPERATOR, "<"))
        return NULL;
    const Token *inner = peek(p, 0);
    if (!inner || inner->type != TOK_KEYWORD || !inner->value.str) {
        set_error_here(p, inner ? inner : peek(p, 0),
                       "En `lista<T>` se esperaba un tipo T (entero, flotante, texto, bool) despues de '<'.");
        return NULL;
    }
    static const char *ok[] = {"entero", "flotante", "texto", "bool", NULL};
    int good = 0;
    for (int i = 0; ok[i]; i++)
        if (strcmp(inner->value.str, ok[i]) == 0) good = 1;
    if (!good) {
        set_error_here(p, inner, "lista<T>: T debe ser entero, flotante, texto o bool.");
        return NULL;
    }
    char *s = strdup(inner->value.str);
    if (!s) return NULL;
    advance(p);
    if (!expect(p, TOK_OPERATOR, ">", "lista<T> debe cerrarse con '>'")) {
        free(s);
        return NULL;
    }
    return s;
}

/* Helpers para crear nodos */
static ASTNode *make_literal_int(int64_t v) {
    LiteralNode *n = calloc(1, sizeof(LiteralNode));
    if (!n) return NULL;
    n->base.type = NODE_LITERAL;
    n->type_name = strdup("entero");
    n->value.i = v;
    n->is_float = 0;
    return (ASTNode*)n;
}

static ASTNode *make_literal_float(double v) {
    LiteralNode *n = calloc(1, sizeof(LiteralNode));
    if (!n) return NULL;
    n->base.type = NODE_LITERAL;
    n->type_name = strdup("flotante");
    n->value.f = v;
    n->is_float = 1;
    return (ASTNode*)n;
}

static ASTNode *make_literal_str(const char *s) {
    LiteralNode *n = calloc(1, sizeof(LiteralNode));
    if (!n) return NULL;
    n->base.type = NODE_LITERAL;
    n->type_name = strdup("texto");
    n->value.str = strdup_safe(s);
    n->is_float = 0;
    return (ASTNode*)n;
}

static ASTNode *make_literal_char(int c) {
    LiteralNode *n = calloc(1, sizeof(LiteralNode));
    if (!n) return NULL;
    n->base.type = NODE_LITERAL;
    n->type_name = strdup("caracter");
    n->value.i = (unsigned char)c;
    n->is_float = 0;
    return (ASTNode*)n;
}

static ASTNode *make_identifier(const char *s, int line, int col) {
    IdentifierNode *n = calloc(1, sizeof(IdentifierNode));
    if (!n) return NULL;
    n->base.type = NODE_IDENTIFIER;
    n->name = strdup_safe(s);
    n->line = line;
    n->col = col;
    return (ASTNode*)n;
}

/* Copia profunda del identificador para x = x + rhs sin double-free en ast_free */
static ASTNode *clone_identifier_for_compound(IdentifierNode *id) {
    IdentifierNode *n = calloc(1, sizeof(IdentifierNode));
    if (!n) return NULL;
    n->base.type = NODE_IDENTIFIER;
    n->base.line = id->base.line;
    n->base.col = id->base.col;
    n->name = strdup_safe(id->name);
    n->line = id->line;
    n->col = id->col;
    return (ASTNode*)n;
}

/* Forward declarations */
static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p, const char **end_kw, size_t n_end);

static ASTNode *parse_args(Parser *p) {
    /* Solo para parse_primary - retorna primer arg. Usamos parse_expression en bucle. */
    (void)p;
    return NULL;
}

typedef struct {
    ASTNode **arr;
    size_t n;
    size_t cap;
} NodeVec;

typedef struct {
    SelectCase *arr;
    size_t n;
    size_t cap;
} SelectCaseVec;

static int node_vec_push(NodeVec *v, ASTNode *n) {
    if (v->n >= v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 8;
        ASTNode **p = realloc(v->arr, nc * sizeof(ASTNode*));
        if (!p) return -1;
        v->arr = p;
        v->cap = nc;
    }
    v->arr[v->n++] = n;
    return 0;
}

static int select_case_vec_push(SelectCaseVec *v, SelectCase c) {
    if (v->n >= v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 4;
        SelectCase *p = realloc(v->arr, nc * sizeof(SelectCase));
        if (!p) return -1;
        v->arr = p;
        v->cap = nc;
    }
    v->arr[v->n++] = c;
    return 0;
}

static int is_ident_token_for_lambda(const Token *t) {
    if (!t || !t->value.str) return 0;
    if (t->type == TOK_IDENTIFIER) return 1;
    return (t->type == TOK_KEYWORD && keyword_ok_as_user_identifier(t->value.str));
}

/* Parametro invalido en `( ... ) =>`: reservada u otro token; deja last_error si falta. */
static void set_error_bad_lambda_param(Parser *p, const Token *pt) {
    if (!pt || p->last_error) return;
    if (pt->type == TOK_KEYWORD && pt->value.str && !validate_user_defined_name_tok(p, pt))
        return;
    char buf[512];
    const char *tokdisp = pt->value.str ? pt->value.str : "?";
    snprintf(buf, sizeof buf,
        "en una macro `(parametros) => ...` se esperaba un nombre de parametro (identificador valido), no `%s`.",
        tokdisp);
    set_error_here(p, pt, buf);
}

static ASTNode *clone_expr_basic(const ASTNode *node);

static ASTNode *dup_with_lambda_subst(const ASTNode *node, char **params, ASTNode **args, size_t n_params) {
    if (!node) return NULL;
    if (node->type == NODE_IDENTIFIER) {
        const IdentifierNode *id = (const IdentifierNode*)node;
        for (size_t i = 0; i < n_params; i++) {
            if (params[i] && id->name && strcmp(params[i], id->name) == 0)
                return clone_expr_basic(args[i]);
        }
    }
    switch (node->type) {
        case NODE_LITERAL: {
            const LiteralNode *ln = (const LiteralNode*)node;
            LiteralNode *n = calloc(1, sizeof(LiteralNode));
            n->base.type = NODE_LITERAL;
            n->type_name = strdup_safe(ln->type_name);
            n->is_float = ln->is_float;
            if (ln->type_name && strcmp(ln->type_name, "texto") == 0)
                n->value.str = strdup_safe(ln->value.str);
            else
                n->value = ln->value;
            return (ASTNode*)n;
        }
        case NODE_IDENTIFIER:
            return make_identifier(((const IdentifierNode*)node)->name, node->line, node->col);
        case NODE_BINARY_OP: {
            const BinaryOpNode *bn = (const BinaryOpNode*)node;
            BinaryOpNode *n = calloc(1, sizeof(BinaryOpNode));
            n->base.type = NODE_BINARY_OP;
            n->operator = strdup_safe(bn->operator);
            n->left = dup_with_lambda_subst(bn->left, params, args, n_params);
            n->right = dup_with_lambda_subst(bn->right, params, args, n_params);
            n->line = bn->line; n->col = bn->col;
            return (ASTNode*)n;
        }
        case NODE_UNARY_OP: {
            const UnaryOpNode *un = (const UnaryOpNode*)node;
            UnaryOpNode *n = calloc(1, sizeof(UnaryOpNode));
            n->base.type = NODE_UNARY_OP;
            n->operator = strdup_safe(un->operator);
            n->expression = dup_with_lambda_subst(un->expression, params, args, n_params);
            return (ASTNode*)n;
        }
        case NODE_TERNARY: {
            const TernaryNode *tn = (const TernaryNode*)node;
            TernaryNode *n = calloc(1, sizeof(TernaryNode));
            n->base.type = NODE_TERNARY;
            n->condition = dup_with_lambda_subst(tn->condition, params, args, n_params);
            n->true_expr = dup_with_lambda_subst(tn->true_expr, params, args, n_params);
            n->false_expr = dup_with_lambda_subst(tn->false_expr, params, args, n_params);
            return (ASTNode*)n;
        }
        default:
            return NULL;
    }
}

static ASTNode *clone_expr_basic(const ASTNode *node) {
    return dup_with_lambda_subst(node, NULL, NULL, 0);
}

static ASTNode *parse_lambda(Parser *p) {
    if (!match(p, TOK_OPERATOR, "(")) {
        const Token *bad = peek(p, 0);
        if (bad && !p->last_error)
            set_error_here(p, bad,
                "En una macro, la funcion flecha debe comenzar con `(` y la lista de parametros antes de `=>`. "
                "Ejemplo valido: macro f = (x) => x + 1. No es valido omitir los parentesis (p. ej. `macro f = x => ...`).");
        return NULL;
    }
    char *params[16] = {0};
    char *types[16] = {0};
    size_t n_params = 0;
    int parse_ok = 1;
    while (!match(p, TOK_OPERATOR, ")")) {
        const Token *pt = peek(p, 0);
        if (!pt || pt->type == TOK_EOF || n_params >= 16) { parse_ok = 0; break; }
        
        // Optional type
        if (is_decl_type_token(pt)) {
            types[n_params] = strdup_safe(pt->value.str);
            advance(p);
            pt = peek(p, 0);
            if (!pt || pt->type == TOK_EOF || !is_ident_token_for_lambda(pt)) {
                if (pt && !is_ident_token_for_lambda(pt)) set_error_bad_lambda_param(p, pt);
                parse_ok = 0;
                break;
            }
        } else {
            types[n_params] = strdup_safe("entero"); // default
        }

        if (!is_ident_token_for_lambda(pt)) {
            set_error_bad_lambda_param(p, pt);
            parse_ok = 0;
            break;
        }
        params[n_params++] = strdup_safe(pt->value.str);
        advance(p);
        if (match(p, TOK_OPERATOR, ")")) break;
        if (!match(p, TOK_OPERATOR, ",")) { parse_ok = 0; break; }
    }
    
    if (parse_ok && match(p, TOK_OPERATOR, "=>")) {
        ASTNode *body = NULL;
        if (match(p, TOK_OPERATOR, "{")) {
            const char *ends[] = {"}"};
            body = parse_block(p, ends, 1);
            expect(p, TOK_OPERATOR, "}", "Se esperaba '}' para cerrar el bloque de la funcion flecha");
        } else {
            body = parse_expression(p);
            if (!body) {
                const Token *nx = peek(p, 0);
                if (nx && nx->type == TOK_OPERATOR && nx->value.str &&
                    strcmp(nx->value.str, ")") == 0)
                    set_error_lambda_empty_body_after_arrow(p, nx);
            }
        }
        
        if (body) {
            LambdaDeclNode *ld = calloc(1, sizeof(LambdaDeclNode));
            ld->base.type = NODE_LAMBDA_DECL;
            ld->n_params = n_params;
            ld->params = calloc(n_params, sizeof(char*));
            ld->types = calloc(n_params, sizeof(char*));
            for (size_t i = 0; i < n_params; i++) {
                ld->params[i] = strdup_safe(params[i]);
                ld->types[i] = strdup_safe(types[i]);
            }
            ld->body = body;
            const Token *t = peek(p, -1);
            ld->base.line = t ? t->line : 0;
            ld->base.col = t ? t->column : 0;
            for (size_t i = 0; i < n_params; i++) { free(params[i]); free(types[i]); }
            return (ASTNode*)ld;
        }
    } else if (parse_ok && !p->last_error) {
        const Token *bad = peek(p, 0);
        if (bad)
            set_error_here(p, bad,
                "En una macro con funcion flecha, despues de la lista de parametros entre parentesis debe ir el "
                "operador `=>` antes del cuerpo. Ejemplo: macro f = (x) => x + 1.");
    }
    for (size_t i = 0; i < n_params; i++) { free(params[i]); free(types[i]); }
    return NULL;
}

static ASTNode *parse_primary(Parser *p) {
    const Token *t = peek(p, 0);
    if (!t) return NULL;

    if (t->type == TOK_NUMBER) {
        advance(p);
        if (t->is_float)
            return make_literal_float(t->value.f);
        return make_literal_int(t->value.i);
    }
    if (t->type == TOK_STRING) {
        advance(p);
        return make_literal_str(t->value.str);
    }
    if (t->type == TOK_CONCEPT) {
        advance(p);
        const char *s = t->value.str ? t->value.str : "";
        if (s[0] != '\0' && s[1] == '\0') {
            return make_literal_char((unsigned char)s[0]);
        }
        LiteralNode *n = calloc(1, sizeof(LiteralNode));
        if (!n) return NULL;
        n->base.type = NODE_LITERAL;
        n->type_name = strdup("concepto");
        n->value.str = strdup_safe(t->value.str);
        n->is_float = 0;
        return (ASTNode*)n;
    }
    if (t->type == TOK_KEYWORD && t->value.str && !keyword_ok_as_user_identifier(t->value.str)) {
        // En jasboot, "es", "mayor", "menor", "que" son parte de operadores compuestos y podrian llegar aqui.
        // Pero "es" no deberia ser leido como variable.
        validate_user_defined_name_tok(p, t);
        return NULL;
    }
    if (t->type == TOK_IDENTIFIER || (t->type == TOK_KEYWORD && t->value.str &&
        keyword_ok_as_user_identifier(t->value.str))) {
        const char *name = t->value.str ? strdup(t->value.str) : NULL;
        advance(p);
        return make_identifier(name, t->line, t->column);
    }
    if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "(") == 0) {
        size_t save_pos = p->pos;
        
        /* 1. Try parsing as a raw lambda expression: (x, y) => expr or (texto x, entero y) => expr */
        advance(p); /* '(' */
        char *params_raw[16] = {0};
        char *types_raw[16] = {0};
        size_t n_params_raw = 0;
        int raw_ok = 1;
        while (!match(p, TOK_OPERATOR, ")")) {
            const Token *pt = peek(p, 0);
            if (!pt || pt->type == TOK_EOF || n_params_raw >= 16) { raw_ok = 0; break; }
            
            // Optional type
            if (is_decl_type_token(pt)) {
                types_raw[n_params_raw] = strdup_safe(pt->value.str);
                advance(p);
                pt = peek(p, 0);
                if (!pt || pt->type == TOK_EOF || !is_ident_token_for_lambda(pt)) { raw_ok = 0; break; }
            } else {
                types_raw[n_params_raw] = strdup_safe("entero"); // default
            }

            if (!is_ident_token_for_lambda(pt)) { raw_ok = 0; break; }
            params_raw[n_params_raw++] = strdup_safe(pt->value.str);
            advance(p);
            if (match(p, TOK_OPERATOR, ")")) break;
            if (!match(p, TOK_OPERATOR, ",")) { raw_ok = 0; break; }
        }
        if (raw_ok && match(p, TOK_OPERATOR, "=>")) {
            ASTNode *body = NULL;
            if (match(p, TOK_OPERATOR, "{")) {
                const char *ends[] = {"}"};
                body = parse_block(p, ends, 1);
                expect(p, TOK_OPERATOR, "}", "Se esperaba '}' para cerrar el bloque de la funcion flecha");
            } else {
                body = parse_expression(p);
                if (!body) {
                    const Token *nx = peek(p, 0);
                    if (nx && nx->type == TOK_OPERATOR && nx->value.str &&
                        strcmp(nx->value.str, ")") == 0)
                        set_error_lambda_empty_body_after_arrow(p, nx);
                }
            }
            if (body) {
                LambdaDeclNode *ld = calloc(1, sizeof(LambdaDeclNode));
                ld->base.type = NODE_LAMBDA_DECL;
                if (n_params_raw > 0) {
                    ld->params = calloc(n_params_raw, sizeof(char*));
                    ld->types = calloc(n_params_raw, sizeof(char*));
                    for (size_t i = 0; i < n_params_raw; i++) {
                        ld->params[i] = params_raw[i];
                        ld->types[i] = types_raw[i];
                    }
                }
                ld->n_params = n_params_raw;
                ld->body = body;
                return (ASTNode*)ld;
            }
        }
        for (size_t i = 0; i < n_params_raw; i++) {
            free(params_raw[i]);
            free(types_raw[i]);
        }
        p->pos = save_pos;

        /* 2. Try parsing as immediate lambda invocation: ((x, y) => expr)(args) or ((texto x) => expr)(args) */
        advance(p); /* '(' externo */
        const Token *param_list_open = peek(p, 0);
        if (match(p, TOK_OPERATOR, "(")) {
            char *params[16] = {0};
            char *types[16] = {0};
            size_t n_params = 0;
            int parse_ok = 1;
            while (!match(p, TOK_OPERATOR, ")")) {
                const Token *pt = peek(p, 0);
                if (!pt || pt->type == TOK_EOF || n_params >= 16) { parse_ok = 0; break; }
                
                // Optional type
                if (is_decl_type_token(pt)) {
                    types[n_params] = strdup_safe(pt->value.str);
                    advance(p);
                    pt = peek(p, 0);
                    if (!pt || pt->type == TOK_EOF || !is_ident_token_for_lambda(pt)) { parse_ok = 0; break; }
                } else {
                    types[n_params] = strdup_safe("entero"); // default
                }

                if (!is_ident_token_for_lambda(pt)) { parse_ok = 0; break; }
                params[n_params++] = strdup_safe(pt->value.str);
                advance(p);
                if (match(p, TOK_OPERATOR, ")")) break;
                if (!match(p, TOK_OPERATOR, ",")) { parse_ok = 0; break; }
            }
                if (!parse_ok) {
                }
            if (parse_ok && match(p, TOK_OPERATOR, "=>")) {
                ASTNode *body = NULL;
                if (match(p, TOK_OPERATOR, "{")) {
                    const char *ends[] = {"}"};
                    body = parse_block(p, ends, 1);
                    expect(p, TOK_OPERATOR, "}", "Se esperaba '}' para cerrar el bloque de la funcion flecha");
                } else {
                    body = parse_expression(p);
                    if (!body) {
                        const Token *nx = peek(p, 0);
                        if (nx && nx->type == TOK_OPERATOR && nx->value.str &&
                            strcmp(nx->value.str, ")") == 0)
                            set_error_lambda_empty_body_after_arrow(p, nx);
                    }
                }
                if (body && match(p, TOK_OPERATOR, ")")) {
                    const Token *args_list_open = peek(p, 0);
                    if (match(p, TOK_OPERATOR, "(")) {
                        ASTNode *args[16] = {0};
                        size_t n_args = 0;
                        int args_ok = 1;
                        while (!match(p, TOK_OPERATOR, ")")) {
                            if (n_args >= 16) { args_ok = 0; break; }
                            ASTNode *a = parse_expression(p);
                            if (!a) { args_ok = 0; break; }
                            args[n_args++] = a;
                            if (match(p, TOK_OPERATOR, ")")) break;
                            if (!match(p, TOK_OPERATOR, ",")) { args_ok = 0; break; }
                        }
                        if (args_ok) {
                            if (n_args != n_params) {
                                set_error_lambda_arity_iife(p, param_list_open, args_list_open);
                                for (size_t i = 0; i < n_args; i++) ast_free(args[i]);
                                ast_free(body);
                                for (size_t i = 0; i < n_params; i++) { free(params[i]); free(types[i]); }
                                return NULL;
                            }
                            ASTNode *expanded = dup_with_lambda_subst(body, params, args, n_params);
                            for (size_t i = 0; i < n_args; i++) ast_free(args[i]);
                            ast_free(body);
                            for (size_t i = 0; i < n_params; i++) { free(params[i]); free(types[i]); }
                            return expanded;
                        }
                        for (size_t i = 0; i < n_args; i++) ast_free(args[i]);
                    }
                } else if (body) {
                    /* No es ejecucion inmediata, retornar el nodo LambdaDeclNode */
                    LambdaDeclNode *ld = calloc(1, sizeof(LambdaDeclNode));
                    ld->base.type = NODE_LAMBDA_DECL;
                    ld->n_params = n_params;
                    ld->params = calloc(n_params, sizeof(char*));
                    ld->types = calloc(n_params, sizeof(char*));
                    for (size_t i = 0; i < n_params; i++) {
                        ld->params[i] = strdup_safe(params[i]);
                        ld->types[i] = strdup_safe(types[i]);
                    }
                    ld->body = body;
                    ld->base.line = t->line;
                    ld->base.col = t->column;
                    for (size_t i = 0; i < n_params; i++) { free(params[i]); free(types[i]); }
                    return (ASTNode*)ld;
                } else {
                }
                if (body) ast_free(body);
            }
            for (size_t i = 0; i < n_params; i++) { free(params[i]); free(types[i]); }
        }
        if (p->last_error)
            return NULL;
        p->pos = save_pos;
        advance(p);
        ASTNode *n = parse_expression(p);
        if (p->last_error)
            return NULL;
        expect(p, TOK_OPERATOR, ")", "Se esperaba ')'");
        return n;
    }
    if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "[") == 0) {
        advance(p);
        NodeVec el = {0};
        while (peek(p, 0) && (!peek(p, 0)->value.str || strcmp(peek(p, 0)->value.str, "]") != 0)) {
            ASTNode *e = parse_expression(p);
            if (!e) break;
            node_vec_push(&el, e);
            if (!match(p, TOK_OPERATOR, ",")) break;
        }
        expect(p, TOK_OPERATOR, "]", "Se esperaba ']'");
        ListLiteralNode *ln = calloc(1, sizeof(ListLiteralNode));
        if (!ln) return NULL;
        ln->base.type = NODE_LIST_LITERAL;
        ln->elements = el.arr;
        ln->n = el.n;
        return (ASTNode*)ln;
    }
    if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "{") == 0) {
        advance(p);
        NodeVec keys = {0}, vals = {0};
        while (peek(p, 0) && (!peek(p, 0)->value.str || strcmp(peek(p, 0)->value.str, "}") != 0)) {
            ASTNode *k = parse_expression(p);
            if (!k) break;
            expect(p, TOK_OPERATOR, ":", "Se esperaba ':'");
            ASTNode *v = parse_expression(p);
            if (!v) break;
            node_vec_push(&keys, k);
            node_vec_push(&vals, v);
            if (!match(p, TOK_OPERATOR, ",")) break;
        }
        expect(p, TOK_OPERATOR, "}", "Se esperaba '}'");
        MapLiteralNode *mn = calloc(1, sizeof(MapLiteralNode));
        if (!mn) return NULL;
        mn->base.type = NODE_MAP_LITERAL;
        mn->keys = keys.arr;
        mn->values = vals.arr;
        mn->n = keys.n;
        return (ASTNode*)mn;
    }
    return NULL;
}

static int ast_is_postfix_lvalue(ASTNode *n) {
    if (!n) return 0;
    return n->type == NODE_IDENTIFIER || n->type == NODE_MEMBER_ACCESS || n->type == NODE_INDEX_ACCESS;
}

static ASTNode *parse_unary(Parser *p) {
    if (match(p, TOK_OPERATOR, "-")) {
        UnaryOpNode *n = calloc(1, sizeof(UnaryOpNode));
        if (!n) return NULL;
        n->base.type = NODE_UNARY_OP;
        n->operator = strdup("-");
        n->expression = parse_unary(p);
        if (!n->expression) { ast_free((ASTNode*)n); return NULL; }
        /* Desugar: -x => 0 - x */
        BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
        b->base.type = NODE_BINARY_OP;
        b->left = make_literal_int(0);
        b->operator = strdup("-");
        b->right = n->expression;
        free(n->operator);
        free(n);
        return (ASTNode*)b;
    }
    if (match(p, TOK_KEYWORD, "no") || match(p, TOK_OPERATOR, "!")) {
        UnaryOpNode *n = calloc(1, sizeof(UnaryOpNode));
        if (!n) return NULL;
        n->base.type = NODE_UNARY_OP;
        n->operator = strdup("no");
        n->expression = parse_unary(p);
        return (ASTNode*)n;
    }
    if (match(p, TOK_KEYWORD, "llamar")) {
        const Token *here = peek(p, 0);
        ASTNode *inner = parse_primary(p);
        if (!inner || inner->type != NODE_CALL) {
            if (inner) ast_free(inner);
            if (!p->last_error && here) {
                if (p->source_path && p->source_path[0])
                    set_error_at(p, here->line, here->column,
                          "Archivo %s, linea %d, columna %d: tras `llamar` debe seguir una llamada con la forma nombre( argumentos ). Ejemplo: llamar mi_funcion(1, 2).",
                          p->source_path, here->line, here->column);
                else
                    set_error_at(p, here->line, here->column,
                          "linea %d, columna %d: tras `llamar` debe seguir una llamada con la forma nombre( argumentos ). Ejemplo: llamar mi_funcion(1, 2).",
                          here->line, here->column);
            }
            return NULL;
        }
        return inner;
    }
    ASTNode *node = parse_primary(p);
    if (!node) return NULL;
    /* Sufijos: .member, [index] */
    while (1) {
        if (match(p, TOK_OPERATOR, ".")) {
            const Token *m = peek(p, 0);
            /* `y` es palabra clave (`y` logico); en vectores el campo .y debe seguir siendo valido. */
            int ok_member = m && m->value.str &&
                (m->type == TOK_IDENTIFIER ||
                 (m->type == TOK_KEYWORD && strcmp(m->value.str, "y") == 0));
            if (ok_member) {
                advance(p);
                MemberAccessNode *ma = calloc(1, sizeof(MemberAccessNode));
                ma->base.type = NODE_MEMBER_ACCESS;
                ma->base.line = m->line;
                ma->base.col = m->column;
                ma->target = node;
                ma->member = strdup(m->value.str);
                node = (ASTNode*)ma;
            } else break;
        } else if (match(p, TOK_OPERATOR, "[")) {
            ASTNode *idx = parse_expression(p);
            expect(p, TOK_OPERATOR, "]", "Se esperaba ']'");
            IndexAccessNode *ia = calloc(1, sizeof(IndexAccessNode));
            ia->base.type = NODE_INDEX_ACCESS;
            ia->target = node;
            ia->index = idx;
            node = (ASTNode*)ia;
        } else break;
    }
    /* Llamadas: primario ( args ) o miembro(args) — soporta valores funcion como callees */
    while (1) {
        const Token *lp = peek(p, 0);
        if (!lp || lp->type != TOK_OPERATOR || !lp->value.str || strcmp(lp->value.str, "(") != 0)
            break;
        int cline = lp->line, ccol = lp->column;
        advance(p);
        NodeVec args = {0};
        for (int max_args = 0; max_args < 16; max_args++) {
            const Token *nx = peek(p, 0);
            if (!nx || nx->type == TOK_EOF) break;
            if (nx->type == TOK_OPERATOR && nx->value.str && strcmp(nx->value.str, ")") == 0)
                break;
            ASTNode *a = parse_expression(p);
            if (!a) break;
            if (node_vec_push(&args, a) != 0) break;
            if (!match(p, TOK_OPERATOR, ",")) break;
        }
        if (!expect(p, TOK_OPERATOR, ")", "Se esperaba ')'")) {
            for (size_t i = 0; i < args.n; i++) ast_free(args.arr[i]);
            free(args.arr);
            ast_free(node);
            return NULL;
        }
        CallNode *cn = calloc(1, sizeof(CallNode));
        if (!cn) {
            for (size_t i = 0; i < args.n; i++) ast_free(args.arr[i]);
            free(args.arr);
            ast_free(node);
            return NULL;
        }
        cn->base.type = NODE_CALL;
        cn->base.line = cline;
        cn->base.col = ccol;
        cn->args = args.arr;
        cn->n_args = args.n;
        if (node && node->type == NODE_IDENTIFIER) {
            IdentifierNode *id = (IdentifierNode *)node;
            cn->name = id->name;
            id->name = NULL;
            cn->callee = NULL;
            ast_free(node);
        } else {
            cn->callee = node;
            cn->name = NULL;
        }
        node = (ASTNode *)cn;
    }
    while (1) {
        const Token *op = peek(p, 0);
        int delta = 0;
        if (op && op->type == TOK_OPERATOR && op->value.str) {
            if (strcmp(op->value.str, "++") == 0) delta = 1;
            else if (strcmp(op->value.str, "--") == 0) delta = -1;
        }
        if (delta == 0) break;
        int err_line = op->line;
        int err_col = op->column;
        advance(p);
        if (!ast_is_postfix_lvalue(node)) {
            if (p->source_path && p->source_path[0])
                set_error_at(p, err_line, err_col,
                    "Archivo %s, linea %d, columna %d: '++' y '--' solo aplican a variables, campos o indice [].",
                    p->source_path, err_line, err_col);
            else
                set_error_at(p, err_line, err_col,
                    "linea %d, columna %d: '++' y '--' solo aplican a variables, campos o indice [].",
                    err_line, err_col);
            return NULL;
        }
        PostfixUpdateNode *pu = calloc(1, sizeof(PostfixUpdateNode));
        if (!pu) return NULL;
        pu->base.type = NODE_POSTFIX_UPDATE;
        pu->base.line = err_line;
        pu->base.col = err_col;
        pu->target = node;
        pu->delta = delta;
        node = (ASTNode*)pu;
    }
    return node;
}

static ASTNode *parse_term(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (!left) return NULL;
    while (1) {
        const Token *t = peek(p, 0);
        if (!t || t->type != TOK_OPERATOR || !t->value.str) break;
        const char *op = t->value.str;
        if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0 ||
            strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
            const Token *t_op = peek(p, 0);
            int op_line = t_op ? t_op->line : 0;
            int op_col = t_op ? t_op->column : 0;
            advance(p);
            ASTNode *right = parse_unary(p);
            if (!right) { ast_free(left); return NULL; }
            BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
            b->base.type = NODE_BINARY_OP;
            b->left = left;
            b->operator = strdup(op);
            b->right = right;
            b->line = op_line;
            b->col = op_col;
            left = (ASTNode*)b;
        } else break;
    }
    return left;
}

static ASTNode *parse_arithmetic(Parser *p) {
    ASTNode *left = parse_term(p);
    if (!left) return NULL;
    while (1) {
        const Token *t = peek(p, 0);
        if (!t || t->type != TOK_OPERATOR || !t->value.str) break;
        const char *op = t->value.str;
        if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0) {
            const Token *t_op = peek(p, 0);
            int op_line = t_op ? t_op->line : 0;
            int op_col = t_op ? t_op->column : 0;
            advance(p);
            ASTNode *right = parse_term(p);
            if (!right) { ast_free(left); return NULL; }
            BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
            b->base.type = NODE_BINARY_OP;
            b->left = left;
            b->operator = strdup(op);
            b->right = right;
            b->line = op_line;
            b->col = op_col;
            left = (ASTNode*)b;
        } else break;
    }
    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_arithmetic(p);
    if (!left) return NULL;
    const Token *t = peek(p, 0);
    if (!t) return left;
    const char *op = NULL;
    int op_consumed = 0;  /* 1 si ya consumimos operador (es igual a, mayor que, menor que) */

    int is_negated = 0;
    if (t->type == TOK_KEYWORD && t->value.str && strcmp(t->value.str, "no") == 0) {
        const Token *t2 = peek(p, 1);
        if (t2 && t2->type == TOK_KEYWORD && t2->value.str && strcmp(t2->value.str, "es") == 0) {
            is_negated = 1;
            advance(p);
            t = peek(p, 0);
        } else if (t2 && t2->type == TOK_IDENTIFIER && t2->value.str && strcmp(t2->value.str, "esta") == 0) {
            is_negated = 1;
            advance(p);
            t = peek(p, 0);
        }
    }

    if (t->type == TOK_OPERATOR && t->value.str) {
        if (strcmp(t->value.str, "==") == 0 || strcmp(t->value.str, "!=") == 0 ||
            strcmp(t->value.str, "<") == 0 || strcmp(t->value.str, ">") == 0 ||
            strcmp(t->value.str, "<=") == 0 || strcmp(t->value.str, ">=") == 0) {
            op = t->value.str;
            if (is_negated) {
                if (strcmp(op, "==") == 0) op = "!=";
                else if (strcmp(op, "!=") == 0) op = "==";
                else if (strcmp(op, "<") == 0) op = ">=";
                else if (strcmp(op, ">") == 0) op = "<=";
                else if (strcmp(op, "<=") == 0) op = ">";
                else if (strcmp(op, ">=") == 0) op = "<";
            }
        }
    } else if (t->type == TOK_KEYWORD && t->value.str) {
        // En jasboot, "es" no se trata como Keyword sino como inicio de un operador verbal ("es igual a", "es mayor que").
        // Sin embargo, si entra aqui, `parse_arithmetic` no procesó "es".
        // La sintaxis de comparacion original asume que despues de una expresion aritmetica viene un operador.
        // Pero en tu codigo es: `si num_a es mayor que b entonces`
        if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "igual") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "a") == 0) {
            advance(p); advance(p); advance(p);
            op = is_negated ? "!=" : "==";
            op_consumed = 1;
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "mayor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "o") == 0 && peek(p, 3) && peek(p, 3)->value.str &&
            strcmp(peek(p, 3)->value.str, "igual") == 0 && peek(p, 4) && peek(p, 4)->value.str &&
            strcmp(peek(p, 4)->value.str, "a") == 0) {
            advance(p); advance(p); advance(p); advance(p); advance(p);
            op = is_negated ? "<" : ">=";
            op_consumed = 1;
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "menor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "o") == 0 && peek(p, 3) && peek(p, 3)->value.str &&
            strcmp(peek(p, 3)->value.str, "igual") == 0 && peek(p, 4) && peek(p, 4)->value.str &&
            strcmp(peek(p, 4)->value.str, "a") == 0) {
            advance(p); advance(p); advance(p); advance(p); advance(p);
            op = is_negated ? ">" : "<=";
            op_consumed = 1;
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "mayor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "que") == 0) {
            advance(p); advance(p); advance(p);
            op = is_negated ? "<=" : ">";
            op_consumed = 1;
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "menor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "que") == 0) {
            advance(p); advance(p); advance(p);
            op = is_negated ? ">=" : "<";
            op_consumed = 1;
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            (strcmp(peek(p, 1)->value.str, "distinto") == 0 || strcmp(peek(p, 1)->value.str, "diferente") == 0) &&
            peek(p, 2) && peek(p, 2)->value.str &&
            (strcmp(peek(p, 2)->value.str, "de") == 0 || strcmp(peek(p, 2)->value.str, "a") == 0)) {
            advance(p); advance(p); advance(p);
            op = is_negated ? "==" : "!=";
            op_consumed = 1;
        }
    } else if (t->type == TOK_IDENTIFIER && t->value.str) {
        if (strcmp(t->value.str, "esta") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            (strcmp(peek(p, 1)->value.str, "vacia") == 0 || strcmp(peek(p, 1)->value.str, "vacio") == 0)) {
            advance(p); advance(p);
            op = is_negated ? "!=" : "==";
            op_consumed = 1;
            BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
            b->base.type = NODE_BINARY_OP;
            b->left = left;
            b->operator = strdup(op);
            b->right = make_literal_int(0);
            return (ASTNode*)b;
        }
    }
    if (op) {
        if (!op_consumed && (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0))
            advance(p);  /* consumir operador de un solo token */
        ASTNode *right = parse_arithmetic(p);
        if (!right) { ast_free(left); return NULL; }
        BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
        b->base.type = NODE_BINARY_OP;
        b->left = left;
        b->operator = strdup(op);
        b->right = right;
        left = (ASTNode*)b;
    }
    return left;
}

static ASTNode *parse_logical_and(Parser *p) {
    ASTNode *left = parse_comparison(p);
    if (!left) return NULL;
    while (match(p, TOK_KEYWORD, "y")) {
        ASTNode *right = parse_comparison(p);
        if (!right) { ast_free(left); return NULL; }
        BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
        b->base.type = NODE_BINARY_OP;
        b->left = left;
        b->operator = strdup("y");
        b->right = right;
        left = (ASTNode*)b;
    }
    return left;
}

static ASTNode *parse_logical_or(Parser *p) {
    ASTNode *left = parse_logical_and(p);
    if (!left) return NULL;
    while (match(p, TOK_KEYWORD, "o")) {
        ASTNode *right = parse_logical_and(p);
        if (!right) { ast_free(left); return NULL; }
        BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
        b->base.type = NODE_BINARY_OP;
        b->left = left;
        b->operator = strdup("o");
        b->right = right;
        left = (ASTNode*)b;
    }
    return left;
}

static ASTNode *parse_ternary(Parser *p) {
    ASTNode *cond = parse_logical_or(p);
    if (!cond) return NULL;
    if (match(p, TOK_OPERATOR, "?")) {
        ASTNode *te = parse_expression(p);
        expect(p, TOK_OPERATOR, ":", "Se esperaba ':'");
        ASTNode *fe = parse_expression(p);
        if (!te || !fe) { ast_free(cond); ast_free(te); ast_free(fe); return NULL; }
        TernaryNode *tn = calloc(1, sizeof(TernaryNode));
        tn->base.type = NODE_TERNARY;
        tn->condition = cond;
        tn->true_expr = te;
        tn->false_expr = fe;
        return (ASTNode*)tn;
    }
    return cond;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_ternary(p);
}

static void parser_accumulate_error(Parser *p, const char *err_msg) {
    if (!err_msg) return;
    p->error_count++;
    if (!p->accumulated_errors) {
        p->accumulated_errors = strdup(err_msg);
    } else {
        size_t l1 = strlen(p->accumulated_errors);
        size_t l2 = strlen(err_msg);
        char *comb = malloc(l1 + l2 + 2);
        if (comb) {
            strcpy(comb, p->accumulated_errors);
            if (l1 > 0 && p->accumulated_errors[l1 - 1] != '\n') strcat(comb, "\n");
            strcat(comb, err_msg);
            free(p->accumulated_errors);
            p->accumulated_errors = comb;
        }
    }
}

static void parser_synchronize(Parser *p) {
    if (p->pos < token_vec_size(p->tokens)) p->pos++; // saltar el token que causó el error para no ciclar
    while (p->pos < token_vec_size(p->tokens)) {
        const Token *t = peek(p, 0);
        if (!t || t->type == TOK_EOF) return;
        if (t->type == TOK_KEYWORD && t->value.str) {
            const char *kw = t->value.str;
            if (strcmp(kw, "entero") == 0 || strcmp(kw, "texto") == 0 ||
                strcmp(kw, "flotante") == 0 || strcmp(kw, "caracter") == 0 ||
                strcmp(kw, "bool") == 0 || strcmp(kw, "lista") == 0 ||
                strcmp(kw, "mapa") == 0 || strcmp(kw, "u32") == 0 ||
                strcmp(kw, "u64") == 0 || strcmp(kw, "u8") == 0 ||
                strcmp(kw, "byte") == 0 || strcmp(kw, "vec2") == 0 ||
                strcmp(kw, "vec3") == 0 || strcmp(kw, "vec4") == 0 ||
                strcmp(kw, "mat4") == 0 || strcmp(kw, "mat3") == 0 ||
                strcmp(kw, "si") == 0 || strcmp(kw, "mientras") == 0 ||
                strcmp(kw, "para_cada") == 0 ||
                strcmp(kw, "hacer") == 0 || strcmp(kw, "para") == 0 ||
                strcmp(kw, "seleccionar") == 0 || strcmp(kw, "intentar") == 0 ||
                strcmp(kw, "funcion") == 0 || strcmp(kw, "principal") == 0 ||
                strcmp(kw, "retornar") == 0 || strcmp(kw, "lanzar") == 0 ||
                strcmp(kw, "llamar") == 0 ||
                strcmp(kw, "imprimir") == 0 || strcmp(kw, "ingresar_texto") == 0 ||
                strcmp(kw, "ingreso_inmediato") == 0 || strcmp(kw, "limpiar_consola") == 0 ||
                strcmp(kw, "fin_si") == 0 || strcmp(kw, "fin_mientras") == 0 ||
                strcmp(kw, "fin_para_cada") == 0 ||
                strcmp(kw, "fin_hacer") == 0 || strcmp(kw, "fin_para") == 0 ||
                strcmp(kw, "fin_seleccionar") == 0 || strcmp(kw, "fin_intentar") == 0 ||
                strcmp(kw, "fin_funcion") == 0 || strcmp(kw, "fin_principal") == 0 ||
                strcmp(kw, "caso") == 0 || strcmp(kw, "defecto") == 0 ||
                strcmp(kw, "atrapar") == 0 || strcmp(kw, "final") == 0 ||
                strcmp(kw, "registro") == 0 || strcmp(kw, "fin_registro") == 0) {
                return;
            }
        }
        p->pos++;
    }
}

/* Linea de `fin_registro` antes que `principal` o `registro` al nivel de archivo; 0 si no aplica. */
static int find_orphan_fin_registro_line(const Parser *p, size_t from_pos) {
    size_t n = token_vec_size(p->tokens);
    for (size_t i = from_pos; i < n; i++) {
        const Token *tk = token_vec_get(p->tokens, i);
        if (!tk || tk->type == TOK_EOF) return 0;
        if (tk->type == TOK_KEYWORD && tk->value.str) {
            if (strcmp(tk->value.str, "principal") == 0 || strcmp(tk->value.str, "registro") == 0)
                return 0;
            if (strcmp(tk->value.str, "fin_registro") == 0)
                return (int)tk->line;
        }
    }
    return 0;
}

static void advance_past_fin_registro_from(Parser *p, size_t from) {
    size_t n = token_vec_size(p->tokens);
    for (size_t i = from; i < n; i++) {
        const Token *tk = token_vec_get(p->tokens, i);
        if (tk && tk->type == TOK_KEYWORD && tk->value.str && strcmp(tk->value.str, "fin_registro") == 0) {
            p->pos = i + 1;
            return;
        }
    }
}

/* parse_block: end_kw es lista de keywords que terminan el bloque */
static ASTNode *parse_block(Parser *p, const char **end_kw, size_t n_end) {
    NodeVec stmts = {0};
    while (1) {
        const Token *t = peek(p, 0);
        if (!t) break;
        int is_end = 0;
        if ((t->type == TOK_KEYWORD || t->type == TOK_OPERATOR) && t->value.str) {
            for (size_t i = 0; i < n_end; i++)
                if (strcmp(t->value.str, end_kw[i]) == 0) { is_end = 1; break; }
            if (is_end) break;
        }
        const Token *start_t = peek(p, 0);
        int stmt_line = start_t ? start_t->line : 0;
        int stmt_col = start_t ? start_t->column : 0;
        
        ASTNode *s = parse_statement(p);
        if (!s) {
            if (!p->last_error) {
                const Token *err_t = peek(p, 0);
                if (err_t && err_t->type == TOK_KEYWORD && err_t->value.str) {
                    if (strncmp(err_t->value.str, "fin_", 4) == 0) {
                        break;
                    } else if (strcmp(err_t->value.str, "sino") == 0) {
                        if (p->source_path && p->source_path[0])
                            set_error_at(p, err_t->line, err_t->column, 
                                     "Archivo %s, linea %d, columna %d: Error de sintaxis: se encontro 'sino' sin un 'si' o 'cuando' previo correspondiente.", p->source_path, err_t->line, err_t->column);
                        else
                            set_error_at(p, err_t->line, err_t->column, 
                                     "linea %d, columna %d: Error de sintaxis: se encontro 'sino' sin un 'si' o 'cuando' previo correspondiente.", err_t->line, err_t->column);
                    } else {
                         if (p->source_path && p->source_path[0])
                             set_error_at(p, err_t->line, err_t->column, 
                                      "Archivo %s, linea %d, columna %d: Error de sintaxis inesperado cerca de '%s'. Verifique que los bloques (si, mientras, etc) esten bien cerrados.", p->source_path, err_t->line, err_t->column, err_t->value.str);
                         else
                             set_error_at(p, err_t->line, err_t->column, 
                                      "linea %d, columna %d: Error de sintaxis inesperado cerca de '%s'. Verifique que los bloques (si, mientras, etc) esten bien cerrados.", err_t->line, err_t->column, err_t->value.str);
                    }
                } else if (err_t) {
                     if (err_t->type == TOK_EOF && n_end > 0) {
                         if (p->source_path && p->source_path[0])
                             set_error_at(p, stmt_line, stmt_col,
                                      "Archivo %s, linea %d, columna %d: bloque sin cierre. Falta `%s` antes del fin de archivo.",
                                      p->source_path, stmt_line, stmt_col, end_kw[0]);
                         else
                             set_error_at(p, stmt_line, stmt_col,
                                      "linea %d, columna %d: bloque sin cierre. Falta `%s` antes del fin de archivo.",
                                      stmt_line, stmt_col, end_kw[0]);
                     } else {
                         int block_wants_brace = (n_end == 1 && end_kw[0] && strcmp(end_kw[0], "}") == 0);
                         int got_paren = (err_t->type == TOK_OPERATOR && err_t->value.str &&
                                          strcmp(err_t->value.str, ")") == 0);
                         if (block_wants_brace && got_paren) {
                             if (p->source_path && p->source_path[0])
                                 set_error_at(p, err_t->line, err_t->column,
                                              "Archivo %s, linea %d, columna %d: Bloque abierto con '{' sin cerrar: aqui aparece ')' pero aun faltaba '}' "
                                              "para terminar el cuerpo. Suele ocurrir en funciones flecha con cuerpo multilinea (=> { ... }).",
                                              p->source_path, err_t->line, err_t->column);
                             else
                                 set_error_at(p, err_t->line, err_t->column,
                                              "linea %d, columna %d: Bloque abierto con '{' sin cerrar: aparece ')' antes de la '}' que cierra el cuerpo "
                                              "(p. ej. cuerpo de funcion flecha => { ... }).",
                                              err_t->line, err_t->column);
                         } else {
                         char got[192];
                         format_token_desc(err_t, got, sizeof got);
                         const char *hint = "Verifique que los bloques esten bien cerrados o la sintaxis sea correcta.";
                         if (err_t->type == TOK_STRING || err_t->type == TOK_NUMBER || err_t->type == TOK_CONCEPT) {
                             hint = "¿Falta un operador (como '+' para concatenar) antes de este valor o en medio de las expresiones?";
                         }
                         if (p->source_path && p->source_path[0])
                             set_error_at(p, err_t->line, err_t->column, 
                                      "Archivo %s, linea %d, columna %d: Error de sintaxis inesperado cerca de %s. %s", p->source_path, err_t->line, err_t->column, got, hint);
                         else
                             set_error_at(p, err_t->line, err_t->column, 
                                      "linea %d, columna %d: Error de sintaxis inesperado cerca de %s. %s", err_t->line, err_t->column, got, hint);
                         }
                     }
                }
            }

            if (p->last_error) {
                parser_accumulate_error(p, p->last_error);
                free(p->last_error);
                p->last_error = NULL;
                if (peek(p, 0) && peek(p, 0)->type == TOK_EOF) break;
                parser_synchronize(p);
                continue;
            }
            break;
        }
        if (s->line == 0) {
            s->line = stmt_line;
            s->col = stmt_col;
        }
        node_vec_push(&stmts, s);
        
        if (s->type == NODE_END_DO_WHILE) {
            break;
        }
        
        /* Punto opcional */
        match(p, TOK_OPERATOR, ".");
    }
    BlockNode *bn = calloc(1, sizeof(BlockNode));
    if (!bn) return NULL;
    bn->base.type = NODE_BLOCK;
    bn->statements = stmts.arr;
    bn->n = stmts.n;
    return (ASTNode*)bn;
}

static ASTNode *parse_statement(Parser *p) {
    const Token *t = peek(p, 0);
    if (!t) return NULL;

    if (t->type == TOK_KEYWORD && t->value.str) {
        if (strncmp(t->value.str, "fin_", 4) == 0 || strcmp(t->value.str, "sino") == 0) {
            // No seteamos error aqui. Si parse_statement es llamado directamente con una palabra clave
            // de cierre de bloque, simplemente retornamos NULL. Esto permite que el llamador (como parse_block)
            // se de cuenta que ya no puede parsear mas statements y termine, dejando que el control vuelva
            // a la funcion padre (si, mientras, etc.) para validar el cierre correctamente, y dar un error con num de linea.
            return NULL;
        }

        if (strcmp(t->value.str, "funcion") == 0) {
            const Token *n1 = peek(p, 1);
            const Token *n2 = peek(p, 2);
            int name_ok = n1 && (n1->type == TOK_IDENTIFIER ||
                                 (n1->type == TOK_KEYWORD && n1->value.str && keyword_ok_as_user_identifier(n1->value.str)));
            if (name_ok && n2 && n2->type == TOK_OPERATOR && n2->value.str && strcmp(n2->value.str, "(") == 0) {
                if (p->source_path && p->source_path[0])
                    set_error_at(p, t->line, t->column,
                              "Archivo %s, linea %d, columna %d: declaracion de funcion no permitida en este contexto. Las funciones deben declararse a nivel superior, fuera de `principal` y de otros bloques.",
                              p->source_path, t->line, t->column);
                else
                    set_error_at(p, t->line, t->column,
                              "linea %d, columna %d: declaracion de funcion no permitida en este contexto. Las funciones deben declararse a nivel superior, fuera de `principal` y de otros bloques.",
                              t->line, t->column);
                return NULL;
            }
            /* `funcion nombreVariable` (sin `(`): tipo funcion, se analiza mas abajo como TYPE ID */
        }

        if (strcmp(t->value.str, "imprimir") == 0 || strcmp(t->value.str, "imprimir_texto") == 0) {
            int imp_line = t->line;
            int imp_col = t->column;
            advance(p);
            const Token *nx = peek(p, 0);
            if (!nx || nx->type == TOK_EOF) {
                if (p->source_path && p->source_path[0])
                    set_error_at(p, imp_line, imp_col,
                              "Archivo %s, linea %d, columna %d: tras `imprimir` o `imprimir_texto` se esperaba una expresion; se alcanzo el fin del archivo.",
                              p->source_path, imp_line, imp_col);
                else
                    set_error_at(p, imp_line, imp_col,
                              "linea %d, columna %d: tras `imprimir` o `imprimir_texto` se esperaba una expresion; se alcanzo el fin del archivo.",
                              imp_line, imp_col);
                return NULL;
            }
            if (nx->type == TOK_KEYWORD && nx->value.str) {
                const char *k = nx->value.str;
                if (strncmp(k, "fin_", 4) == 0 || strcmp(k, "sino") == 0) {
                    char got[192];
                    format_token_desc(nx, got, sizeof got);
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, nx->line, nx->column,
                                  "Archivo %s, linea %d, columna %d: tras `imprimir` se esperaba una expresion; el siguiente token es %s.",
                                  p->source_path, nx->line, nx->column, got);
                    else
                        set_error_at(p, nx->line, nx->column,
                                  "linea %d, columna %d: tras `imprimir` se esperaba una expresion; el siguiente token es %s.",
                                  nx->line, nx->column, got);
                    return NULL;
                }
            }
            ASTNode *e = parse_expression(p);
            if (!e) {
                nx = peek(p, 0);
                char got[192];
                format_token_desc(nx, got, sizeof got);
                int el = nx ? nx->line : imp_line;
                int ec = nx ? nx->column : imp_col;
                if (p->source_path && p->source_path[0])
                    set_error_at(p, el, ec,
                              "Archivo %s, linea %d, columna %d: tras `imprimir` se esperaba una expresion. Empezando aqui: %s.",
                              p->source_path, el, ec, got);
                else
                    set_error_at(p, el, ec,
                              "linea %d, columna %d: tras `imprimir` se esperaba una expresion. Empezando aqui: %s.",
                              el, ec, got);
                return NULL;
            }
            PrintNode *n = calloc(1, sizeof(PrintNode));
            n->base.type = NODE_PRINT;
            n->expression = e;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "imprimir_sin_salto") == 0) {
            int isl = t->line;
            int isc = t->column;
            advance(p);
            const Token *nx = peek(p, 0);
            if (!nx || nx->type == TOK_EOF) {
                if (p->source_path && p->source_path[0])
                    set_error_at(p, isl, isc,
                              "Archivo %s, linea %d, columna %d: tras `imprimir_sin_salto` se esperaba una expresion.",
                              p->source_path, isl, isc);
                else
                    set_error_at(p, isl, isc,
                              "linea %d, columna %d: tras `imprimir_sin_salto` se esperaba una expresion.",
                              isl, isc);
                return NULL;
            }
            ASTNode *e = parse_expression(p);
            if (!e) return NULL;
            CallNode *cn = calloc(1, sizeof(CallNode));
            cn->base.type = NODE_CALL;
            cn->base.line = isl;
            cn->base.col = isc;
            cn->name = strdup("imprimir_sin_salto");
            cn->n_args = 1;
            cn->args = malloc(sizeof(ASTNode *));
            if (!cn->args) {
                ast_free(e);
                free(cn);
                return NULL;
            }
            cn->args[0] = e;
            return (ASTNode *)cn;
        }
        if (strcmp(t->value.str, "limpiar_consola") == 0) {
            int lcl = t->line;
            int lcc = t->column;
            advance(p);
            if (peek(p, 0) && peek(p, 0)->type == TOK_OPERATOR && peek(p, 0)->value.str &&
                strcmp(peek(p, 0)->value.str, "(") == 0) {
                advance(p);
                if (!expect(p, TOK_OPERATOR, ")", "tras `limpiar_consola(` se esperaba `)`"))
                    return NULL;
            }
            CallNode *cn = calloc(1, sizeof(CallNode));
            cn->base.type = NODE_CALL;
            cn->base.line = lcl;
            cn->base.col = lcc;
            cn->name = strdup("limpiar_consola");
            cn->n_args = 0;
            cn->args = NULL;
            return (ASTNode *)cn;
        }
        /* constante tipo nombre = expr (valor obligatorio) */
        if (strcmp(t->value.str, "constante") == 0) {
            advance(p);
            const Token *ty = peek(p, 0);
            if (!ty || !ty->value.str ||
                (strcmp(ty->value.str, "entero") != 0 && strcmp(ty->value.str, "texto") != 0 &&
                 strcmp(ty->value.str, "flotante") != 0 && strcmp(ty->value.str, "caracter") != 0 &&
                 strcmp(ty->value.str, "bool") != 0 && strcmp(ty->value.str, "u32") != 0 &&
                 strcmp(ty->value.str, "u64") != 0 && strcmp(ty->value.str, "u8") != 0 &&
                 strcmp(ty->value.str, "byte") != 0 && strcmp(ty->value.str, "vec2") != 0 &&
                 strcmp(ty->value.str, "vec3") != 0 && strcmp(ty->value.str, "vec4") != 0 && strcmp(ty->value.str, "mat4") != 0 && strcmp(ty->value.str, "mat3") != 0)) {
                set_error_here(p, ty ? ty : peek(p, 0),
                    "constante requiere un tipo (entero, texto, flotante, caracter, bool, u32, u64, u8, byte, vec2, vec3, vec4, mat4, mat3).");
                return NULL;
            }
            char *tyn = strdup(ty->value.str);
            advance(p);
            const Token *nt = peek(p, 0);
            if (!validate_user_defined_name_tok(p, nt)) {
                free(tyn);
                return NULL;
            }
            advance(p);
            char *nm = nt && nt->value.str ? strdup(nt->value.str) : NULL;
            if (!match(p, TOK_OPERATOR, "=")) {
                free(tyn);
                free(nm);
                set_error_here(p, peek(p, 0), "constante requiere valor inicial (= expresion).");
                return NULL;
            }
            ASTNode *val = parse_expression(p);
            if (!val) {
                free(tyn);
                free(nm);
                return NULL;
            }
            VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
            n->base.type = NODE_VAR_DECL;
            n->base.line = nt ? nt->line : (ty ? ty->line : 0);
            n->base.col = nt ? nt->column : (ty ? ty->column : 0);
            n->type_name = tyn;
            n->name = nm;
            n->value = val;
            n->is_const = 1;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "entero") == 0 || strcmp(t->value.str, "texto") == 0 ||
            strcmp(t->value.str, "flotante") == 0 || strcmp(t->value.str, "bool") == 0 ||
            strcmp(t->value.str, "caracter") == 0 || strcmp(t->value.str, "u32") == 0 ||
            strcmp(t->value.str, "u64") == 0 || strcmp(t->value.str, "u8") == 0 ||
            strcmp(t->value.str, "byte") == 0 || strcmp(t->value.str, "vec2") == 0 ||
            strcmp(t->value.str, "vec3") == 0 || strcmp(t->value.str, "vec4") == 0 || strcmp(t->value.str, "mat4") == 0 || strcmp(t->value.str, "mat3") == 0) {
            char *ty = strdup(t->value.str);
            advance(p);
            const Token *nt = peek(p, 0);
            if (!validate_user_defined_name_tok(p, nt)) {
                free(ty);
                return NULL;
            }
            advance(p);
            char *nm = nt && nt->value.str ? strdup(nt->value.str) : NULL;
            ASTNode *val = NULL;
            if (match(p, TOK_OPERATOR, "="))
                val = parse_expression(p);
            VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
            n->base.type = NODE_VAR_DECL;
            n->base.line = nt ? nt->line : t->line;
            n->base.col = nt ? nt->column : t->column;
            n->type_name = ty;
            n->name = nm;
            n->value = val;
            n->is_const = 0;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "lista") == 0) {
            char *ty = strdup("lista");
            advance(p);
            char *lista_el = parse_optional_lista_element_type(p);
            if (p->last_error) {
                free(ty);
                return NULL;
            }
            const Token *nt = peek(p, 0);
            if (!validate_user_defined_name_tok(p, nt)) {
                free(ty);
                free(lista_el);
                return NULL;
            }
            advance(p);
            char *nm = nt && nt->value.str ? strdup(nt->value.str) : NULL;
            ASTNode *val = NULL;
            if (match(p, TOK_OPERATOR, "="))
                val = parse_expression(p);
            VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
            n->base.type = NODE_VAR_DECL;
            n->base.line = nt ? nt->line : t->line;
            n->base.col = nt ? nt->column : t->column;
            n->type_name = ty;
            n->name = nm;
            n->value = val;
            n->is_const = 0;
            n->list_element_type = lista_el;
            return (ASTNode *)n;
        }
        if (strcmp(t->value.str, "seleccionar") == 0) {
            const Token *sel_tok = t;
            advance(p);
            ASTNode *selector = parse_expression(p);
            if (!selector) return NULL;
            if (!expect(p, TOK_KEYWORD, "hacer", "Se esperaba 'hacer' despues de 'seleccionar <expresion>'")) {
                ast_free(selector);
                return NULL;
            }
            SelectCaseVec cases = {0};
            ASTNode *default_body = NULL;
            while (peek(p, 0) && peek(p, 0)->value.str) {
                const Token *nt = peek(p, 0);
                if (nt->type == TOK_KEYWORD && strcmp(nt->value.str, "caso") == 0) {
                    advance(p);
                    NodeVec vals = {0};
                    int is_range = 0;
                    ASTNode *range_end = NULL;

                    if (match(p, TOK_KEYWORD, "rango")) {
                        is_range = 1;
                        ASTNode *r_start = parse_expression(p);
                        if (!r_start) {
                            ast_free(selector);
                            return NULL;
                        }
                        node_vec_push(&vals, r_start);
                        if (!expect(p, TOK_OPERATOR, "..", "Se esperaba '..' en caso rango")) {
                            ast_free(selector);
                            for (size_t k=0; k<vals.n; k++) { ast_free(vals.arr[k]); } free(vals.arr);
                            return NULL;
                        }
                        range_end = parse_expression(p);
                        if (!range_end) {
                            ast_free(selector);
                            for (size_t k=0; k<vals.n; k++) { ast_free(vals.arr[k]); } free(vals.arr);
                            return NULL;
                        }
                    } else {
                        ASTNode *v0 = parse_expression(p);
                        if (!v0) {
                            ast_free(selector);
                            return NULL;
                        }
                        node_vec_push(&vals, v0);
                        while (match(p, TOK_OPERATOR, ",")) {
                            ASTNode *vx = parse_expression(p);
                            if (!vx) break;
                            node_vec_push(&vals, vx);
                        }
                    }

                    const char *ends_case[] = {"caso", "defecto", "fin_seleccionar"};
                    ASTNode *body = parse_block(p, ends_case, 3);
                    if (p->last_error) {
                        ast_free(selector);
                        if (range_end) ast_free(range_end);
                        for (size_t k=0; k<vals.n; k++) { ast_free(vals.arr[k]); } free(vals.arr);
                        return NULL;
                    }
                    SelectCase sc = {0};
                    sc.values = vals.arr;
                    sc.n_values = vals.n;
                    sc.body = body;
                    sc.is_range = is_range;
                    sc.range_end = range_end;
                    if (select_case_vec_push(&cases, sc) != 0) {
                        ast_free(selector);
                        if (range_end) ast_free(range_end);
                        for (size_t k=0; k<vals.n; k++) { ast_free(vals.arr[k]); } free(vals.arr);
                        return NULL;
                    }
                    continue;
                }
                if (nt->type == TOK_KEYWORD && strcmp(nt->value.str, "defecto") == 0) {
                    advance(p);
                    const char *ends_def[] = {"fin_seleccionar"};
                    default_body = parse_block(p, ends_def, 1);
                    if (p->last_error) {
                        ast_free(selector);
                        return NULL;
                    }
                    break;
                }
                break;
            }
            if (!expect(p, TOK_KEYWORD, "fin_seleccionar", "Se esperaba 'fin_seleccionar'")) {
                ast_free(selector);
                for (size_t i = 0; i < cases.n; i++) {
                    for (size_t j = 0; j < cases.arr[i].n_values; j++) ast_free(cases.arr[i].values[j]);
                    free(cases.arr[i].values);
                    ast_free(cases.arr[i].body);
                }
                free(cases.arr);
                ast_free(default_body);
                return NULL;
            }
            SelectNode *sn = calloc(1, sizeof(SelectNode));
            sn->base.type = NODE_SELECT;
            sn->base.line = sel_tok->line;
            sn->base.col = sel_tok->column;
            sn->selector = selector;
            sn->cases = cases.arr;
            sn->n_cases = cases.n;
            sn->default_body = default_body;
            return (ASTNode*)sn;
        }
        if (strcmp(t->value.str, "intentar") == 0) {
            const Token *try_tok = t;
            advance(p);
            const char *ends_try[] = {"atrapar", "final", "fin_intentar"};
            ASTNode *try_body = parse_block(p, ends_try, 3);
            if (p->last_error) return NULL;
            char *catch_var = NULL;
            ASTNode *catch_body = NULL;
            ASTNode *final_body = NULL;

            if (match(p, TOK_KEYWORD, "atrapar")) {
                const Token *cv = peek(p, 0);
                if (cv && (cv->type == TOK_IDENTIFIER || cv->type == TOK_KEYWORD) && cv->value.str) {
                    if (!validate_user_defined_name_tok(p, cv)) return NULL;
                    catch_var = strdup_safe(cv->value.str);
                    advance(p);
                }
                const char *ends_catch[] = {"final", "fin_intentar"};
                catch_body = parse_block(p, ends_catch, 2);
                if (p->last_error) return NULL;
            }
            if (match(p, TOK_KEYWORD, "final")) {
                const char *ends_final[] = {"fin_intentar"};
                final_body = parse_block(p, ends_final, 1);
                if (p->last_error) return NULL;
            }
            if (!expect(p, TOK_KEYWORD, "fin_intentar", "Se esperaba 'fin_intentar'")) {
                ast_free(try_body);
                ast_free(catch_body);
                ast_free(final_body);
                free(catch_var);
                return NULL;
            }
            TryNode *tn = calloc(1, sizeof(TryNode));
            tn->base.type = NODE_TRY;
            tn->base.line = try_tok->line;
            tn->base.col = try_tok->column;
            tn->try_body = try_body;
            tn->catch_var = catch_var;
            tn->catch_body = catch_body;
            tn->final_body = final_body;
            return (ASTNode*)tn;
        }
        if (strcmp(t->value.str, "lanzar") == 0) {
            const Token *th_tok = t;
            advance(p);
            ASTNode *expr = parse_expression(p);
            if (!expr) return NULL;
            ThrowNode *tn = calloc(1, sizeof(ThrowNode));
            tn->base.type = NODE_THROW;
            tn->base.line = th_tok->line;
            tn->base.col = th_tok->column;
            tn->expression = expr;
            return (ASTNode*)tn;
        }
        if (strcmp(t->value.str, "mientras") == 0) {
            const Token *m_tok = t;
            advance(p);
            ASTNode *cond = parse_expression(p);
            
            if (match(p, TOK_KEYWORD, "fin_hacer")) {
                EndDoWhileNode *n = calloc(1, sizeof(EndDoWhileNode));
                n->base.type = NODE_END_DO_WHILE;
                n->condition = cond;
                return (ASTNode*)n;
            }
            
            if (!expect(p, TOK_KEYWORD, "hacer", "Se esperaba 'hacer' despues de la condicion del 'mientras'")) {
                if (cond) ast_free(cond);
                return NULL;
            }
            const char *ends[] = {"fin_mientras"};
            ASTNode *body = parse_block(p, ends, 1);
            if (p->last_error) return NULL;
            if (!match(p, TOK_KEYWORD, "fin_mientras")) {
                const Token *next_t = peek(p, 0);
                if (next_t && next_t->type == TOK_KEYWORD && 
                    (strcmp(next_t->value.str, "fin_principal") == 0 || strcmp(next_t->value.str, "fin_funcion") == 0)) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, m_tok->line, m_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque 'mientras' iniciado en esta linea no fue cerrado. Se llego al final del entorno ('%s') sin encontrar su 'fin_mientras'.", p->source_path, m_tok->line, m_tok->column, next_t->value.str);
                    else
                        set_error_at(p, m_tok->line, m_tok->column, 
                                  "linea %d, columna %d: El bloque 'mientras' iniciado en esta linea no fue cerrado. Se llego al final del entorno ('%s') sin encontrar su 'fin_mientras'.", m_tok->line, m_tok->column, next_t->value.str);
                } else if (next_t) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, m_tok->line, m_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque 'mientras' iniciado en esta linea no fue cerrado correctamente. Se esperaba 'fin_mientras', pero se encontro '%s'.", p->source_path, m_tok->line, m_tok->column, next_t->value.str);
                    else
                        set_error_at(p, m_tok->line, m_tok->column, 
                                  "linea %d, columna %d: El bloque 'mientras' iniciado en esta linea no fue cerrado correctamente. Se esperaba 'fin_mientras', pero se encontro '%s'.", m_tok->line, m_tok->column, next_t->value.str);
                } else {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, m_tok->line, m_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque 'mientras' iniciado en esta linea no fue cerrado con 'fin_mientras'.", p->source_path, m_tok->line, m_tok->column);
                    else
                        set_error_at(p, m_tok->line, m_tok->column, 
                                  "linea %d, columna %d: El bloque 'mientras' iniciado en esta linea no fue cerrado con 'fin_mientras'.", m_tok->line, m_tok->column);
                }
                return NULL;
            }
            WhileNode *n = calloc(1, sizeof(WhileNode));
            n->base.type = NODE_WHILE;
            n->condition = cond;
            n->body = body;
            n->base.line = m_tok->line;
            n->base.col = m_tok->column;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "para_cada") == 0) {
            const Token *fe_tok = t;
            advance(p);
            const Token *ity = peek(p, 0);
            if (!ity || ity->type != TOK_KEYWORD || !ity->value.str) {
                set_error_here(p, ity ? ity : peek(p, 0),
                               "para_cada requiere un tipo de elemento (entero, flotante, texto, bool) antes del nombre de variable.");
                return NULL;
            }
            static const char *ok_it[] = {"entero", "flotante", "texto", "bool", NULL};
            int it_ok = 0;
            for (int i = 0; ok_it[i]; i++)
                if (strcmp(ity->value.str, ok_it[i]) == 0) it_ok = 1;
            if (!it_ok) {
                set_error_here(p, ity, "para_cada: el tipo de elemento debe ser entero, flotante, texto o bool.");
                return NULL;
            }
            char *iter_ty = strdup(ity->value.str);
            if (!iter_ty) return NULL;
            advance(p);
            const Token *id_tok = peek(p, 0);
            if (!validate_user_defined_name_tok(p, id_tok)) {
                free(iter_ty);
                return NULL;
            }
            char *iter_nm = id_tok && id_tok->value.str ? strdup(id_tok->value.str) : NULL;
            advance(p);
            if (!expect(p, TOK_KEYWORD, "sobre", "Se esperaba 'sobre' despues del nombre de variable en para_cada")) {
                free(iter_ty);
                free(iter_nm);
                return NULL;
            }
            ASTNode *coll = parse_expression(p);
            if (!coll) {
                free(iter_ty);
                free(iter_nm);
                return NULL;
            }
            if (!expect(p, TOK_KEYWORD, "hacer", "Se esperaba 'hacer' despues de la expresion de lista en para_cada")) {
                ast_free(coll);
                free(iter_ty);
                free(iter_nm);
                return NULL;
            }
            const char *fe_ends[] = {"fin_para_cada"};
            ASTNode *fe_body = parse_block(p, fe_ends, 1);
            if (p->last_error) {
                ast_free(coll);
                free(iter_ty);
                free(iter_nm);
                return NULL;
            }
            if (!match(p, TOK_KEYWORD, "fin_para_cada")) {
                const Token *next_t = peek(p, 0);
                if (next_t && next_t->type == TOK_KEYWORD && next_t->value.str) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, fe_tok->line, fe_tok->column,
                                     "Archivo %s, linea %d, columna %d: bloque 'para_cada' sin cierre 'fin_para_cada' (se encontro '%s').",
                                     p->source_path, fe_tok->line, fe_tok->column, next_t->value.str);
                    else
                        set_error_at(p, fe_tok->line, fe_tok->column,
                                     "linea %d, columna %d: bloque 'para_cada' sin cierre 'fin_para_cada' (se encontro '%s').",
                                     fe_tok->line, fe_tok->column, next_t->value.str);
                } else {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, fe_tok->line, fe_tok->column,
                                     "Archivo %s, linea %d, columna %d: bloque 'para_cada' sin cierre 'fin_para_cada'.",
                                     p->source_path, fe_tok->line, fe_tok->column);
                    else
                        set_error_at(p, fe_tok->line, fe_tok->column,
                                     "linea %d, columna %d: bloque 'para_cada' sin cierre 'fin_para_cada'.",
                                     fe_tok->line, fe_tok->column);
                }
                ast_free(coll);
                ast_free(fe_body);
                free(iter_ty);
                free(iter_nm);
                return NULL;
            }
            ForEachNode *n = calloc(1, sizeof(ForEachNode));
            n->base.type = NODE_FOREACH;
            n->base.line = fe_tok->line;
            n->base.col = fe_tok->column;
            n->iter_type = iter_ty;
            n->iter_name = iter_nm;
            n->collection = coll;
            n->body = fe_body;
            return (ASTNode *)n;
        }
        if (strcmp(t->value.str, "hacer") == 0) {
            const Token *h_tok = t;
            advance(p);
            
            // Usamos palabras clave de cierre de entorno como seguridad
            const char *ends[] = {"fin_principal", "fin_funcion"};
            ASTNode *body = parse_block(p, ends, 2);
            if (p->last_error) return NULL;
            
            BlockNode *bn = (BlockNode*)body;
            if (bn->n == 0 || bn->statements[bn->n - 1]->type != NODE_END_DO_WHILE) {
                const Token *next_t = peek(p, 0);
                if (next_t && next_t->type == TOK_KEYWORD && 
                    (strcmp(next_t->value.str, "fin_principal") == 0 || strcmp(next_t->value.str, "fin_funcion") == 0)) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, h_tok->line, h_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque 'hacer' iniciado en esta linea no encontro su 'mientras'. Se llego al final del entorno ('%s').", p->source_path, h_tok->line, h_tok->column, next_t->value.str);
                    else
                        set_error_at(p, h_tok->line, h_tok->column, 
                                  "linea %d, columna %d: El bloque 'hacer' iniciado en esta linea no encontro su 'mientras'. Se llego al final del entorno ('%s').", h_tok->line, h_tok->column, next_t->value.str);
                } else if (next_t) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, h_tok->line, h_tok->column, 
                                  "Archivo %s, linea %d, columna %d: Se esperaba 'mientras ... fin_hacer' para cerrar el bloque 'hacer', pero se encontro '%s'.", p->source_path, h_tok->line, h_tok->column, next_t->value.str);
                    else
                        set_error_at(p, h_tok->line, h_tok->column, 
                                  "linea %d, columna %d: Se esperaba 'mientras ... fin_hacer' para cerrar el bloque 'hacer', pero se encontro '%s'.", h_tok->line, h_tok->column, next_t->value.str);
                } else {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, h_tok->line, h_tok->column, 
                                  "Archivo %s, linea %d, columna %d: Se esperaba 'mientras ... fin_hacer' para cerrar el bloque 'hacer'.", p->source_path, h_tok->line, h_tok->column);
                    else
                        set_error_at(p, h_tok->line, h_tok->column, 
                                  "linea %d, columna %d: Se esperaba 'mientras ... fin_hacer' para cerrar el bloque 'hacer'.", h_tok->line, h_tok->column);
                }
                if (body) ast_free(body);
                return NULL;
            }
            
            EndDoWhileNode *ewn = (EndDoWhileNode*)bn->statements[bn->n - 1];
            ASTNode *cond = ewn->condition;
            
            // Retiramos el nodo END_DO_WHILE del bloque
            bn->n--;
            free(ewn); // Solo liberamos el nodo contenedor, conservamos la condicion
            
            DoWhileNode *n = calloc(1, sizeof(DoWhileNode));
            n->base.type = NODE_DO_WHILE;
            n->condition = cond;
            n->body = body;
            n->base.line = h_tok->line;
            n->base.col = h_tok->column;
            return (ASTNode*)n;
        }
        const Token *si_tok = t;
        if (strcmp(t->value.str, "cuando") == 0 || strcmp(t->value.str, "si") == 0) {
            advance(p);
            ASTNode *cond = parse_expression(p);
            
            if (strcmp(si_tok->value.str, "si") == 0) {
                if (!expect(p, TOK_KEYWORD, "entonces", "Se esperaba 'entonces' despues de la condicion del 'si'")) {
                    if (cond) ast_free(cond);
                    return NULL;
                }
            } else {
                if (!expect(p, TOK_KEYWORD, "entonces", "Se esperaba 'entonces' despues de la condicion del 'cuando'")) {
                    if (cond) ast_free(cond);
                    return NULL;
                }
            }
            const char *ends[] = {"fin_cuando", "fin_si", "sino"};
            ASTNode *body = parse_block(p, ends, 3);
            
            // Check if error occurred inside block
            if (p->last_error) return NULL;

            ASTNode *else_b = NULL;
            int found_sino = 0;
            if (match(p, TOK_KEYWORD, "sino")) {
                found_sino = 1;
                if (peek(p, 0) && peek(p, 0)->value.str && 
                   (strcmp(peek(p, 0)->value.str, "si") == 0 || strcmp(peek(p, 0)->value.str, "cuando") == 0)) {
                    else_b = parse_statement(p);
                } else {
                    const char *e2[] = {"fin_cuando", "fin_si"};
                    else_b = parse_block(p, e2, 2);
                    if (p->last_error) return NULL;
                    if (!match(p, TOK_KEYWORD, "fin_cuando")) {
                        if (!match(p, TOK_KEYWORD, "fin_si")) {
                            const Token *next_t = peek(p, 0);
                            if (next_t && next_t->type == TOK_KEYWORD && 
                                (strcmp(next_t->value.str, "fin_principal") == 0 || strcmp(next_t->value.str, "fin_funcion") == 0)) {
                                if (p->source_path && p->source_path[0])
                                    set_error_at(p, si_tok->line, si_tok->column, 
                                              "Archivo %s, linea %d, columna %d: El bloque '%s' iniciado en esta linea (o uno de sus 'sino') no fue cerrado. Se llego al final del entorno ('%s') sin encontrar su 'fin_si'.", p->source_path, si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                                else
                                    set_error_at(p, si_tok->line, si_tok->column, 
                                              "linea %d, columna %d: El bloque '%s' iniciado en esta linea (o uno de sus 'sino') no fue cerrado. Se llego al final del entorno ('%s') sin encontrar su 'fin_si'.", si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                            } else if (next_t) {
                                if (p->source_path && p->source_path[0])
                                    set_error_at(p, si_tok->line, si_tok->column, 
                                              "Archivo %s, linea %d, columna %d: El bloque '%s' iniciado en esta linea (o su 'sino') no fue cerrado correctamente. Se esperaba 'fin_si' o 'fin_cuando', pero se encontro '%s'.", p->source_path, si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                                else
                                    set_error_at(p, si_tok->line, si_tok->column, 
                                              "linea %d, columna %d: El bloque '%s' iniciado en esta linea (o su 'sino') no fue cerrado correctamente. Se esperaba 'fin_si' o 'fin_cuando', pero se encontro '%s'.", si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                            } else {
                                if (p->source_path && p->source_path[0])
                                    set_error_at(p, si_tok->line, si_tok->column, 
                                              "Archivo %s, linea %d, columna %d: El bloque '%s' iniciado en esta linea (o su 'sino') no fue cerrado correctamente. Falta un 'fin_si' o 'fin_cuando'.", p->source_path, si_tok->line, si_tok->column, si_tok->value.str);
                                else
                                    set_error_at(p, si_tok->line, si_tok->column, 
                                              "linea %d, columna %d: El bloque '%s' iniciado en esta linea (o su 'sino') no fue cerrado correctamente. Falta un 'fin_si' o 'fin_cuando'.", si_tok->line, si_tok->column, si_tok->value.str);
                            }
                            return NULL;
                        }
                    }
                }
            } else {
        if (!match(p, TOK_KEYWORD, "fin_cuando")) {
            if (!match(p, TOK_KEYWORD, "fin_si")) {
                const Token *next_t = peek(p, 0);
                if (next_t && next_t->type == TOK_KEYWORD && 
                    (strcmp(next_t->value.str, "fin_principal") == 0 || strcmp(next_t->value.str, "fin_funcion") == 0)) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, si_tok->line, si_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque '%s' iniciado en esta linea (o uno de sus 'sino') no fue cerrado. Se llego al final del entorno ('%s') sin encontrar su 'fin_si'.", p->source_path, si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                    else
                        set_error_at(p, si_tok->line, si_tok->column, 
                                  "linea %d, columna %d: El bloque '%s' iniciado en esta linea (o uno de sus 'sino') no fue cerrado. Se llego al final del entorno ('%s') sin encontrar su 'fin_si'.", si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                } else if (next_t) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, si_tok->line, si_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque '%s' iniciado en esta linea no fue cerrado correctamente. Se esperaba 'fin_si' o 'fin_cuando', pero se encontro '%s'.", p->source_path, si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                    else
                        set_error_at(p, si_tok->line, si_tok->column, 
                                  "linea %d, columna %d: El bloque '%s' iniciado en esta linea no fue cerrado correctamente. Se esperaba 'fin_si' o 'fin_cuando', pero se encontro '%s'.", si_tok->line, si_tok->column, si_tok->value.str, next_t->value.str);
                } else {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, si_tok->line, si_tok->column, 
                                  "Archivo %s, linea %d, columna %d: El bloque '%s' iniciado en esta linea no fue cerrado correctamente. Falta un 'fin_si' o 'fin_cuando'.", p->source_path, si_tok->line, si_tok->column, si_tok->value.str);
                    else
                        set_error_at(p, si_tok->line, si_tok->column, 
                                  "linea %d, columna %d: El bloque '%s' iniciado en esta linea no fue cerrado correctamente. Falta un 'fin_si' o 'fin_cuando'.", si_tok->line, si_tok->column, si_tok->value.str);
                }
                return NULL;
            }
        }
            }
            IfNode *n = calloc(1, sizeof(IfNode));
            n->base.type = NODE_IF;
            n->condition = cond;
            n->body = body;
            n->else_body = else_b;
            n->base.line = si_tok->line;
            n->base.col = si_tok->column;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "romper") == 0) {
            int l = t->line, c = t->column;
            advance(p);
            BreakNode *n = calloc(1, sizeof(BreakNode));
            n->base.type = NODE_BREAK;
            n->base.line = l;
            n->base.col = c;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "continuar") == 0) {
            int l = t->line, c = t->column;
            advance(p);
            ContinueNode *n = calloc(1, sizeof(ContinueNode));
            n->base.type = NODE_CONTINUE;
            n->base.line = l;
            n->base.col = c;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "ingresar_texto") == 0 || strcmp(t->value.str, "ingreso_inmediato") == 0) {
            int immediate = (strcmp(t->value.str, "ingreso_inmediato") == 0);
            advance(p);
            const Token *vt = peek(p, 0);
            if (!validate_user_defined_name_tok(p, vt)) return NULL;
            advance(p);
            InputNode *n = calloc(1, sizeof(InputNode));
            n->base.type = NODE_INPUT;
            n->variable = vt && vt->value.str ? strdup(vt->value.str) : NULL;
            n->immediate = immediate ? 1 : 0;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "retornar") == 0) {
            int l = t->line, c = t->column;
            advance(p);
            ASTNode *e = NULL;
            const Token *nx_ret = peek(p, 0);
            if (nx_ret) {
                int kw_starts_expr = nx_ret->type == TOK_KEYWORD && nx_ret->value.str &&
                    (strcmp(nx_ret->value.str, "llamar") == 0 || strcmp(nx_ret->value.str, "no") == 0);
                if (nx_ret->type != TOK_KEYWORD || kw_starts_expr)
                    e = parse_expression(p);
            }
            ReturnNode *n = calloc(1, sizeof(ReturnNode));
            n->base.type = NODE_RETURN;
            n->base.line = l;
            n->base.col = c;
            n->expression = e;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "recordar") == 0) {
            advance(p);
            ASTNode *key = parse_expression(p);
            ASTNode *val = NULL;
            if (match(p, TOK_KEYWORD, "con") && match(p, TOK_KEYWORD, "valor"))
                val = parse_expression(p);
            RecordarNode *n = calloc(1, sizeof(RecordarNode));
            n->base.type = NODE_RECORDAR;
            n->key = key;
            n->value = val;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "responder") == 0) {
            advance(p);
            ASTNode *msg = parse_expression(p);
            ResponderNode *n = calloc(1, sizeof(ResponderNode));
            n->base.type = NODE_RESPONDER;
            n->message = msg;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "activar_modulo") == 0 || strcmp(t->value.str, "usar") == 0) {
            advance(p);
            ASTNode *path = parse_expression(p);
            ActivarModuloNode *n = calloc(1, sizeof(ActivarModuloNode));
            n->base.type = NODE_ACTIVAR_MODULO;
            n->module_path = path;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "biblioteca") == 0) {
            advance(p);
            ASTNode *path = parse_expression(p);
            BibliotecaNode *n = calloc(1, sizeof(BibliotecaNode));
            n->base.type = NODE_BIBLIOTECA;
            n->library_path = path;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "crear_memoria") == 0) {
            advance(p);
            expect(p, TOK_OPERATOR, "(", NULL);
            ASTNode *fn = parse_expression(p);
            ASTNode *nc = NULL, *cc = NULL;
            if (match(p, TOK_OPERATOR, ",")) nc = parse_expression(p);
            if (match(p, TOK_OPERATOR, ",")) cc = parse_expression(p);
            expect(p, TOK_OPERATOR, ")", NULL);
            CrearMemoriaNode *n = calloc(1, sizeof(CrearMemoriaNode));
            n->base.type = NODE_CREAR_MEMORIA;
            n->filename = fn;
            n->nodes_capacity = nc;
            n->connections_capacity = cc;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "cerrar_memoria") == 0) {
            advance(p);
            match(p, TOK_OPERATOR, "(");
            match(p, TOK_OPERATOR, ")");
            CerrarMemoriaNode *n = calloc(1, sizeof(CerrarMemoriaNode));
            n->base.type = NODE_CERRAR_MEMORIA;
            return (ASTNode*)n;
        }
        if (is_sistema_llamada(t->value.str, strlen(t->value.str))) {
            /* Call como statement */
            char *name = strdup(t->value.str);
            advance(p);
            NodeVec args = {0};
            if (match(p, TOK_OPERATOR, "(")) {
                for (;;) {
                    const Token *nx = peek(p, 0);
                    if (!nx || nx->type == TOK_EOF) break;
                    if (nx->type == TOK_OPERATOR && nx->value.str && strcmp(nx->value.str, ")") == 0)
                        break;
                    ASTNode *a = parse_expression(p);
                    if (!a) break;
                    node_vec_push(&args, a);
                    if (!match(p, TOK_OPERATOR, ",")) break;
                }
                expect(p, TOK_OPERATOR, ")", NULL);
            } else {
                ASTNode *a = parse_expression(p);
                if (a) node_vec_push(&args, a);
            }
            CallNode *cn = calloc(1, sizeof(CallNode));
            cn->base.type = NODE_CALL;
            cn->base.line = t->line;
            cn->base.col = t->column;
            cn->name = name;
            cn->args = args.arr;
            cn->n_args = args.n;
            return (ASTNode*)cn;
        }
    }

    /* Identifier o keyword que puede ser declaración TYPE ID o assignment/call */
        if ((t->type == TOK_IDENTIFIER || t->type == TOK_KEYWORD) && t->value.str) {
        const Token *nxt = peek(p, 1);
        if (nxt && nxt->type == TOK_IDENTIFIER && nxt->value.str) {
            /* Posible TYPE ID = expr */
            if (is_keyword(t->value.str, strlen(t->value.str)) &&
                (strcmp(t->value.str, "entero") == 0 || strcmp(t->value.str, "texto") == 0 ||
                 strcmp(t->value.str, "flotante") == 0 || strcmp(t->value.str, "caracter") == 0 ||
                 strcmp(t->value.str, "lista") == 0 || strcmp(t->value.str, "mapa") == 0 ||
                 strcmp(t->value.str, "bool") == 0 || strcmp(t->value.str, "u32") == 0 ||
                 strcmp(t->value.str, "u64") == 0 || strcmp(t->value.str, "u8") == 0 ||
                 strcmp(t->value.str, "byte") == 0 || strcmp(t->value.str, "vec2") == 0 ||
                 strcmp(t->value.str, "vec3") == 0 || strcmp(t->value.str, "vec4") == 0 || strcmp(t->value.str, "mat4") == 0 || strcmp(t->value.str, "mat3") == 0 || strcmp(t->value.str, "macro") == 0 ||
                 strcmp(t->value.str, "funcion") == 0)) {
                char *ty = strdup(t->value.str);
                advance(p);
                char *lista_el = NULL;
                if (ty && strcmp(ty, "lista") == 0) {
                    lista_el = parse_optional_lista_element_type(p);
                    if (p->last_error) {
                        free(ty);
                        return NULL;
                    }
                }
                const Token *name_tok = peek(p, 0);
                if (!validate_user_defined_name_tok(p, name_tok)) {
                    free(ty);
                    free(lista_el);
                    return NULL;
                }
                char *nm = name_tok && name_tok->value.str ? strdup(name_tok->value.str) : NULL;
                advance(p);
                ASTNode *val = NULL;
                if (match(p, TOK_OPERATOR, "=")) {
                    if (strcmp(ty, "macro") == 0) {
                        val = parse_lambda(p);
                    } else {
                        val = parse_expression(p);
                    }
                    if (!val) {
                        if (!p->last_error && nm) {
                            if (p->source_path && p->source_path[0])
                                set_error_at(p, name_tok ? name_tok->line : 0, name_tok ? name_tok->column : 0,
                                    "Archivo %s, linea %d, columna %d: no se pudo analizar la definicion de la macro `%s` (revisar parametros `( ... ) =>` y el cuerpo).",
                                    p->source_path, name_tok ? name_tok->line : 0, name_tok ? name_tok->column : 0, nm);
                            else
                                set_error_at(p, name_tok ? name_tok->line : 0, name_tok ? name_tok->column : 0,
                                    "linea %d, columna %d: no se pudo analizar la definicion de la macro `%s` (revisar parametros `( ... ) =>` y el cuerpo).",
                                    name_tok ? name_tok->line : 0, name_tok ? name_tok->column : 0, nm);
                        }
                        free(ty);
                        free(lista_el);
                        free(nm);
                        return NULL;
                    }
                }
                VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
                n->base.type = NODE_VAR_DECL;
                n->base.line = name_tok ? name_tok->line : t->line;
                n->base.col = name_tok ? name_tok->column : t->column;
                n->type_name = ty;
                n->name = nm;
                n->value = val;
                n->is_const = 0;
                n->list_element_type = lista_el;
                return (ASTNode*)n;
            }
            /* Tipo con nombre de registro (identificador): ej. Punto p [, = expr] */
            if (t->type == TOK_IDENTIFIER) {
                if (!validate_user_defined_name_tok(p, t)) return NULL;
                if (!validate_user_defined_name_tok(p, nxt)) return NULL;
                char *ty = strdup(t->value.str);
                if (!ty) return NULL;
                advance(p);
                const Token *name_tok = peek(p, 0);
                if (!name_tok || !name_tok->value.str) {
                    free(ty);
                    return NULL;
                }
                char *nm = strdup(name_tok->value.str);
                if (!nm) {
                    free(ty);
                    return NULL;
                }
                advance(p);
                ASTNode *val = NULL;
                if (match(p, TOK_OPERATOR, "=")) {
                    val = parse_expression(p);
                    if (!val) {
                        free(ty);
                        free(nm);
                        return NULL;
                    }
                }
                VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
                n->base.type = NODE_VAR_DECL;
                n->base.line = name_tok ? name_tok->line : t->line;
                n->base.col = name_tok ? name_tok->column : t->column;
                n->type_name = ty;
                n->name = nm;
                n->value = val;
                n->is_const = 0;
                return (ASTNode*)n;
            }
        }
        /* Expresion completa (incluye i++, i + j, llamadas) o asignacion */
        ASTNode *node = parse_expression(p);
        if (!node) return NULL;
        if (match(p, TOK_OPERATOR, "=")) {
            ASTNode *expr = parse_expression(p);
            AssignmentNode *a = calloc(1, sizeof(AssignmentNode));
            a->base.type = NODE_ASSIGNMENT;
            a->target = node;
            a->expression = expr;
            return (ASTNode*)a;
        }
        /* += -= *= /= %=  ->  target = target op rhs */
        {
            const Token *ct = peek(p, 0);
            const char *binop = NULL;
            if (ct && ct->type == TOK_OPERATOR && ct->value.str) {
                if (strcmp(ct->value.str, "+=") == 0) binop = "+";
                else if (strcmp(ct->value.str, "-=") == 0) binop = "-";
                else if (strcmp(ct->value.str, "*=") == 0) binop = "*";
                else if (strcmp(ct->value.str, "/=") == 0) binop = "/";
                else if (strcmp(ct->value.str, "%=") == 0) binop = "%";
            }
            if (binop) {
                if (node->type != NODE_IDENTIFIER) {
                    set_error_here(p, ct,
                        "los operadores compuestos (+=, -=, *=, /=, %=) solo aplican a una variable simple");
                    ast_free(node);
                    return NULL;
                }
                advance(p);
                ASTNode *rhs = parse_expression(p);
                if (!rhs) {
                    ast_free(node);
                    return NULL;
                }
                ASTNode *lread = clone_identifier_for_compound((IdentifierNode*)node);
                if (!lread) {
                    ast_free(node);
                    ast_free(rhs);
                    return NULL;
                }
                BinaryOpNode *bn = calloc(1, sizeof(BinaryOpNode));
                bn->base.type = NODE_BINARY_OP;
                bn->left = lread;
                bn->right = rhs;
                bn->operator = strdup_safe(binop);
                bn->line = ct->line;
                bn->col = ct->column;
                AssignmentNode *a = calloc(1, sizeof(AssignmentNode));
                a->base.type = NODE_ASSIGNMENT;
                a->target = node;
                a->expression = (ASTNode*)bn;
                return (ASTNode*)a;
            }
        }
        return node;
    }
    return NULL;
}

static ASTNode *parse_function(Parser *p, int is_exported) {
    const Token *func_tok = peek(p, 0);
    expect(p, TOK_KEYWORD, "funcion", NULL);
    const Token *nt = peek(p, 0);
    
    // Validar que el token sea candidato a nombre (identificador o keyword);
    // si es keyword reservada, validate_user_defined_name_tok dara el mensaje especifico.
    if (!nt || (nt->type != TOK_IDENTIFIER && nt->type != TOK_KEYWORD)) {
        if (p->source_path && p->source_path[0])
            set_error_at(p, nt ? nt->line : func_tok->line, nt ? nt->column : func_tok->column,
                      "Archivo %s, linea %d, columna %d: declaracion de funcion invalida. Se esperaba un nombre para la funcion, pero se encontro `%s`.",
                      p->source_path, nt ? nt->line : func_tok->line, nt ? nt->column : func_tok->column, nt && nt->value.str ? nt->value.str : "EOF");
        else
            set_error_at(p, nt ? nt->line : func_tok->line, nt ? nt->column : func_tok->column,
                      "linea %d, columna %d: declaracion de funcion invalida. Se esperaba un nombre para la funcion, pero se encontro `%s`.",
                      nt ? nt->line : func_tok->line, nt ? nt->column : func_tok->column, nt && nt->value.str ? nt->value.str : "EOF");
        
        parser_accumulate_error(p, p->last_error);
        free(p->last_error);
        p->last_error = NULL;
        parser_synchronize(p);
        return NULL;
    }

    if (!validate_user_defined_name_tok(p, nt)) return NULL;
    advance(p);
    char *name = nt && nt->value.str ? strdup(nt->value.str) : NULL;
    NodeVec params = {0};
    if (match(p, TOK_OPERATOR, "(")) {
        for (;;) {
            const Token *nx = peek(p, 0);
            if (!nx || nx->type == TOK_EOF) break;
            if (nx->type == TOK_OPERATOR && nx->value.str && strcmp(nx->value.str, ")") == 0)
                break;
            const Token *ty_err = peek(p, 0);
            char *type_str = NULL;
            char *lista_el = NULL;
            if (ty_err && ty_err->type == TOK_KEYWORD && ty_err->value.str && strcmp(ty_err->value.str, "lista") == 0) {
                advance(p);
                type_str = strdup("lista");
                lista_el = parse_optional_lista_element_type(p);
                if (p->last_error) {
                    free(type_str);
                    if (lista_el) free(lista_el);
                    for (size_t k = 0; k < params.n; k++) ast_free((ASTNode *)params.arr[k]);
                    free(params.arr);
                    free(name);
                    return NULL;
                }
            } else {
                const Token *ty = advance(p);
                ty_err = ty;
                if (!is_decl_type_token(ty) &&
                    !(ty && ty->type == TOK_IDENTIFIER && validate_user_defined_name_tok(p, ty))) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, ty ? ty->line : 0, ty ? ty->column : 0,
                                  "Archivo %s, linea %d, columna %d: parametro invalido en firma de funcion. Se esperaba un tipo (entero/texto/flotante/...), pero se encontro `%s`.",
                                  p->source_path, ty ? ty->line : 0, ty ? ty->column : 0, ty && ty->value.str ? ty->value.str : "EOF");
                    else
                        set_error_at(p, ty ? ty->line : 0, ty ? ty->column : 0,
                                  "linea %d, columna %d: parametro invalido en firma de funcion. Se esperaba un tipo (entero/texto/flotante/...), pero se encontro `%s`.",
                                  ty ? ty->line : 0, ty ? ty->column : 0, ty && ty->value.str ? ty->value.str : "EOF");
                    for (size_t k = 0; k < params.n; k++) ast_free((ASTNode *)params.arr[k]);
                    free(params.arr);
                    free(name);
                    return NULL;
                }
                type_str = ty && ty->value.str ? strdup(ty->value.str) : NULL;
            }
            const Token *pn = peek(p, 0);
            if (!pn || pn->type == TOK_EOF ||
                (pn->type == TOK_OPERATOR && pn->value.str &&
                 (strcmp(pn->value.str, ",") == 0 || strcmp(pn->value.str, ")") == 0))) {
                if (p->source_path && p->source_path[0])
                    set_error_at(p, pn ? pn->line : ty_err->line, pn ? pn->column : ty_err->column,
                              "Archivo %s, linea %d, columna %d: parametro invalido: se declaro solo el tipo `%s` sin nombre de variable.",
                              p->source_path, pn ? pn->line : ty_err->line, pn ? pn->column : ty_err->column,
                              type_str ? type_str : "?");
                else
                    set_error_at(p, pn ? pn->line : ty_err->line, pn ? pn->column : ty_err->column,
                              "linea %d, columna %d: parametro invalido: se declaro solo el tipo `%s` sin nombre de variable.",
                              pn ? pn->line : ty_err->line, pn ? pn->column : ty_err->column,
                              type_str ? type_str : "?");
                free(type_str);
                free(lista_el);
                for (size_t k = 0; k < params.n; k++) ast_free((ASTNode *)params.arr[k]);
                free(params.arr);
                free(name);
                return NULL;
            }
            if (!validate_user_defined_name_tok(p, pn)) {
                free(type_str);
                free(lista_el);
                for (size_t k = 0; k < params.n; k++) ast_free((ASTNode *)params.arr[k]);
                free(params.arr);
                free(name);
                return NULL;
            }
            advance(p);
            VarDeclNode *vd = calloc(1, sizeof(VarDeclNode));
            vd->base.type = NODE_VAR_DECL;
            vd->base.line = pn ? pn->line : (ty_err ? ty_err->line : 0);
            vd->base.col = pn ? pn->column : (ty_err ? ty_err->column : 0);
            vd->type_name = type_str;
            vd->name = pn && pn->value.str ? strdup(pn->value.str) : NULL;
            vd->value = NULL;
            vd->list_element_type = lista_el;
            node_vec_push(&params, (ASTNode*)vd);
            if (!match(p, TOK_OPERATOR, ",")) break;
        }
        
        // Consumir el final de los parámetros (el paréntesis) para evitar cascada de errores de parser
        if (!match(p, TOK_OPERATOR, ")")) {
            const Token *err_t = peek(p, 0);
            if (p->source_path && p->source_path[0])
                set_error_at(p, err_t ? err_t->line : 0, err_t ? err_t->column : 0,
                          "Archivo %s, linea %d, columna %d: error de sintaxis: falta parentesis de cierre ')' en la definicion de parametros de la funcion.",
                          p->source_path, err_t ? err_t->line : 0, err_t ? err_t->column : 0);
            else
                set_error_at(p, err_t ? err_t->line : 0, err_t ? err_t->column : 0,
                          "linea %d, columna %d: error de sintaxis: falta parentesis de cierre ')' en la definicion de parametros de la funcion.",
                          err_t ? err_t->line : 0, err_t ? err_t->column : 0);
            
            // Registramos el error en la cola acumulada
            parser_accumulate_error(p, p->last_error);
            free(p->last_error);
            p->last_error = NULL;

            // Sincronizar consumiendo tokens hasta encontrar algo que parezca el cuerpo de la función o su cierre
            while (peek(p, 0) && peek(p, 0)->type != TOK_EOF) {
                const Token *t_sync = peek(p, 0);
                if (t_sync->type == TOK_KEYWORD && t_sync->value.str) {
                    if (strcmp(t_sync->value.str, "retorna") == 0 || 
                        strcmp(t_sync->value.str, "fin_funcion") == 0 ||
                        is_decl_type_token(t_sync)) {
                        break;
                    }
                }
                advance(p);
            }
            
            for(size_t i = 0; i < params.n; i++) ast_free((ASTNode*)params.arr[i]);
            free(params.arr);
            free(name);
            return NULL;
        }
    }
    if (!match(p, TOK_KEYWORD, "retorna") && peek(p, 0) && peek(p, 0)->value.str && strcmp(peek(p, 0)->value.str, "retorna") == 0)
        advance(p);  /* consumir "retorna" si match fallo (p.ej. lexed como ID) */
    char *ret_type = strdup("entero");
    /* Solo consumir tipo de retorno si acabamos de ver "retorna" Y el siguiente token es un tipo válido */
    const Token *rt = peek(p, 0);
    if (rt && rt->value.str && strcmp(rt->value.str, "fin_funcion") != 0 &&
        (strcmp(rt->value.str, "entero") == 0 || strcmp(rt->value.str, "texto") == 0 ||
         strcmp(rt->value.str, "flotante") == 0 || strcmp(rt->value.str, "caracter") == 0 ||
         strcmp(rt->value.str, "bool") == 0 || strcmp(rt->value.str, "lista") == 0 ||
         strcmp(rt->value.str, "mapa") == 0 || strcmp(rt->value.str, "u32") == 0 ||
         strcmp(rt->value.str, "u64") == 0 || strcmp(rt->value.str, "u8") == 0 ||
         strcmp(rt->value.str, "byte") == 0 || strcmp(rt->value.str, "vec2") == 0 ||
         strcmp(rt->value.str, "vec3") == 0 || strcmp(rt->value.str, "vec4") == 0 || strcmp(rt->value.str, "mat4") == 0 || strcmp(rt->value.str, "mat3") == 0)) {
        free(ret_type);
        ret_type = strdup_safe(rt->value.str);
        advance(p);
    }
    const char *ends[] = {"fin_funcion"};
    ASTNode *body = parse_block(p, ends, 1);
    
    // Check if we encountered an error while parsing the body and didn't find fin_funcion
    if (!match(p, TOK_KEYWORD, "fin_funcion")) {
        const Token *err_t = peek(p, 0);
        if (p->source_path && p->source_path[0])
            set_error_at(p, func_tok ? func_tok->line : 0, func_tok ? func_tok->column : 0,
                      "Archivo %s, linea %d, columna %d: bloque de `funcion` sin cierre. Falta `fin_funcion`.",
                      p->source_path, func_tok ? func_tok->line : 0, func_tok ? func_tok->column : 0);
        else
            set_error_at(p, func_tok ? func_tok->line : 0, func_tok ? func_tok->column : 0,
                      "linea %d, columna %d: bloque de `funcion` sin cierre. Falta `fin_funcion`.",
                      func_tok ? func_tok->line : 0, func_tok ? func_tok->column : 0);
        
        // Let it fall through, or free and return NULL?
        // We probably want to return NULL so the error propagates
        ast_free(body);
        for(size_t i = 0; i < params.n; i++) ast_free((ASTNode*)params.arr[i]);
        free(params.arr);
        free(name);
        free(ret_type);
        return NULL;
    }
    FunctionNode *fn = calloc(1, sizeof(FunctionNode));
    fn->base.type = NODE_FUNCTION;
    fn->base.line = func_tok ? func_tok->line : 0;
    fn->base.col = func_tok ? func_tok->column : 0;
    fn->name = name;
    fn->return_type = (char*)ret_type;
    fn->params = (ASTNode**)params.arr;
    fn->n_params = params.n;
    fn->body = body;
    fn->is_exported = is_exported ? 1 : 0;
    return (ASTNode*)fn;
}

static ASTNode *parse_struct_def(Parser *p) {
    expect(p, TOK_KEYWORD, "registro", NULL);
    const Token *nt = peek(p, 0);
    if (!validate_user_defined_name_tok(p, nt)) return NULL;
    advance(p);
    char *name = nt && nt->value.str ? strdup(nt->value.str) : NULL;
    char **ft = NULL, **fn = NULL;
    size_t nf = 0, cap = 0;
    while (peek(p, 0) && peek(p, 0)->value.str && strcmp(peek(p, 0)->value.str, "fin_registro") != 0) {
        const Token *tblk = peek(p, 0);
        if (tblk && tblk->type == TOK_KEYWORD && tblk->value.str) {
            const char *kw = tblk->value.str;
            if (strcmp(kw, "principal") == 0 || strcmp(kw, "funcion") == 0 || strcmp(kw, "registro") == 0 ||
                strcmp(kw, "activar_modulo") == 0 || strcmp(kw, "usar") == 0 || strcmp(kw, "fin_principal") == 0) {
                if (p->source_path && p->source_path[0])
                    set_error_at(p, tblk->line, tblk->column,
                        "Archivo %s, linea %d, columna %d: el registro '%s' no esta cerrado: aparece `%s` antes de `fin_registro`. "
                        "Cierre el registro con una linea `fin_registro` (después de los campos) y luego declare `%s` u otros bloques de nivel superior.",
                        p->source_path, tblk->line, tblk->column, name ? name : "?", kw, kw);
                else
                    set_error_at(p, tblk->line, tblk->column,
                        "linea %d, columna %d: el registro '%s' no esta cerrado: aparece `%s` antes de `fin_registro`. "
                        "Anada `fin_registro` después de los campos.",
                        tblk->line, tblk->column, name ? name : "?", kw);
                goto parse_struct_def_fail;
            }
        }
        const Token *ty = advance(p);
        const Token *fld = peek(p, 0);
        if (!ty || !fld) break;
        if (!validate_user_defined_name_tok(p, fld)) {
            for (size_t i = 0; i < nf; i++) {
                free(ft[i]);
                free(fn[i]);
            }
            free(ft);
            free(fn);
            free(name);
            return NULL;
        }
        advance(p);
        if (nf >= cap) {
            cap = cap ? cap * 2 : 4;
            char **pft = realloc(ft, cap * sizeof(char*));
            char **pfn = realloc(fn, cap * sizeof(char*));
            if (!pft || !pfn) break;
            ft = pft;
            fn = pfn;
        }
        ft[nf] = ty->value.str ? strdup(ty->value.str) : NULL;
        fn[nf] = fld->value.str ? strdup(fld->value.str) : NULL;
        nf++;
    }
    {
        char finmsg[288];
        snprintf(finmsg, sizeof finmsg,
            "se esperaba `fin_registro` para cerrar el registro '%s'", name ? name : "?");
        if (!expect(p, TOK_KEYWORD, "fin_registro", finmsg))
            goto parse_struct_def_fail;
    }
    StructDefNode *sn = calloc(1, sizeof(StructDefNode));
    sn->base.type = NODE_STRUCT_DEF;
    sn->base.line = nt ? nt->line : 0;
    sn->base.col = nt ? nt->column : 0;
    sn->name = name;
    sn->field_types = ft;
    sn->field_names = fn;
    sn->n_fields = nf;
    return (ASTNode*)sn;
parse_struct_def_fail:
    for (size_t i = 0; i < nf; i++) {
        free(ft[i]);
        free(fn[i]);
    }
    free(ft);
    free(fn);
    free(name);
    return NULL;
}

static ASTNode *parse_main(Parser *p) {
    expect(p, TOK_KEYWORD, "principal", NULL);
    const char *ends[] = {"fin_principal"};
    ASTNode *b = parse_block(p, ends, 1);
    if (!b) return NULL;
    return b;
}

void parser_init(Parser *p, const TokenVec *tokens, const char *source_path, const char *source_text) {
    p->tokens = tokens;
    p->pos = 0;
    p->last_error = NULL;
    p->accumulated_errors = NULL;
    p->error_count = 0;
    p->source_path = source_path;
    p->source_text = source_text;
}

void parser_free(Parser *p) {
    if (p->last_error) {
        free(p->last_error);
        p->last_error = NULL;
    }
    if (p->accumulated_errors) {
        free(p->accumulated_errors);
        p->accumulated_errors = NULL;
    }
}

static void free_import_name_list(char **names, size_t n) {
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
}

static int import_name_list_contains(char **names, size_t n, const char *s) {
    if (!s) return 0;
    for (size_t i = 0; i < n; i++)
        if (names[i] && strcmp(names[i], s) == 0) return 1;
    return 0;
}

/* Tras consumir `usar` / `activar_modulo`. */
static ActivarModuloNode *parse_activar_modulo_directive(Parser *p, int kw_line, int kw_col) {
    ActivarModuloNode *an = calloc(1, sizeof(ActivarModuloNode));
    if (!an) return NULL;
    an->base.type = NODE_ACTIVAR_MODULO;
    an->base.line = kw_line;
    an->base.col = kw_col;
    an->import_kind = USAR_IMPORT_LEGACY;

    const Token *p1 = peek(p, 0);

    if (p1 && p1->type == TOK_KEYWORD && p1->value.str
        && (strcmp(p1->value.str, "todo") == 0 || strcmp(p1->value.str, "todas") == 0)) {
        advance(p);
        if (!expect(p, TOK_KEYWORD, "de", "Tras `usar todo` o `usar todas` se esperaba la palabra clave `de`.")) {
            free(an);
            return NULL;
        }
        const Token *pt = advance(p);
        if (!pt || pt->type != TOK_STRING || !pt->value.str) {
            set_error_here(p, pt ? pt : p1, "`usar todo de` / `usar todas de` requiere una ruta entre comillas (texto).");
            free(an);
            return NULL;
        }
        an->import_kind = USAR_IMPORT_TODO;
        an->module_path = make_literal_str(pt->value.str);
        an->base.line = pt->line;
        an->base.col = pt->column;
        return an;
    }

    if (p1 && p1->type == TOK_OPERATOR && p1->value.str && strcmp(p1->value.str, "{") == 0) {
        advance(p);
        char **names = NULL;
        size_t nn = 0, cap = 0;
        for (;;) {
            const Token *id = peek(p, 0);
            if (!id) {
                set_error_here(p, p1, "Lista `usar { ... }` incompleta (se esperaban identificadores o `}`).");
                free_import_name_list(names, nn);
                free(an);
                return NULL;
            }
            if (id->type == TOK_OPERATOR && id->value.str && strcmp(id->value.str, "}") == 0)
                break;
            if (!validate_user_defined_name_tok(p, id)) {
                free_import_name_list(names, nn);
                free(an);
                return NULL;
            }
            const char *inm = id->value.str;
            if (import_name_list_contains(names, nn, inm)) {
                set_error_here(p, id, "Nombre duplicado en la lista de `usar { ... }`.");
                free_import_name_list(names, nn);
                free(an);
                return NULL;
            }
            advance(p);
            if (nn >= cap) {
                cap = cap ? cap * 2 : 4;
                char **n2 = realloc(names, cap * sizeof(char *));
                if (!n2) {
                    free_import_name_list(names, nn);
                    free(an);
                    return NULL;
                }
                names = n2;
            }
            names[nn++] = strdup(inm);
            if (match(p, TOK_OPERATOR, ","))
                continue;
            const Token *cl = peek(p, 0);
            if (cl && cl->type == TOK_OPERATOR && cl->value.str && strcmp(cl->value.str, "}") == 0)
                break;
            set_error_here(p, id, "Tras un nombre en `usar { ... }` se esperaba `,` o `}`.");
            free_import_name_list(names, nn);
            free(an);
            return NULL;
        }
        if (nn == 0) {
            set_error_here(p, p1, "La lista de `usar { ... }` no puede estar vacia.");
            free_import_name_list(names, nn);
            free(an);
            return NULL;
        }
        if (!expect(p, TOK_OPERATOR, "}", "Falta el cierre `}` en `usar { ... }`.")) {
            free_import_name_list(names, nn);
            free(an);
            return NULL;
        }
        if (!expect(p, TOK_KEYWORD, "de", "Tras `usar { ... }` se esperaba `de`.")) {
            free_import_name_list(names, nn);
            free(an);
            return NULL;
        }
        const Token *pt = advance(p);
        if (!pt || pt->type != TOK_STRING || !pt->value.str) {
            set_error_here(p, pt ? pt : peek(p, 0), "`usar { ... } de` requiere una ruta entre comillas.");
            free_import_name_list(names, nn);
            free(an);
            return NULL;
        }
        an->import_kind = USAR_IMPORT_NAMES;
        an->import_names = names;
        an->n_import_names = nn;
        an->module_path = make_literal_str(pt->value.str);
        an->base.line = pt->line;
        an->base.col = pt->column;
        return an;
    }

    const Token *pt = peek(p, 0);
    if (pt && pt->type == TOK_STRING && pt->value.str) {
        set_error_here(p, pt,
            "Tras `usar` se espera `todas de` o `todo de` y luego la ruta entre comillas, "
            "o ` { n1, n2 } de` y la ruta entre comillas.");
        free(an);
        return NULL;
    }
    pt = advance(p);
    set_error_here(p, pt ? pt : peek(p, 0),
        "Tras `usar` se espera `todas de` o `todo de` y la ruta entre comillas, "
        "o ` { n1, n2 } de` y la ruta.");
    free(an);
    return NULL;
}

ASTNode *parser_parse(Parser *p) {
    NodeVec funcs = {0}, globals = {0}, main_stmts = {0};
    while (p->pos < token_vec_size(p->tokens)) {
        const Token *start_t = peek(p, 0);
        int stmt_line = start_t ? start_t->line : 0;
        int stmt_col = start_t ? start_t->column : 0;
        const Token *t = peek(p, 0);
        if (!t || t->type == TOK_EOF) break;

        if (t->type == TOK_KEYWORD && t->value.str) {
            if (strcmp(t->value.str, "enviar") == 0) {
                advance(p);
                const Token *nx = peek(p, 0);
                if (nx && nx->type == TOK_KEYWORD && nx->value.str && strcmp(nx->value.str, "funcion") == 0) {
                    ASTNode *fn = parse_function(p, 1);
                    if (!fn) {
                        if (p->last_error) {
                            parser_accumulate_error(p, p->last_error);
                            free(p->last_error);
                            p->last_error = NULL;
                            parser_synchronize(p);
                            continue;
                        }
                        break;
                    }
                    node_vec_push(&funcs, fn);
                    continue;
                }
                ASTNode *s = parse_statement(p);
                if (!s) {
                    if (p->last_error) {
                        parser_accumulate_error(p, p->last_error);
                        free(p->last_error);
                        p->last_error = NULL;
                        parser_synchronize(p);
                        continue;
                    }
                    break;
                }
                if (s->type == NODE_VAR_DECL) {
                    ((VarDeclNode *)s)->is_exported = 1;
                    node_vec_push(&globals, s);
                    continue;
                }
                set_error_here(p, start_t, "`enviar` solo puede preceder a `funcion` o a una declaracion global (tipo + nombre).");
                ast_free(s);
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                }
                parser_synchronize(p);
                continue;
            }
            if (strcmp(t->value.str, "funcion") == 0) {
                ASTNode *fn = parse_function(p, 0);
                if (!fn) {
                    if (p->last_error) {
                        parser_accumulate_error(p, p->last_error);
                        free(p->last_error);
                        p->last_error = NULL;
                        parser_synchronize(p);
                        continue;
                    }
                    break;
                }
                node_vec_push(&funcs, fn);
                continue;
            }
            if (strcmp(t->value.str, "principal") == 0) {
                ASTNode *mb = parse_main(p);
                if (!mb) {
                    if (p->last_error) {
                        parser_accumulate_error(p, p->last_error);
                        free(p->last_error);
                        p->last_error = NULL;
                        parser_synchronize(p);
                        continue;
                    }
                    break;
                }
                /* Si parse_block ya llego a EOF por falta de fin_principal,
                   no emitir un segundo error redundante desde expect(). */
                {
                    const Token *after_main = peek(p, 0);
                    if (!after_main || after_main->type == TOK_EOF) {
                        ast_free(mb);
                        continue;
                    }
                }
                if (!expect(p, TOK_KEYWORD, "fin_principal", "Se esperaba fin_principal")) {
                    if (p->last_error) {
                        parser_accumulate_error(p, p->last_error);
                        free(p->last_error);
                        p->last_error = NULL;
                        parser_synchronize(p);
                        continue;
                    }
                    break;
                }
                /* Copiar statements del block a main_stmts */
                BlockNode *bn = (BlockNode*)mb;
                for (size_t i = 0; i < bn->n; i++)
                    node_vec_push(&main_stmts, bn->statements[i]);
                free(bn->statements);
                bn->statements = NULL;
                bn->n = 0;
                ast_free(mb);
                continue;
            }
            if (strcmp(t->value.str, "registro") == 0) {
                ASTNode *sd = parse_struct_def(p);
                if (sd) {
                    node_vec_push(&globals, sd);
                } else if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    /* No parser_synchronize: dejaria el cursor tras `principal` y el resto
                       se analizaria como sentencias sueltas (mensaje engañoso "fuera de principal"). */
                    break;
                }
                continue;
            }
            if (strcmp(t->value.str, "fin_registro") == 0) {
                const Token *fr = t;
                if (p->source_path && p->source_path[0])
                    set_error_at(p, fr->line, fr->column,
                        "Archivo %s, linea %d, columna %d: `fin_registro` sin un `registro` que lo abra. "
                        "Cada registro debe empezar con `registro` y el nombre del tipo (por ejemplo `registro Cordenadas`) antes de los campos, y terminar con `fin_registro`.",
                        p->source_path, fr->line, fr->column);
                else
                    set_error_at(p, fr->line, fr->column,
                        "linea %d, columna %d: `fin_registro` sin un `registro` que lo abra. "
                        "Empiece con `registro <nombre>` antes de los campos.",
                        fr->line, fr->column);
                parser_accumulate_error(p, p->last_error);
                free(p->last_error);
                p->last_error = NULL;
                advance(p);
                continue;
            }
            if (strcmp(t->value.str, "activar_modulo") == 0 || strcmp(t->value.str, "usar") == 0) {
                int ul = t->line;
                int uc = t->column;
                advance(p);
                ActivarModuloNode *an = parse_activar_modulo_directive(p, ul, uc);
                if (an)
                    node_vec_push(&globals, (ASTNode *)an);
                else if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    parser_synchronize(p);
                } else
                    break;
                continue;
            }
        }

        ASTNode *s = parse_statement(p);
        if (!s) {
            if (p->last_error) {
                parser_accumulate_error(p, p->last_error);
                free(p->last_error);
                p->last_error = NULL;
                parser_synchronize(p);
                continue;
            }
            {
                const Token *nx = peek(p, 0);
                if (nx && nx->type == TOK_KEYWORD && nx->value.str &&
                    strcmp(nx->value.str, "fin_registro") == 0) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, nx->line, nx->column,
                            "Archivo %s, linea %d, columna %d: `fin_registro` sin un `registro` que lo abra. "
                            "Cada registro debe empezar con `registro` y el nombre del tipo antes de los campos.",
                            p->source_path, nx->line, nx->column);
                    else
                        set_error_at(p, nx->line, nx->column,
                            "linea %d, columna %d: `fin_registro` sin un `registro` que lo abra. "
                            "Empiece con `registro <nombre>` antes de los campos.",
                            nx->line, nx->column);
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    advance(p);
                    continue;
                }
            }
            break;  /* No se pudo parsear, salir para evitar bucle infinito */
        }
        if (s->line == 0) {
            s->line = stmt_line;
            s->col = stmt_col;
        }
        if (s->type == NODE_VAR_DECL) {
            node_vec_push(&globals, s);
        } else {
            int fr_line = 0;
            if (s->type == NODE_IDENTIFIER && (fr_line = find_orphan_fin_registro_line(p, p->pos)) > 0) {
                IdentifierNode *id = (IdentifierNode *)s;
                const char *nm = (id->name && id->name[0]) ? id->name : "?";
                if (p->source_path && p->source_path[0])
                    set_error_at(p, s->line, s->col,
                        "Archivo %s, linea %d, columna %d: parece un registro sin la palabra `registro`. "
                        "Hay `fin_registro` en la linea %d pero falta la apertura `registro %s` (o el nombre que corresponda) antes de los campos.",
                        p->source_path, s->line, s->col, fr_line, nm);
                else
                    set_error_at(p, s->line, s->col,
                        "linea %d, columna %d: bloque de campos con `fin_registro` en la linea %d sin `registro` previo. Use `registro %s` antes del primer campo.",
                        s->line, s->col, fr_line, nm);
                parser_accumulate_error(p, p->last_error);
                free(p->last_error);
                p->last_error = NULL;
                ast_free(s);
                advance_past_fin_registro_from(p, p->pos);
                continue;
            }
            if (p->source_path && p->source_path[0])
                set_error_at(p, s->line, s->col, "Archivo %s, linea %d, columna %d: Codigo ejecutable fuera del bloque principal o de una funcion. Las sentencias deben ir dentro de `principal`.", p->source_path, s->line, s->col);
            else
                set_error_at(p, s->line, s->col, "linea %d, columna %d: Codigo ejecutable fuera del bloque principal o de una funcion. Las sentencias deben ir dentro de `principal`.", s->line, s->col);
            parser_accumulate_error(p, p->last_error);
            free(p->last_error);
            p->last_error = NULL;
            ast_free(s);
            parser_synchronize(p);
            continue;
        }
        match(p, TOK_OPERATOR, ".");
    }

    /* `fin_principal` al nivel superior sin un `principal` previo queda sin consumir; antes se ignoraba. */
    if (p->pos < token_vec_size(p->tokens)) {
        const Token *rem = peek(p, 0);
        if (rem && rem->type == TOK_KEYWORD && rem->value.str &&
            strcmp(rem->value.str, "fin_principal") == 0) {
            if (p->source_path && p->source_path[0])
                set_error_at(p, rem->line, rem->column,
                          "Archivo %s, linea %d, columna %d: `fin_principal` sin bloque `principal` que lo abra (falta `principal` antes).",
                          p->source_path, rem->line, rem->column);
            else
                set_error_at(p, rem->line, rem->column,
                          "linea %d, columna %d: `fin_principal` sin bloque `principal` que lo abra (falta `principal` antes).",
                          rem->line, rem->column);
            parser_accumulate_error(p, p->last_error);
            free(p->last_error);
            p->last_error = NULL;
        }
        rem = peek(p, 0);
        if (rem && rem->type == TOK_KEYWORD && rem->value.str &&
            strcmp(rem->value.str, "fin_registro") == 0) {
            if (p->source_path && p->source_path[0])
                set_error_at(p, rem->line, rem->column,
                          "Archivo %s, linea %d, columna %d: `fin_registro` sin un `registro` que lo abra (falta `registro <nombre>` antes de los campos).",
                          p->source_path, rem->line, rem->column);
            else
                set_error_at(p, rem->line, rem->column,
                          "linea %d, columna %d: `fin_registro` sin un `registro` que lo abra.",
                          rem->line, rem->column);
            parser_accumulate_error(p, p->last_error);
            free(p->last_error);
            p->last_error = NULL;
        }
    }

    if (p->accumulated_errors) {
        if (p->last_error) free(p->last_error);
        p->last_error = p->accumulated_errors;
        p->accumulated_errors = NULL;
    }

    BlockNode *main_block = calloc(1, sizeof(BlockNode));
    main_block->base.type = NODE_BLOCK;
    main_block->statements = main_stmts.arr;
    main_block->n = main_stmts.n;

    ProgramNode *prog = calloc(1, sizeof(ProgramNode));
    prog->base.type = NODE_PROGRAM;
    prog->functions = (ASTNode**)funcs.arr;
    prog->n_funcs = funcs.n;
    prog->main_block = (ASTNode*)main_block;
    prog->globals = (ASTNode**)globals.arr;
    prog->n_globals = globals.n;
    return (ASTNode*)prog;
}

ASTNode *parser_parse_expression_from_string(const char *str, char **err_out) {
    if (!str) return NULL;
    Lexer lex;
    lexer_init(&lex, str);
    TokenVec tvec;
    token_vec_init(&tvec);
    Token tok;
    while (lexer_next(&lex, &tok) == 0) {
        token_vec_push(&tvec, &tok);
        token_free_value(&tok);
        if (tok.type == TOK_EOF) break;
    }
    if (lex.last_error) {
        if (err_out) *err_out = lex.last_error ? strdup(lex.last_error) : NULL;
        lexer_free(&lex);
        return NULL;
    }
    lexer_free(&lex);
    Parser par;
    parser_init(&par, &tvec, NULL, str);
    ASTNode *node = parse_expression(&par);
    if (par.last_error && err_out)
        *err_out = par.last_error ? strdup(par.last_error) : NULL;
    parser_free(&par);
    token_vec_free(&tvec);
    return node;
}
