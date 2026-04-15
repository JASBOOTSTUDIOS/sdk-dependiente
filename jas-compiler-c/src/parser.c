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

static char *strdup_safe(const char *s) {
    return s ? strdup(s) : NULL;
}

/* value.str solo es valido cuando el tipo de token almacena puntero en la union;
 * para TOK_NUMBER/TOK_EOF/TOK_NEWLINE el lexer usa value.i/value.f y leer .str es UB. */
static const char *token_union_str(const Token *t) {
    if (!t) return NULL;
    switch (t->type) {
    case TOK_KEYWORD:
    case TOK_IDENTIFIER:
    case TOK_OPERATOR:
    case TOK_STRING:
    case TOK_CONCEPT:
        return t->value.str;
    default:
        return NULL;
    }
}

/* por compatibilidad historica (p. ej. "a" en formulas) o constructores vec/mat. Las llamadas sistema
   se escriben como identificador-primario en parse_primary. */
static int keyword_ok_as_user_identifier(const char *s) {
    if (!s) return 0;
    size_t L = strlen(s);
    return (strcmp(s, "vec2") == 0 || strcmp(s, "vec3") == 0 ||
            strcmp(s, "vec4") == 0 || strcmp(s, "mat4") == 0 || strcmp(s, "mat3") == 0 ||
            strcmp(s, "entrada") == 0 || strcmp(s, "texto") == 0 ||
            strcmp(s, "caracter") == 0 || strcmp(s, "bool") == 0 ||
            strcmp(s, "lista") == 0 || strcmp(s, "mapa") == 0 ||
            is_sistema_llamada(s, L));
}

/* Variable, parametro, constante, campo: no usar KEYWORDS salvo excepciones anteriores. */
static int validate_user_defined_name_tok(Parser *p, const Token *tok) {
    if (!tok) return 1;
    if (tok->type == TOK_NULL) {
        if (p->source_path && p->source_path[0])
            set_error_at(p, tok->line, tok->column,
                      "Archivo %s, linea %d, columna %d: 'nulo' es una palabra reservada del lenguaje: aqui se esperaba un nombre elegido por usted.",
                      p->source_path, tok->line, tok->column);
        else
            set_error_at(p, tok->line, tok->column,
                      "linea %d, columna %d: 'nulo' es una palabra reservada del lenguaje: aqui se esperaba un nombre elegido por usted.",
                      tok->line, tok->column);
        return 0;
    }
    if (!tok->value.str) return 1;
    const char *s = tok->value.str;
    if (tok->type == TOK_KEYWORD && !keyword_ok_as_user_identifier(s)) {
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
    const char *s = token_union_str(t);
    if (!s) return 0;
    return (strcmp(s, "entero") == 0 || strcmp(s, "texto") == 0 ||
            strcmp(s, "flotante") == 0 || strcmp(s, "caracter") == 0 ||
            strcmp(s, "bool") == 0 || strcmp(s, "lista") == 0 ||
            strcmp(s, "mapa") == 0 || strcmp(s, "u32") == 0 ||
            strcmp(s, "u64") == 0 || strcmp(s, "u8") == 0 ||
            strcmp(s, "byte") == 0 || strcmp(s, "vec2") == 0 ||
            strcmp(s, "vec3") == 0 || strcmp(s, "vec4") == 0 ||
            strcmp(s, "mat4") == 0 || strcmp(s, "mat3") == 0 ||
            strcmp(s, "funcion") == 0 || strcmp(s, "bytes") == 0 ||
            strcmp(s, "tarea") == 0 ||
            strcmp(s, "socket") == 0 || strcmp(s, "tls") == 0 ||
            strcmp(s, "http_solicitud") == 0 || strcmp(s, "http_respuesta") == 0 ||
            strcmp(s, "http_servidor") == 0 ||
            strcmp(s, "objeto") == 0);
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
    static const char *ok[] = {"entero", "flotante", "texto", "bool", "byte", "bytes", "mapa", "lista", NULL};
    int good = 0;
    for (int i = 0; ok[i]; i++)
        if (strcmp(inner->value.str, ok[i]) == 0) good = 1;
    if (!good) {
        set_error_here(p, inner, "lista<T>: T debe ser entero, flotante, texto, bool, byte o bytes.");
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

/* Tras consumir la palabra `tarea`, si sigue `<T>` lo parsea y devuelve el T en *out_inner (el caller hace free).
 * Si no hay `<`, *out_inner queda NULL. Devuelve 0 si hubo error de analisis. */
static int parse_optional_tarea_inner_type_after_tarea_keyword(Parser *p, char **out_inner) {
    *out_inner = NULL;
    const Token *nx = peek(p, 0);
    if (!nx || nx->type != TOK_OPERATOR || !nx->value.str || strcmp(nx->value.str, "<") != 0)
        return 1;
    if (!match(p, TOK_OPERATOR, "<"))
        return 0;
    const Token *inner = peek(p, 0);
    if (!inner || inner->type != TOK_KEYWORD || !inner->value.str) {
        set_error_here(p, inner ? inner : peek(p, 0),
            "En `tarea<T>` se esperaba un tipo T despues de '<' (por ejemplo texto o entero).");
        return 0;
    }
    char *s = strdup_safe(inner->value.str);
    if (!s) return 0;
    advance(p);
    if (!expect(p, TOK_OPERATOR, ">", "Se esperaba '>' para cerrar `tarea<T>`.")) {
        free(s);
        return 0;
    }
    *out_inner = s;
    return 1;
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

static ASTNode *make_export_directive(char **names, size_t n_names, int line, int col) {
    ExportDirectiveNode *n = calloc(1, sizeof(ExportDirectiveNode));
    if (!n) return NULL;
    n->base.type = NODE_EXPORT_DIRECTIVE;
    n->base.line = line;
    n->base.col = col;
    n->names = names;
    n->n_names = n_names;
    return (ASTNode *)n;
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
static char *parse_full_type_name(Parser *p) {
    const Token *t = peek(p, 0);
    if (!t) return NULL;
    
    char *name = NULL;
    if (is_decl_type_token(t)) {
        name = strdup(t->value.str);
        advance(p);
    } else if (t->type == TOK_IDENTIFIER || t->type == TOK_KEYWORD) {
        name = strdup(t->value.str);
        advance(p);
        while (peek(p, 0) && peek(p, 0)->type == TOK_OPERATOR && peek(p, 0)->value.str && strcmp(peek(p, 0)->value.str, ".") == 0) {
            const Token *dot_t = peek(p, 0);
            const Token *next = peek(p, 1);
            if (next && (next->type == TOK_IDENTIFIER || next->type == TOK_KEYWORD)) {
                advance(p); /* consume '.' */
                char *new_name = malloc(strlen(name) + strlen(next->value.str) + 2);
                sprintf(new_name, "%s.%s", name, next->value.str);
                free(name);
                name = new_name;
                advance(p); /* consume identifier */
            } else {
                break;
            }
        }
    }
    return name;
}

static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p, const char **end_kw, size_t n_end);

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

typedef struct {
    char *name;
    ASTNode *route_expr;
    ASTNode *title_expr;
    ASTNode *subtitle_expr;
    ASTNode *body_expr;
    int line;
    int col;
} EstructaWindowDef;

typedef struct {
    EstructaWindowDef *arr;
    size_t n;
    size_t cap;
} EstructaWindowVec;

static int node_vec_push(NodeVec *v, ASTNode *n);
static int token_is_word(const Token *t, const char *word);
static int match_word(Parser *p, const char *word);
static int expect_word(Parser *p, const char *word, const char *msg);
static ASTNode *make_call_named(const char *name, ASTNode **args, size_t n_args, int line, int col);
static ASTNode *make_return_stmt(ASTNode *expr, int line, int col);
static ASTNode *make_print_stmt(ASTNode *expr, int line, int col);
static ASTNode *make_block_node(ASTNode **stmts, size_t n, int line, int col);
static ASTNode *make_if_stmt(ASTNode *cond, ASTNode *body, ASTNode *else_body, int line, int col);
static ASTNode *make_var_decl_simple(const char *type_name, const char *name, ASTNode *value, int line, int col);
static ASTNode *make_function_node_simple(const char *name, const char *return_type, ASTNode **params, size_t n_params, ASTNode *body, int line, int col);
static ASTNode *make_concat_expr(ASTNode *left, ASTNode *right, int line, int col);
static const char *literal_text_value(ASTNode *node);
static ASTNode *join_markup_exprs(ASTNode **exprs, size_t n, int line, int col);
static ASTNode *make_route_match_expr(ASTNode *route_expr, int line, int col);
static ASTNode *clone_expr_basic(const ASTNode *node);
static char *make_named_string(const char *a, const char *b, const char *c);
static int ensure_estructura_runtime_imports(Parser *p, NodeVec *globals, int *imports_added, int line, int col);
static int estructura_window_vec_push(EstructaWindowVec *v, EstructaWindowDef def);
static void estructura_window_vec_free(EstructaWindowVec *v);

static char *parse_estructura_name(Parser *p, const char *contexto) {
    const Token *t = peek(p, 0);
    if (!t || (t->type != TOK_IDENTIFIER && t->type != TOK_KEYWORD)) {
        set_error_here(p, t, contexto);
        return NULL;
    }
    if (!validate_user_defined_name_tok(p, t)) return NULL;
    advance(p);
    return strdup_safe(t->value.str);
}

static ASTNode *parse_estructura_ui_elements(Parser *p);
static ASTNode *parse_estructura_component_v2(Parser *p);
static ASTNode *parse_estructura_view_v2(Parser *p);
static ASTNode *parse_estructura_theme_v2(Parser *p);
static int parse_estructura_app_v2(Parser *p, NodeVec *funcs, NodeVec *globals, NodeVec *main_stmts, int *imports_added);

static ASTNode *parse_estructura_component_invocation(Parser *p) {
    const Token *name_tok = peek(p, 0);
    if (!name_tok || !name_tok->value.str) return NULL;
    char *name = strdup_safe(name_tok->value.str);
    NodeVec args = {0};
    advance(p);
    if (!expect(p, TOK_OPERATOR, "(", "Se esperaba `(` al invocar un componente de Estructa.")) {
        free(name);
        return NULL;
    }
    while (!match(p, TOK_OPERATOR, ")")) {
        ASTNode *arg = parse_expression(p);
        if (!arg) {
            free(name);
            for (size_t i = 0; i < args.n; i++) ast_free(args.arr[i]);
            free(args.arr);
            return NULL;
        }
        node_vec_push(&args, arg);
        if (match(p, TOK_OPERATOR, ")")) break;
        if (!expect(p, TOK_OPERATOR, ",", "Entre argumentos del componente se esperaba `,` o `)`.")) {
            free(name);
            for (size_t i = 0; i < args.n; i++) ast_free(args.arr[i]);
            free(args.arr);
            return NULL;
        }
    }
    ASTNode *call = make_call_named(name, args.arr, args.n, name_tok->line, name_tok->column);
    free(name);
    return call;
}

static ASTNode *parse_estructura_button(Parser *p) {
    const Token *btn_tok = peek(p, 0);
    advance(p);
    ASTNode *label_expr = parse_expression(p);
    if (!label_expr) return NULL;
    if (!expect(p, TOK_OPERATOR, "{", "Tras `boton <etiqueta>` se esperaba un bloque `{ ... }`.")) {
        ast_free(label_expr);
        return NULL;
    }
    if (!expect_word(p, "al_presionar", "Dentro de `boton { ... }` se esperaba `al_presionar { ... }`.")) {
        ast_free(label_expr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "{", "Tras `al_presionar` se esperaba `{`.")) {
        ast_free(label_expr);
        return NULL;
    }

    ASTNode *result = NULL;
    if (match_word(p, "mostrar_alerta")) {
        ASTNode *message_expr = parse_expression(p);
        if (!message_expr) {
            ast_free(label_expr);
            return NULL;
        }
        ASTNode **args = calloc(2, sizeof(ASTNode *));
        args[0] = label_expr;
        args[1] = message_expr;
        result = make_call_named("estructura_boton_alerta", args, 2, btn_tok->line, btn_tok->column);
    } else if (match_word(p, "navegar")) {
        ASTNode *route_expr = parse_expression(p);
        if (!route_expr) {
            ast_free(label_expr);
            return NULL;
        }
        ASTNode **args = calloc(2, sizeof(ASTNode *));
        args[0] = route_expr;
        args[1] = label_expr;
        result = make_call_named("estructura_boton_ruta", args, 2, btn_tok->line, btn_tok->column);
    } else {
        set_error_here(p, peek(p, 0),
            "En `al_presionar { ... }` la V1 de Estructa soporta `mostrar_alerta <expr>` o `navegar <expr>`.");
        ast_free(label_expr);
        return NULL;
    }

    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `al_presionar { ... }`.")) {
        ast_free(result);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `boton { ... }`.")) {
        ast_free(result);
        return NULL;
    }
    return result;
}

static ASTNode *parse_estructura_card(Parser *p) {
    const Token *card_tok = peek(p, 0);
    advance(p);
    if (!expect(p, TOK_OPERATOR, "{", "Tras `tarjeta` se esperaba `{`.")) return NULL;

    ASTNode *title_expr = make_literal_str("");
    ASTNode *content_expr = make_literal_str("");
    ASTNode *variant_expr = make_literal_str("");
    int content_seen = 0;

    while (1) {
        const Token *t = peek(p, 0);
        if (!t) break;
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        if (token_is_word(t, "titulo")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `titulo` se esperaba `:`.")) goto card_fail;
            ast_free(title_expr);
            title_expr = parse_expression(p);
            if (!title_expr) goto card_fail;
            continue;
        }
        if (token_is_word(t, "contenido")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, "{", "Tras `contenido` se esperaba `{`.")) goto card_fail;
            ast_free(content_expr);
            content_expr = parse_estructura_ui_elements(p);
            if (!content_expr) goto card_fail;
            if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `contenido { ... }`.")) goto card_fail;
            content_seen = 1;
            continue;
        }
        if (token_is_word(t, "variante")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `variante` se esperaba `:`.")) goto card_fail;
            ast_free(variant_expr);
            variant_expr = parse_expression(p);
            if (!variant_expr) goto card_fail;
            continue;
        }
        if (!content_seen) {
            content_expr = parse_estructura_ui_elements(p);
            if (!content_expr) goto card_fail;
            content_seen = 1;
            continue;
        }
        set_error_here(p, t, "Dentro de `tarjeta { ... }` solo se permite un bloque de contenido.");
        goto card_fail;
    }

    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `tarjeta { ... }`.")) goto card_fail;
    if (!content_seen) {
        ast_free(content_expr);
        content_expr = make_literal_str("");
    }
    if (literal_text_value(variant_expr) && literal_text_value(variant_expr)[0] == '\0') {
        ASTNode **args = calloc(2, sizeof(ASTNode *));
        args[0] = title_expr;
        args[1] = content_expr;
        ast_free(variant_expr);
        return make_call_named("estructura_tarjeta", args, 2, card_tok->line, card_tok->column);
    }
    ASTNode **args = calloc(3, sizeof(ASTNode *));
    args[0] = title_expr;
    args[1] = variant_expr;
    args[2] = content_expr;
    return make_call_named("estructura_tarjeta_visual", args, 3, card_tok->line, card_tok->column);

card_fail:
    ast_free(title_expr);
    ast_free(content_expr);
    ast_free(variant_expr);
    return NULL;
}

static ASTNode *parse_estructura_layout_block(Parser *p, const char *helper_name) {
    const Token *tok = peek(p, 0);
    advance(p);
    if (!expect(p, TOK_OPERATOR, "{", "Tras este componente de Estructa se esperaba `{`.")) return NULL;
    ASTNode *spacing_expr = make_literal_str("");
    ASTNode *align_expr = make_literal_str("");
    ASTNode *children = NULL;
    while (1) {
        const Token *t = peek(p, 0);
        if (!t) break;
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        if (token_is_word(t, "espacio")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `espacio` se esperaba `:`.")) goto layout_fail;
            ast_free(spacing_expr);
            spacing_expr = parse_expression(p);
            if (!spacing_expr) goto layout_fail;
            continue;
        }
        if (token_is_word(t, "alineacion")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `alineacion` se esperaba `:`.")) goto layout_fail;
            ast_free(align_expr);
            align_expr = parse_expression(p);
            if (!align_expr) goto layout_fail;
            continue;
        }
        children = parse_estructura_ui_elements(p);
        if (!children) goto layout_fail;
        break;
    }
    if (!children) children = make_literal_str("");
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar el bloque del componente.")) {
        goto layout_fail;
    }
    if (helper_name && strcmp(helper_name, "estructura_columna") == 0 &&
        ((literal_text_value(spacing_expr) && literal_text_value(spacing_expr)[0]) ||
         (literal_text_value(align_expr) && literal_text_value(align_expr)[0]))) {
        ASTNode **args = calloc(3, sizeof(ASTNode *));
        args[0] = spacing_expr;
        args[1] = align_expr;
        args[2] = children;
        return make_call_named("estructura_columna_visual", args, 3, tok->line, tok->column);
    }
    if (helper_name && strcmp(helper_name, "estructura_fila") == 0 &&
        ((literal_text_value(spacing_expr) && literal_text_value(spacing_expr)[0]) ||
         (literal_text_value(align_expr) && literal_text_value(align_expr)[0]))) {
        ASTNode **args = calloc(3, sizeof(ASTNode *));
        args[0] = spacing_expr;
        args[1] = align_expr;
        args[2] = children;
        return make_call_named("estructura_fila_visual", args, 3, tok->line, tok->column);
    }
    ast_free(spacing_expr);
    ast_free(align_expr);
    ASTNode **args = calloc(1, sizeof(ASTNode *));
    args[0] = children;
    return make_call_named(helper_name, args, 1, tok->line, tok->column);

layout_fail:
    ast_free(spacing_expr);
    ast_free(align_expr);
    ast_free(children);
    return NULL;
}

static ASTNode *parse_estructura_theme_v2(Parser *p) {
    const Token *theme_tok = peek(p, 0);
    advance(p);
    char *name = parse_estructura_name(p, "Tras `tema` se esperaba el nombre del tema.");
    if (!name) return NULL;
    if (!expect(p, TOK_OPERATOR, "{", "Tras `tema <Nombre>` se esperaba `{`.")) {
        free(name);
        return NULL;
    }
    ASTNode *primary = make_literal_str("#2563eb");
    ASTNode *secondary = make_literal_str("#0f172a");
    ASTNode *background = make_literal_str("#ffffff");
    ASTNode *text = make_literal_str("#111827");
    ASTNode *radius = make_literal_str("12px");
    ASTNode *spacing = make_literal_str("12px");
    while (1) {
        const Token *t = peek(p, 0);
        if (!t) break;
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        if (token_is_word(t, "color_primario")) {
            advance(p); if (!expect(p, TOK_OPERATOR, ":", "Tras `color_primario` se esperaba `:`.")) goto theme_fail;
            ast_free(primary); primary = parse_expression(p); if (!primary) goto theme_fail; continue;
        }
        if (token_is_word(t, "color_secundario")) {
            advance(p); if (!expect(p, TOK_OPERATOR, ":", "Tras `color_secundario` se esperaba `:`.")) goto theme_fail;
            ast_free(secondary); secondary = parse_expression(p); if (!secondary) goto theme_fail; continue;
        }
        if (token_is_word(t, "color_fondo")) {
            advance(p); if (!expect(p, TOK_OPERATOR, ":", "Tras `color_fondo` se esperaba `:`.")) goto theme_fail;
            ast_free(background); background = parse_expression(p); if (!background) goto theme_fail; continue;
        }
        if (token_is_word(t, "color_texto")) {
            advance(p); if (!expect(p, TOK_OPERATOR, ":", "Tras `color_texto` se esperaba `:`.")) goto theme_fail;
            ast_free(text); text = parse_expression(p); if (!text) goto theme_fail; continue;
        }
        if (token_is_word(t, "radio_base")) {
            advance(p); if (!expect(p, TOK_OPERATOR, ":", "Tras `radio_base` se esperaba `:`.")) goto theme_fail;
            ast_free(radius); radius = parse_expression(p); if (!radius) goto theme_fail; continue;
        }
        if (token_is_word(t, "espacio_base")) {
            advance(p); if (!expect(p, TOK_OPERATOR, ":", "Tras `espacio_base` se esperaba `:`.")) goto theme_fail;
            ast_free(spacing); spacing = parse_expression(p); if (!spacing) goto theme_fail; continue;
        }
        set_error_here(p, t, "Propiedad de tema no soportada en la V1.");
        goto theme_fail;
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `tema { ... }`.")) goto theme_fail;
    ASTNode **args = calloc(6, sizeof(ASTNode *));
    args[0] = primary; args[1] = secondary; args[2] = background;
    args[3] = text; args[4] = radius; args[5] = spacing;
    ASTNode **stmts = calloc(1, sizeof(ASTNode *));
    stmts[0] = make_return_stmt(make_call_named("estructura_tema", args, 6, theme_tok->line, theme_tok->column), theme_tok->line, theme_tok->column);
    ASTNode *body = make_block_node(stmts, 1, theme_tok->line, theme_tok->column);
    ASTNode *fn = make_function_node_simple(name, "texto", NULL, 0, body, theme_tok->line, theme_tok->column);
    free(name);
    return fn;
theme_fail:
    free(name);
    ast_free(primary); ast_free(secondary); ast_free(background);
    ast_free(text); ast_free(radius); ast_free(spacing);
    return NULL;
}

static ASTNode *parse_estructura_ui_element(Parser *p) {
    const Token *t = peek(p, 0);
    if (!t) return NULL;

    if (token_is_word(t, "titulo")) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        if (!expr) return NULL;
        ASTNode **args = calloc(1, sizeof(ASTNode *));
        args[0] = expr;
        return make_call_named("estructura_titulo", args, 1, t->line, t->column);
    }
    if (token_is_word(t, "subtitulo")) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        if (!expr) return NULL;
        ASTNode **args = calloc(1, sizeof(ASTNode *));
        args[0] = expr;
        return make_call_named("estructura_subtitulo", args, 1, t->line, t->column);
    }
    if (token_is_word(t, "texto")) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        if (!expr) return NULL;
        ASTNode **args = calloc(1, sizeof(ASTNode *));
        args[0] = expr;
        return make_call_named("estructura_texto", args, 1, t->line, t->column);
    }
    if (token_is_word(t, "boton_ruta")) {
        advance(p);
        ASTNode *route_expr = parse_expression(p);
        if (!route_expr) return NULL;
        if (!expect(p, TOK_OPERATOR, ",", "Tras la ruta de `boton_ruta` se esperaba `,`.")) {
            ast_free(route_expr);
            return NULL;
        }
        ASTNode *label_expr = parse_expression(p);
        if (!label_expr) {
            ast_free(route_expr);
            return NULL;
        }
        ASTNode **args = calloc(2, sizeof(ASTNode *));
        args[0] = route_expr;
        args[1] = label_expr;
        return make_call_named("estructura_boton_ruta", args, 2, t->line, t->column);
    }
    if (token_is_word(t, "boton")) return parse_estructura_button(p);
    if (token_is_word(t, "columna")) return parse_estructura_layout_block(p, "estructura_columna");
    if (token_is_word(t, "fila")) return parse_estructura_layout_block(p, "estructura_fila");
    if (token_is_word(t, "tarjeta")) return parse_estructura_card(p);

    if ((t->type == TOK_IDENTIFIER || (t->type == TOK_KEYWORD && keyword_ok_as_user_identifier(t->value.str)))
        && peek(p, 1) && peek(p, 1)->type == TOK_OPERATOR && peek(p, 1)->value.str
        && strcmp(peek(p, 1)->value.str, "(") == 0) {
        return parse_estructura_component_invocation(p);
    }

    ASTNode *expr = parse_expression(p);
    if (!expr) return NULL;
    return expr;
}

static ASTNode *parse_estructura_ui_elements(Parser *p) {
    NodeVec exprs = {0};
    const Token *start = peek(p, 0);
    while (1) {
        const Token *t = peek(p, 0);
        if (!t) break;
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        ASTNode *expr = parse_estructura_ui_element(p);
        if (!expr) {
            for (size_t i = 0; i < exprs.n; i++) ast_free(exprs.arr[i]);
            free(exprs.arr);
            return NULL;
        }
        node_vec_push(&exprs, expr);
    }
    int line = start ? start->line : 0;
    int col = start ? start->column : 0;
    ASTNode *joined = join_markup_exprs(exprs.arr, exprs.n, line, col);
    free(exprs.arr);
    return joined;
}

static int parse_estructura_params(Parser *p, NodeVec *params) {
    if (!match(p, TOK_OPERATOR, "(")) return 1;
    while (!match(p, TOK_OPERATOR, ")")) {
        const Token *maybe_type = peek(p, 0);
        char *type_name = NULL;
        char *list_el = NULL;
        if (maybe_type && maybe_type->type == TOK_KEYWORD && maybe_type->value.str &&
            strcmp(maybe_type->value.str, "lista") == 0) {
            advance(p);
            type_name = strdup_safe("lista");
            list_el = parse_optional_lista_element_type(p);
            if (p->last_error) {
                free(type_name);
                free(list_el);
                return 0;
            }
        } else if (is_decl_type_token(maybe_type)) {
            type_name = strdup_safe(maybe_type->value.str);
            advance(p);
        } else {
            type_name = strdup_safe("texto");
        }

        const Token *name_tok = peek(p, 0);
        if (!name_tok || (name_tok->type != TOK_IDENTIFIER && name_tok->type != TOK_KEYWORD) || !validate_user_defined_name_tok(p, name_tok)) {
            free(type_name);
            free(list_el);
            return 0;
        }
        advance(p);
        VarDeclNode *vd = calloc(1, sizeof(VarDeclNode));
        vd->base.type = NODE_VAR_DECL;
        vd->base.line = name_tok->line;
        vd->base.col = name_tok->column;
        vd->type_name = type_name;
        vd->name = strdup_safe(name_tok->value.str);
        vd->list_element_type = list_el;
        node_vec_push(params, (ASTNode *)vd);
        if (match(p, TOK_OPERATOR, ")")) break;
        if (!expect(p, TOK_OPERATOR, ",", "Entre parametros del componente se esperaba `,` o `)`.")) return 0;
    }
    return 1;
}

static ASTNode *parse_estructura_component(Parser *p) {
    const Token *comp_tok = peek(p, 0);
    advance(p);
    char *name = parse_estructura_name(p, "Tras `Estructa` se esperaba el nombre del componente.");
    if (!name) return NULL;

    NodeVec params = {0};
    if (!parse_estructura_params(p, &params)) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "{", "Tras la firma del componente `Estructa` se esperaba `{`.")) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect_word(p, "contenido", "Dentro del componente se esperaba `contenido { ... }`.")) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "{", "Tras `contenido` se esperaba `{`.")) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    ASTNode *body_expr = parse_estructura_ui_elements(p);
    if (!body_expr) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `contenido { ... }` del componente.")) {
        free(name);
        ast_free(body_expr);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar el componente `Estructa`.")) {
        free(name);
        ast_free(body_expr);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }

    ASTNode **stmts = calloc(1, sizeof(ASTNode *));
    stmts[0] = make_return_stmt(body_expr, comp_tok->line, comp_tok->column);
    ASTNode *body_block = make_block_node(stmts, 1, comp_tok->line, comp_tok->column);
    ASTNode *fn = make_function_node_simple(name, "texto", params.arr, params.n, body_block, comp_tok->line, comp_tok->column);
    free(name);
    return fn;
}

static ASTNode *parse_estructura_component_v2(Parser *p) {
    const Token *comp_tok = peek(p, 0);
    advance(p);
    char *name = parse_estructura_name(p, "Tras `componente` se esperaba el nombre del componente.");
    if (!name) return NULL;
    NodeVec params = {0};
    if (!parse_estructura_params(p, &params)) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "{", "Tras la firma del componente se esperaba `{`.")) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    ASTNode *body_expr = parse_estructura_ui_elements(p);
    if (!body_expr) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar el bloque del componente.")) {
        free(name);
        ast_free(body_expr);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    ASTNode **stmts = calloc(1, sizeof(ASTNode *));
    stmts[0] = make_return_stmt(body_expr, comp_tok->line, comp_tok->column);
    ASTNode *body_block = make_block_node(stmts, 1, comp_tok->line, comp_tok->column);
    ASTNode *fn = make_function_node_simple(name, "texto", params.arr, params.n, body_block, comp_tok->line, comp_tok->column);
    free(name);
    return fn;
}

static ASTNode *parse_estructura_view_v2(Parser *p) {
    const Token *view_tok = peek(p, 0);
    advance(p);
    char *name = parse_estructura_name(p, "Tras `vista` se esperaba el nombre de la vista.");
    if (!name) return NULL;
    NodeVec params = {0};
    if (!parse_estructura_params(p, &params)) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "{", "Tras la firma de la vista se esperaba `{`.")) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    ASTNode *body_expr = parse_estructura_ui_elements(p);
    if (!body_expr) {
        free(name);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar el bloque de la vista.")) {
        free(name);
        ast_free(body_expr);
        for (size_t i = 0; i < params.n; i++) ast_free(params.arr[i]);
        free(params.arr);
        return NULL;
    }
    ASTNode **stmts = calloc(1, sizeof(ASTNode *));
    stmts[0] = make_return_stmt(body_expr, view_tok->line, view_tok->column);
    ASTNode *body_block = make_block_node(stmts, 1, view_tok->line, view_tok->column);
    ASTNode *fn = make_function_node_simple(name, "texto", params.arr, params.n, body_block, view_tok->line, view_tok->column);
    free(name);
    return fn;
}

static int parse_estructura_app_v2(Parser *p, NodeVec *funcs, NodeVec *globals, NodeVec *main_stmts, int *imports_added) {
    const Token *app_tok = peek(p, 0);
    advance(p);
    char *app_name = parse_estructura_name(p, "Tras `app` se esperaba el nombre de la aplicacion.");
    if (!app_name) return 0;
    if (!expect(p, TOK_OPERATOR, "{", "Tras `app <Nombre>` se esperaba `{`.")) {
        free(app_name);
        return 0;
    }
    if (!ensure_estructura_runtime_imports(p, globals, imports_added, app_tok->line, app_tok->column)) {
        free(app_name);
        return 0;
    }

    ASTNode *route_exprs[4] = {0};
    char *view_names[4] = {0};
    char *theme_name = NULL;
    size_t route_count = 0;

    while (1) {
        const Token *t = peek(p, 0);
        if (!t) {
            free(app_name);
            return 0;
        }
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        if (token_is_word(t, "usar")) {
            advance(p);
            if (!expect_word(p, "tema", "Tras `usar` se esperaba `tema`.")) {
                free(app_name);
                return 0;
            }
            free(theme_name);
            theme_name = parse_estructura_name(p, "Tras `usar tema` se esperaba el nombre del tema.");
            if (!theme_name) {
                free(app_name);
                return 0;
            }
            continue;
        }
        if (token_is_word(t, "rutas")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, "{", "Tras `rutas` se esperaba `{`.")) {
                free(app_name);
                return 0;
            }
            while (1) {
                const Token *rt = peek(p, 0);
                if (!rt) {
                    free(app_name);
                    return 0;
                }
                if (rt->type == TOK_OPERATOR && rt->value.str && strcmp(rt->value.str, "}") == 0) break;
                if (route_count >= 4) {
                    set_error_here(p, rt, "La V1 de `app { rutas { ... } }` soporta hasta 4 rutas.");
                    free(app_name);
                    return 0;
                }
                ASTNode *route_expr = parse_expression(p);
                if (!route_expr) {
                    free(app_name);
                    return 0;
                }
                if (!expect(p, TOK_OPERATOR, "=>", "Entre ruta y vista se esperaba `=>`.")) {
                    ast_free(route_expr);
                    free(app_name);
                    return 0;
                }
                char *view_name = parse_estructura_name(p, "Tras `=>` se esperaba el nombre de la vista.");
                if (!view_name) {
                    ast_free(route_expr);
                    free(app_name);
                    return 0;
                }
                route_exprs[route_count] = route_expr;
                view_names[route_count] = view_name;
                route_count++;
            }
            if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `rutas { ... }`.")) {
                free(app_name);
                return 0;
            }
            continue;
        }
        set_error_here(p, t, "Dentro de `app { ... }` se admite `usar tema ...` y `rutas { ... }`.");
        free(app_name);
        return 0;
    }

    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `app { ... }`.")) {
        free(app_name);
        return 0;
    }
    if (route_count == 0) {
        set_error_here(p, app_tok, "La aplicacion requiere al menos una ruta.");
        free(app_name);
        return 0;
    }

    ASTNode *nav_exprs[4] = {0};
    for (size_t i = 0; i < route_count; i++) {
        ASTNode **nav_args = calloc(2, sizeof(ASTNode *));
        nav_args[0] = clone_expr_basic(route_exprs[i]);
        nav_args[1] = make_literal_str(view_names[i]);
        nav_exprs[i] = make_call_named("estructura_enlace_ruta", nav_args, 2, app_tok->line, app_tok->column);
    }
    ASTNode *nav_markup = join_markup_exprs(nav_exprs, route_count, app_tok->line, app_tok->column);

    char *handler_name = make_named_string("estructura_app_", app_name, "");
    ASTNode **handler_params = calloc(1, sizeof(ASTNode *));
    handler_params[0] = make_var_decl_simple("texto", "solicitud", NULL, app_tok->line, app_tok->column);
    ASTNode **handler_stmts = calloc(route_count + 2, sizeof(ASTNode *));
    size_t handler_n = 0;
    handler_stmts[handler_n++] = make_var_decl_simple("texto", "navegacion", nav_markup, app_tok->line, app_tok->column);
    for (size_t i = 0; i < route_count; i++) {
        ASTNode *cond = make_route_match_expr(route_exprs[i], app_tok->line, app_tok->column);
        ASTNode **view_args = calloc(1, sizeof(ASTNode *));
        view_args[0] = make_identifier("solicitud", app_tok->line, app_tok->column);
        ASTNode *view_call = make_call_named(view_names[i], view_args, 1, app_tok->line, app_tok->column);
        ASTNode **resp_args = calloc(3, sizeof(ASTNode *));
        resp_args[0] = make_literal_str(app_name);
        resp_args[1] = make_identifier("navegacion", app_tok->line, app_tok->column);
        resp_args[2] = view_call;
        ASTNode **if_stmts = calloc(1, sizeof(ASTNode *));
        if (theme_name && theme_name[0]) {
            ASTNode **theme_args = calloc(4, sizeof(ASTNode *));
            theme_args[0] = resp_args[0];
            theme_args[1] = resp_args[1];
            theme_args[2] = resp_args[2];
            theme_args[3] = make_call_named(theme_name, NULL, 0, app_tok->line, app_tok->column);
            free(resp_args);
            if_stmts[0] = make_return_stmt(make_call_named("estructura_responder_tema", theme_args, 4, app_tok->line, app_tok->column), app_tok->line, app_tok->column);
        } else {
            if_stmts[0] = make_return_stmt(make_call_named("estructura_responder", resp_args, 3, app_tok->line, app_tok->column), app_tok->line, app_tok->column);
        }
        ASTNode *if_body = make_block_node(if_stmts, 1, app_tok->line, app_tok->column);
        handler_stmts[handler_n++] = make_if_stmt(cond, if_body, NULL, app_tok->line, app_tok->column);
    }
    if (theme_name && theme_name[0]) {
        ASTNode **not_found_args = calloc(3, sizeof(ASTNode *));
        not_found_args[0] = make_literal_str(app_name);
        not_found_args[1] = make_identifier("navegacion", app_tok->line, app_tok->column);
        not_found_args[2] = make_call_named(theme_name, NULL, 0, app_tok->line, app_tok->column);
        handler_stmts[handler_n++] = make_return_stmt(make_call_named("estructura_no_encontrado_tema", not_found_args, 3, app_tok->line, app_tok->column), app_tok->line, app_tok->column);
    } else {
        ASTNode **not_found_args = calloc(2, sizeof(ASTNode *));
        not_found_args[0] = make_literal_str(app_name);
        not_found_args[1] = make_identifier("navegacion", app_tok->line, app_tok->column);
        handler_stmts[handler_n++] = make_return_stmt(make_call_named("estructura_no_encontrado", not_found_args, 2, app_tok->line, app_tok->column), app_tok->line, app_tok->column);
    }
    ASTNode *handler_body = make_block_node(handler_stmts, handler_n, app_tok->line, app_tok->column);
    node_vec_push(funcs, make_function_node_simple(handler_name, "texto", handler_params, 1, handler_body, app_tok->line, app_tok->column));

    node_vec_push(main_stmts, make_print_stmt(make_literal_str("Estructa"), app_tok->line, app_tok->column));
    node_vec_push(main_stmts, make_print_stmt(make_literal_str("app declarativa activa en http://127.0.0.1:18220"), app_tok->line, app_tok->column));
    ASTNode **srv_args = calloc(4, sizeof(ASTNode *));
    srv_args[0] = make_literal_str("127.0.0.1");
    srv_args[1] = make_literal_int(18220);
    srv_args[2] = make_literal_int(100);
    srv_args[3] = make_identifier(handler_name, app_tok->line, app_tok->column);
    node_vec_push(main_stmts, make_print_stmt(make_call_named("estructura_http_bucle", srv_args, 4, app_tok->line, app_tok->column), app_tok->line, app_tok->column));

    free(handler_name);
    free(theme_name);
    free(app_name);
    return 1;
}

static ASTNode *make_estructura_window_function(const char *fn_name, EstructaWindowDef *wd) {
    ASTNode **args = calloc(4, sizeof(ASTNode *));
    args[0] = wd->route_expr;
    args[1] = wd->title_expr;
    args[2] = wd->subtitle_expr;
    args[3] = wd->body_expr;
    wd->title_expr = NULL;
    wd->subtitle_expr = NULL;
    wd->route_expr = NULL;
    wd->body_expr = NULL;
    ASTNode *page_call = make_call_named("estructura_vista_ruta", args, 4, wd->line, wd->col);
    ASTNode **stmts = calloc(1, sizeof(ASTNode *));
    stmts[0] = make_return_stmt(page_call, wd->line, wd->col);
    ASTNode *body = make_block_node(stmts, 1, wd->line, wd->col);
    return make_function_node_simple(fn_name, "texto", NULL, 0, body, wd->line, wd->col);
}

static char *make_named_string(const char *a, const char *b, const char *c) {
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    size_t lc = c ? strlen(c) : 0;
    char *out = calloc(la + lb + lc + 1, 1);
    if (!out) return NULL;
    if (a) memcpy(out, a, la);
    if (b) memcpy(out + la, b, lb);
    if (c) memcpy(out + la + lb, c, lc);
    return out;
}

static ASTNode *make_route_match_expr(ASTNode *route_expr, int line, int col) {
    ASTNode **args = calloc(2, sizeof(ASTNode *));
    args[0] = make_identifier("solicitud", line, col);
    args[1] = route_expr;
    return make_call_named("estructura_es_get", args, 2, line, col);
}

static ASTNode *make_window_call_expr(const char *fn_name, int line, int col) {
    return make_call_named(fn_name, NULL, 0, line, col);
}

static ASTNode *make_route_nav_link_expr(ASTNode *route_expr, ASTNode *title_expr, int line, int col) {
    ASTNode **args = calloc(2, sizeof(ASTNode *));
    args[0] = route_expr;
    args[1] = title_expr;
    return make_call_named("estructura_enlace_ruta", args, 2, line, col);
}

static int parse_estructura_window(Parser *p, const char *app_name, size_t index, EstructaWindowDef *out) {
    const Token *win_tok = peek(p, 0);
    advance(p);
    memset(out, 0, sizeof(*out));
    out->name = parse_estructura_name(p, "Tras `ventana` se esperaba el nombre de la ventana.");
    if (!out->name) return 0;
    out->line = win_tok->line;
    out->col = win_tok->column;
    out->title_expr = make_literal_str(out->name);
    out->subtitle_expr = make_literal_str(app_name);
    out->route_expr = (index == 0) ? make_literal_str("/") : make_literal_str(make_named_string("/", out->name, ""));
    if (!expect(p, TOK_OPERATOR, "{", "Tras `ventana <Nombre>` se esperaba `{`.")) return 0;

    while (1) {
        const Token *t = peek(p, 0);
        if (!t) return 0;
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        if (token_is_word(t, "titulo")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `titulo` se esperaba `:`.")) return 0;
            ast_free(out->title_expr);
            out->title_expr = parse_expression(p);
            if (!out->title_expr) return 0;
            continue;
        }
        if (token_is_word(t, "subtitulo")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `subtitulo` se esperaba `:`.")) return 0;
            ast_free(out->subtitle_expr);
            out->subtitle_expr = parse_expression(p);
            if (!out->subtitle_expr) return 0;
            continue;
        }
        if (token_is_word(t, "ruta")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, ":", "Tras `ruta` se esperaba `:`.")) return 0;
            ast_free(out->route_expr);
            out->route_expr = parse_expression(p);
            if (!out->route_expr) return 0;
            continue;
        }
        if (token_is_word(t, "contenido")) {
            advance(p);
            if (!expect(p, TOK_OPERATOR, "{", "Tras `contenido` se esperaba `{`.")) return 0;
            ast_free(out->body_expr);
            out->body_expr = parse_estructura_ui_elements(p);
            if (!out->body_expr) return 0;
            if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `contenido { ... }`.")) return 0;
            continue;
        }
        set_error_here(p, t,
            "Dentro de `ventana { ... }` se admite `titulo: ...`, `subtitulo: ...`, `ruta: ...` y `contenido { ... }`.");
        return 0;
    }

    if (!out->body_expr) out->body_expr = make_literal_str("");
    return expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar la `ventana`.");
}

static int parse_estructura_application(Parser *p, NodeVec *funcs, NodeVec *globals, NodeVec *main_stmts, int *imports_added) {
    const Token *app_tok = peek(p, 0);
    advance(p);
    char *app_name = parse_estructura_name(p, "Tras `aplicacion` se esperaba el nombre de la aplicacion.");
    if (!app_name) return 0;
    if (!expect_word(p, "iniciar", "Tras `aplicacion <Nombre>` se esperaba `iniciar { ... }`.")) {
        free(app_name);
        return 0;
    }
    if (!expect(p, TOK_OPERATOR, "{", "Tras `iniciar` se esperaba `{`.")) {
        free(app_name);
        return 0;
    }
    if (!ensure_estructura_runtime_imports(p, globals, imports_added, app_tok->line, app_tok->column)) {
        free(app_name);
        return 0;
    }

    EstructaWindowVec windows = {0};
    while (1) {
        const Token *t = peek(p, 0);
        if (!t) {
            estructura_window_vec_free(&windows);
            free(app_name);
            return 0;
        }
        if (t->type == TOK_OPERATOR && t->value.str && strcmp(t->value.str, "}") == 0) break;
        if (!token_is_word(t, "ventana")) {
            set_error_here(p, t, "Dentro de `iniciar { ... }` solo se permiten bloques `ventana Nombre { ... }`.");
            estructura_window_vec_free(&windows);
            free(app_name);
            return 0;
        }
        EstructaWindowDef wd;
        if (!parse_estructura_window(p, app_name, windows.n, &wd)) {
            estructura_window_vec_free(&windows);
            free(app_name);
            return 0;
        }
        estructura_window_vec_push(&windows, wd);
    }
    if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `iniciar { ... }`.")) {
        estructura_window_vec_free(&windows);
        free(app_name);
        return 0;
    }
    if (windows.n == 0) {
        set_error_here(p, app_tok, "La aplicacion Estructa requiere al menos una `ventana` dentro de `iniciar { ... }`.");
        free(app_name);
        return 0;
    }

    char *handler_name = make_named_string("estructura_app_", app_name, "");
    NodeVec nav_exprs = {0};
    NodeVec view_calls = {0};
    for (size_t i = 0; i < windows.n; i++) {
        ASTNode *nav_link = make_route_nav_link_expr(
            clone_expr_basic(windows.arr[i].route_expr),
            clone_expr_basic(windows.arr[i].title_expr),
            windows.arr[i].line,
            windows.arr[i].col
        );
        node_vec_push(&nav_exprs, nav_link);
        char *screen_fn = make_named_string("estructura_ventana_", windows.arr[i].name, "");
        node_vec_push(&view_calls, make_window_call_expr(screen_fn, windows.arr[i].line, windows.arr[i].col));
        ASTNode *fn = make_estructura_window_function(screen_fn, &windows.arr[i]);
        node_vec_push(funcs, fn);
        free(screen_fn);
    }

    ASTNode *nav_markup = join_markup_exprs(nav_exprs.arr, nav_exprs.n, app_tok->line, app_tok->column);
    ASTNode *views_markup = join_markup_exprs(view_calls.arr, view_calls.n, app_tok->line, app_tok->column);
    ASTNode **shell_args = calloc(3, sizeof(ASTNode *));
    shell_args[0] = make_literal_str(app_name);
    shell_args[1] = nav_markup;
    shell_args[2] = views_markup;
    ASTNode **handler_stmts = calloc(1, sizeof(ASTNode *));
    handler_stmts[0] = make_return_stmt(make_call_named("estructura_app_rutas", shell_args, 3, app_tok->line, app_tok->column), app_tok->line, app_tok->column);
    ASTNode *handler_body = make_block_node(handler_stmts, 1, app_tok->line, app_tok->column);
    node_vec_push(funcs, make_function_node_simple(handler_name, "texto", NULL, 0, handler_body, app_tok->line, app_tok->column));

    node_vec_push(main_stmts, make_print_stmt(make_literal_str("Estructa"), app_tok->line, app_tok->column));
    node_vec_push(main_stmts, make_print_stmt(make_literal_str("app DSL activa en http://127.0.0.1:18220"), app_tok->line, app_tok->column));
    ASTNode **srv_args = calloc(4, sizeof(ASTNode *));
    srv_args[0] = make_literal_str("127.0.0.1");
    srv_args[1] = make_literal_int(18220);
    srv_args[2] = make_literal_int(100);
    srv_args[3] = make_identifier(handler_name, app_tok->line, app_tok->column);
    node_vec_push(main_stmts, make_print_stmt(make_call_named("estructura_http_bucle", srv_args, 4, app_tok->line, app_tok->column), app_tok->line, app_tok->column));

    estructura_window_vec_free(&windows);
    free(handler_name);
    free(app_name);
    return 1;
}

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

static int estructura_window_vec_push(EstructaWindowVec *v, EstructaWindowDef def) {
    if (v->n >= v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 4;
        EstructaWindowDef *p = realloc(v->arr, nc * sizeof(EstructaWindowDef));
        if (!p) return -1;
        v->arr = p;
        v->cap = nc;
    }
    v->arr[v->n++] = def;
    return 0;
}

static void estructura_window_vec_free(EstructaWindowVec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->n; i++) {
        free(v->arr[i].name);
        ast_free(v->arr[i].route_expr);
        ast_free(v->arr[i].title_expr);
        ast_free(v->arr[i].subtitle_expr);
        ast_free(v->arr[i].body_expr);
    }
    free(v->arr);
    v->arr = NULL;
    v->n = 0;
    v->cap = 0;
}

static int token_is_word(const Token *t, const char *word) {
    if (!t || !word) return 0;
    if (t->type != TOK_KEYWORD && t->type != TOK_IDENTIFIER) return 0;
    if (!t->value.str) return 0;
    return strcmp(t->value.str, word) == 0;
}

static int match_word(Parser *p, const char *word) {
    const Token *t = peek(p, 0);
    if (!token_is_word(t, word)) return 0;
    advance(p);
    return 1;
}

static int expect_word(Parser *p, const char *word, const char *msg) {
    if (match_word(p, word)) return 1;
    return expect(p, TOK_IDENTIFIER, word, msg);
}

static ASTNode *make_call_named(const char *name, ASTNode **args, size_t n_args, int line, int col) {
    CallNode *cn = calloc(1, sizeof(CallNode));
    if (!cn) return NULL;
    cn->base.type = NODE_CALL;
    cn->base.line = line;
    cn->base.col = col;
    cn->name = strdup_safe(name);
    cn->args = args;
    cn->n_args = n_args;
    return (ASTNode *)cn;
}

static ASTNode *make_return_stmt(ASTNode *expr, int line, int col) {
    ReturnNode *rn = calloc(1, sizeof(ReturnNode));
    if (!rn) return NULL;
    rn->base.type = NODE_RETURN;
    rn->base.line = line;
    rn->base.col = col;
    rn->expression = expr;
    return (ASTNode *)rn;
}

static ASTNode *make_print_stmt(ASTNode *expr, int line, int col) {
    PrintNode *pn = calloc(1, sizeof(PrintNode));
    if (!pn) return NULL;
    pn->base.type = NODE_PRINT;
    pn->base.line = line;
    pn->base.col = col;
    pn->expression = expr;
    return (ASTNode *)pn;
}

static ASTNode *make_block_node(ASTNode **stmts, size_t n, int line, int col) {
    BlockNode *bn = calloc(1, sizeof(BlockNode));
    if (!bn) return NULL;
    bn->base.type = NODE_BLOCK;
    bn->base.line = line;
    bn->base.col = col;
    bn->statements = stmts;
    bn->n = n;
    return (ASTNode *)bn;
}

static ASTNode *make_if_stmt(ASTNode *cond, ASTNode *body, ASTNode *else_body, int line, int col) {
    IfNode *in = calloc(1, sizeof(IfNode));
    if (!in) return NULL;
    in->base.type = NODE_IF;
    in->base.line = line;
    in->base.col = col;
    in->condition = cond;
    in->body = body;
    in->else_body = else_body;
    return (ASTNode *)in;
}

static ASTNode *make_var_decl_simple(const char *type_name, const char *name, ASTNode *value, int line, int col) {
    VarDeclNode *vd = calloc(1, sizeof(VarDeclNode));
    if (!vd) return NULL;
    vd->base.type = NODE_VAR_DECL;
    vd->base.line = line;
    vd->base.col = col;
    vd->type_name = strdup_safe(type_name);
    vd->name = strdup_safe(name);
    vd->value = value;
    return (ASTNode *)vd;
}

static ASTNode *make_function_node_simple(const char *name, const char *return_type, ASTNode **params, size_t n_params, ASTNode *body, int line, int col) {
    FunctionNode *fn = calloc(1, sizeof(FunctionNode));
    if (!fn) return NULL;
    fn->base.type = NODE_FUNCTION;
    fn->base.line = line;
    fn->base.col = col;
    fn->name = strdup_safe(name);
    fn->return_type = strdup_safe(return_type);
    fn->params = params;
    fn->n_params = n_params;
    fn->body = body;
    return (ASTNode *)fn;
}

static ASTNode *make_import_all_node(const char *module_path, int line, int col) {
    ActivarModuloNode *an = calloc(1, sizeof(ActivarModuloNode));
    if (!an) return NULL;
    an->base.type = NODE_ACTIVAR_MODULO;
    an->base.line = line;
    an->base.col = col;
    an->import_kind = USAR_IMPORT_TODO;
    an->module_path = make_literal_str(module_path);
    return (ASTNode *)an;
}

static ASTNode *make_concat_expr(ASTNode *left, ASTNode *right, int line, int col) {
    ASTNode **args = calloc(2, sizeof(ASTNode *));
    if (!args) return NULL;
    args[0] = left;
    args[1] = right;
    return make_call_named("concatenar", args, 2, line, col);
}

static const char *literal_text_value(ASTNode *node) {
    if (!node || node->type != NODE_LITERAL) return NULL;
    LiteralNode *lit = (LiteralNode *)node;
    if (!lit->type_name || strcmp(lit->type_name, "texto") != 0) return NULL;
    return lit->value.str ? lit->value.str : "";
}

static ASTNode *join_markup_exprs(ASTNode **exprs, size_t n, int line, int col) {
    if (n == 0) return make_literal_str("");
    ASTNode *acc = exprs[0];
    for (size_t i = 1; i < n; i++) {
        acc = make_concat_expr(acc, exprs[i], line, col);
        if (!acc) return NULL;
    }
    return acc;
}

static char *dup_parent_dir_forward(const char *path) {
    if (!path) return NULL;
    char *buf = strdup_safe(path);
    if (!buf) return NULL;
    for (char *p = buf; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    char *slash = strrchr(buf, '/');
    if (slash) *slash = '\0';
    return buf;
}

static int file_exists_simple(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char *path_join_forward(const char *base, const char *suffix) {
    size_t lb = base ? strlen(base) : 0;
    size_t ls = suffix ? strlen(suffix) : 0;
    char *out = calloc(lb + ls + 2, 1);
    if (!out) return NULL;
    if (base) memcpy(out, base, lb);
    if (lb > 0 && out[lb - 1] != '/') out[lb++] = '/';
    if (suffix) memcpy(out + lb, suffix, ls);
    return out;
}

static char *find_repo_root_for_estructura(const char *source_path) {
    char *dir = dup_parent_dir_forward(source_path);
    if (!dir) return NULL;
    while (dir && dir[0]) {
        char *forja = path_join_forward(dir, "stdlib/forja/forja.jasb");
        char *estructura = path_join_forward(dir, "stdlib/Estructa/codigo-jasb/estructura.jasb");
        int ok = file_exists_simple(forja) && file_exists_simple(estructura);
        free(forja);
        free(estructura);
        if (ok) return dir;
        char *slash = strrchr(dir, '/');
        if (!slash) break;
        if (slash <= dir + 2 && dir[1] == ':') break;
        *slash = '\0';
    }
    free(dir);
    return NULL;
}

static int ensure_estructura_runtime_imports(Parser *p, NodeVec *globals, int *imports_added, int line, int col) {
    if (*imports_added) return 1;
    char *root = find_repo_root_for_estructura(p->source_path);
    if (!root) {
        set_error_at(p, line, col,
            "No se pudo ubicar `stdlib/forja/forja.jasb` y `stdlib/Estructa/codigo-jasb/estructura.jasb` desde este archivo para activar automaticamente el runtime de Estructa.");
        return 0;
    }
    char *forja = path_join_forward(root, "stdlib/forja/forja.jasb");
    char *estructura = path_join_forward(root, "stdlib/Estructa/codigo-jasb/estructura.jasb");
    ASTNode *imp_forja = make_import_all_node(forja, line, col);
    ASTNode *imp_estructura = make_import_all_node(estructura, line, col);
    free(root);
    free(forja);
    free(estructura);
    if (!imp_forja || !imp_estructura) {
        ast_free(imp_forja);
        ast_free(imp_estructura);
        return 0;
    }
    node_vec_push(globals, imp_forja);
    node_vec_push(globals, imp_estructura);
    *imports_added = 1;
    return 1;
}

static int is_ident_token_for_lambda(const Token *t) {
    if (!t) return 0;
    if (t->type == TOK_IDENTIFIER)
        return t->value.str != NULL;
    if (t->type == TOK_KEYWORD && t->value.str)
        return keyword_ok_as_user_identifier(t->value.str);
    return 0;
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

/* Tras consumir `json`, parsea { clave: valor ... } (claves identificador o texto). */
static ASTNode *parse_json_object_literal(Parser *p) {
    const Token *brace = peek(p, 0);
    if (!brace || brace->type != TOK_OPERATOR || !brace->value.str || strcmp(brace->value.str, "{") != 0) {
        set_error_here(p, brace ? brace : peek(p, 0), "Tras `json` se esperaba `{`.");
        return NULL;
    }
    advance(p);
    NodeVec keys = {0}, vals = {0};
    while (peek(p, 0)) {
        const Token *cl = peek(p, 0);
        if (cl->type == TOK_OPERATOR && cl->value.str && strcmp(cl->value.str, "}") == 0) break;
        ASTNode *kn = NULL;
        if (cl->type == TOK_STRING) {
            advance(p);
            kn = make_literal_str(cl->value.str);
        } else if (cl->type == TOK_IDENTIFIER) {
            advance(p);
            kn = make_literal_str(cl->value.str ? cl->value.str : "");
        } else if (cl->type == TOK_KEYWORD && cl->value.str) {
            if (!keyword_ok_as_user_identifier(cl->value.str)) {
                char emsg[ERROR_MAX];
                snprintf(emsg, sizeof emsg,
                    "La clave en `json { ... }` no puede ser la palabra reservada `%s`. "
                    "Use un identificador que no sea palabra clave, o la clave JSON entre comillas dobles (p. ej. \"%s\": valor).",
                    cl->value.str, cl->value.str);
                set_error_here(p, cl, emsg);
                goto json_lit_fail;
            }
            advance(p);
            kn = make_literal_str(cl->value.str);
        } else if (cl->type == TOK_CONCEPT) {
            advance(p);
            kn = make_literal_str(cl->value.str ? cl->value.str : "");
        } else {
            set_error_here(p, cl,
                "En `json { ... }` cada clave debe ser un identificador, un concepto entre comillas simples, "
                "o un texto entre comillas dobles (las palabras reservadas del lenguaje no pueden ser clave sin comillas).");
            goto json_lit_fail;
        }
        if (!expect(p, TOK_OPERATOR, ":", "Se esperaba ':' tras la clave en literal json.")) {
            ast_free(kn);
            goto json_lit_fail;
        }
        ASTNode *vn = parse_expression(p);
        if (!vn) {
            ast_free(kn);
            goto json_lit_fail;
        }
        node_vec_push(&keys, kn);
        node_vec_push(&vals, vn);
        const Token *pafter = peek(p, 0);
        if (pafter && pafter->type == TOK_OPERATOR && pafter->value.str && strcmp(pafter->value.str, "}") == 0) break;
        (void)match(p, TOK_OPERATOR, ",");
    }
    if (!expect(p, TOK_OPERATOR, "}", "Se esperaba '}' para cerrar `json { ... }`.")) goto json_lit_fail;
    MapLiteralNode *mn = calloc(1, sizeof(MapLiteralNode));
    if (!mn) goto json_lit_fail;
    mn->base.type = NODE_JSON_LITERAL;
    mn->base.line = brace ? brace->line : 0;
    mn->base.col = brace ? brace->column : 0;
    mn->keys = keys.arr;
    mn->values = vals.arr;
    mn->n = keys.n;
    return (ASTNode *)mn;
json_lit_fail:
    for (size_t i = 0; i < keys.n; i++) ast_free(keys.arr[i]);
    for (size_t i = 0; i < vals.n; i++) ast_free(vals.arr[i]);
    free(keys.arr);
    free(vals.arr);
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
    if (t->type == TOK_NULL) {
        advance(p);
        LiteralNode *ln = calloc(1, sizeof(LiteralNode));
        if (!ln) return NULL;
        ln->base.type = NODE_LITERAL;
        ln->type_name = strdup("nulo");
        ln->value.i = 0;
        ln->is_float = 0;
        ln->base.line = t->line;
        ln->base.col = t->column;
        return (ASTNode*)ln;
    }
    if (t->type == TOK_STRING) {
        advance(p);
        return make_literal_str(t->value.str);
    }
    if (t->type == TOK_KEYWORD && t->value.str && strcmp(t->value.str, "json") == 0) {
        const Token *nx = peek(p, 1);
        if (nx && nx->type == TOK_OPERATOR && nx->value.str && strcmp(nx->value.str, "{") == 0) {
            advance(p);
            return parse_json_object_literal(p);
        }
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
        for (;;) {
            const Token *pk = peek(p, 0);
            if (!pk) break;
            if (pk->type == TOK_OPERATOR && pk->value.str && strcmp(pk->value.str, "]") == 0) break;
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
        for (;;) {
            const Token *pk = peek(p, 0);
            if (!pk) break;
            if (pk->type == TOK_OPERATOR && pk->value.str && strcmp(pk->value.str, "}") == 0) break;
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
    if (match(p, TOK_KEYWORD, "esperar")) {
        UnaryOpNode *n = calloc(1, sizeof(UnaryOpNode));
        if (!n) return NULL;
        n->base.type = NODE_UNARY_OP;
        n->operator = strdup("esperar");
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
    char *op_owned = NULL;
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
            op_owned = strdup(op);
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
            op_owned = strdup(op);
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "mayor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "o") == 0 && peek(p, 3) && peek(p, 3)->value.str &&
            strcmp(peek(p, 3)->value.str, "igual") == 0 && peek(p, 4) && peek(p, 4)->value.str &&
            strcmp(peek(p, 4)->value.str, "a") == 0) {
            advance(p); advance(p); advance(p); advance(p); advance(p);
            op = is_negated ? "<" : ">=";
            op_consumed = 1;
            op_owned = strdup(op);
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "menor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "o") == 0 && peek(p, 3) && peek(p, 3)->value.str &&
            strcmp(peek(p, 3)->value.str, "igual") == 0 && peek(p, 4) && peek(p, 4)->value.str &&
            strcmp(peek(p, 4)->value.str, "a") == 0) {
            advance(p); advance(p); advance(p); advance(p); advance(p);
            op = is_negated ? ">" : "<=";
            op_consumed = 1;
            op_owned = strdup(op);
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "mayor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "que") == 0) {
            advance(p); advance(p); advance(p);
            op = is_negated ? "<=" : ">";
            op_consumed = 1;
            op_owned = strdup(op);
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            strcmp(peek(p, 1)->value.str, "menor") == 0 && peek(p, 2) && peek(p, 2)->value.str &&
            strcmp(peek(p, 2)->value.str, "que") == 0) {
            advance(p); advance(p); advance(p);
            op = is_negated ? ">=" : "<";
            op_consumed = 1;
            op_owned = strdup(op);
        } else if (strcmp(t->value.str, "es") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            (strcmp(peek(p, 1)->value.str, "distinto") == 0 || strcmp(peek(p, 1)->value.str, "diferente") == 0) &&
            peek(p, 2) && peek(p, 2)->value.str &&
            (strcmp(peek(p, 2)->value.str, "de") == 0 || strcmp(peek(p, 2)->value.str, "a") == 0)) {
            advance(p); advance(p); advance(p);
            op = is_negated ? "==" : "!=";
            op_consumed = 1;
            op_owned = strdup(op);
        }
    } else if (t->type == TOK_IDENTIFIER && t->value.str) {
        if (strcmp(t->value.str, "esta") == 0 && peek(p, 1) && peek(p, 1)->value.str &&
            (strcmp(peek(p, 1)->value.str, "vacia") == 0 || strcmp(peek(p, 1)->value.str, "vacio") == 0)) {
            advance(p); advance(p);
            op = is_negated ? "!=" : "==";
            op_consumed = 1;
            op_owned = strdup(op);
            BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
            b->base.type = NODE_BINARY_OP;
            b->left = left;
            b->operator = op_owned ? op_owned : strdup(op);
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
        b->operator = op_owned ? op_owned : strdup(op);
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
    
    // Limitar el número de operadores 'o' para evitar bucles infinitos
    int max_o_ops = 100;
    int o_count = 0;
    
    while (match(p, TOK_KEYWORD, "o")) {
        if (++o_count > max_o_ops) {
            set_error_here(p, peek(p, 0), "Demasiados operadores 'o' anidados - posible bucle infinito");
            ast_free(left);
            return NULL;
        }
        
        ASTNode *right = parse_logical_and(p);
        if (!right) { 
            ast_free(left); 
            return NULL; 
        }
        
        BinaryOpNode *b = calloc(1, sizeof(BinaryOpNode));
        if (!b) {
            ast_free(left);
            ast_free(right);
            return NULL;
        }
        
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
                strcmp(kw, "asincrono") == 0 || strcmp(kw, "esperar") == 0 ||
                strcmp(kw, "tarea") == 0 ||
                strcmp(kw, "funcion") == 0 || strcmp(kw, "principal") == 0 ||
                strcmp(kw, "retornar") == 0 || strcmp(kw, "lanzar") == 0 ||
                strcmp(kw, "llamar") == 0 ||
                strcmp(kw, "imprimir") == 0 || strcmp(kw, "ingresar_texto") == 0 ||
                strcmp(kw, "ingreso_inmediato") == 0 || strcmp(kw, "limpiar_consola") == 0 ||
                strcmp(kw, "pausa") == 0 ||
                strcmp(kw, "fin_si") == 0 || strcmp(kw, "fin_mientras") == 0 ||
                strcmp(kw, "fin_para_cada") == 0 ||
                strcmp(kw, "fin_hacer") == 0 || strcmp(kw, "fin_para") == 0 ||
                strcmp(kw, "fin_seleccionar") == 0 || strcmp(kw, "fin_intentar") == 0 ||
                strcmp(kw, "fin_funcion") == 0 || strcmp(kw, "fin_principal") == 0 ||
                strcmp(kw, "caso") == 0 || strcmp(kw, "defecto") == 0 ||
                strcmp(kw, "atrapar") == 0 || strcmp(kw, "final") == 0 ||
                strcmp(kw, "registro") == 0 || strcmp(kw, "fin_registro") == 0 ||
                strcmp(kw, "clase") == 0 || strcmp(kw, "fin_clase") == 0) {
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
            if (strcmp(tk->value.str, "principal") == 0 || strcmp(tk->value.str, "registro") == 0 ||
                strcmp(tk->value.str, "clase") == 0)
                return 0;
            if (strcmp(tk->value.str, "fin_registro") == 0 || strcmp(tk->value.str, "fin_clase") == 0)
                return (int)tk->line;
        }
    }
    return 0;
}

static void advance_past_fin_registro_from(Parser *p, size_t from) {
    size_t n = token_vec_size(p->tokens);
    for (size_t i = from; i < n; i++) {
        const Token *tk = token_vec_get(p->tokens, i);
        if (tk && tk->type == TOK_KEYWORD && tk->value.str &&
            (strcmp(tk->value.str, "fin_registro") == 0 || strcmp(tk->value.str, "fin_clase") == 0)) {
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
                            hint = "Falta un operador (como '+' para concatenar) antes de este valor o en medio de las expresiones?";
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
        if (strcmp(t->value.str, "pausa") == 0) {
            int lcl = t->line;
            int lcc = t->column;
            advance(p);
            ASTNode *arg = NULL;
            if (peek(p, 0) && peek(p, 0)->type == TOK_OPERATOR && peek(p, 0)->value.str &&
                strcmp(peek(p, 0)->value.str, "(") == 0) {
                advance(p);
                if (!(peek(p, 0) && peek(p, 0)->type == TOK_OPERATOR && peek(p, 0)->value.str &&
                      strcmp(peek(p, 0)->value.str, ")") == 0)) {
                    arg = parse_expression(p);
                    if (!arg) return NULL;
                }
                if (!expect(p, TOK_OPERATOR, ")", "tras `pausa(` se esperaba `)`")) {
                    ast_free(arg);
                    return NULL;
                }
            }
            CallNode *cn = calloc(1, sizeof(CallNode));
            cn->base.type = NODE_CALL;
            cn->base.line = lcl;
            cn->base.col = lcc;
            cn->name = strdup("pausa");
            cn->n_args = arg ? 1 : 0;
            cn->args = NULL;
            if (arg) {
                cn->args = malloc(sizeof(ASTNode *));
                if (!cn->args) {
                    ast_free(arg);
                    free(cn->name);
                    free(cn);
                    return NULL;
                }
                cn->args[0] = arg;
            }
            return (ASTNode *)cn;
        }
        /* constante tipo nombre = expr (valor obligatorio) */
        if (strcmp(t->value.str, "constante") == 0) {
            advance(p);
            const Token *ty = peek(p, 0);
            const char *tys = token_union_str(ty);
            if (!ty || !tys ||
                (strcmp(tys, "entero") != 0 && strcmp(tys, "texto") != 0 &&
                 strcmp(tys, "flotante") != 0 && strcmp(tys, "caracter") != 0 &&
                 strcmp(tys, "bool") != 0 && strcmp(tys, "u32") != 0 &&
                 strcmp(tys, "u64") != 0 && strcmp(tys, "u8") != 0 &&
                 strcmp(tys, "byte") != 0 && strcmp(tys, "vec2") != 0 &&
                 strcmp(tys, "vec3") != 0 && strcmp(tys, "vec4") != 0 && strcmp(tys, "mat4") != 0 && strcmp(tys, "mat3") != 0)) {
                set_error_here(p, ty ? ty : peek(p, 0),
                    "constante requiere un tipo (entero, texto, flotante, caracter, bool, u32, u64, u8, byte, vec2, vec3, vec4, mat4, mat3).");
                return NULL;
            }
            char *tyn = strdup(tys);
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
        if (strcmp(t->value.str, "objeto") == 0) {
            advance(p);
            const Token *nt = peek(p, 0);
            if (!validate_user_defined_name_tok(p, nt)) return NULL;
            advance(p);
            char *nm = nt && nt->value.str ? strdup(nt->value.str) : NULL;
            ASTNode *val = NULL;
            const Token *nx = peek(p, 0);
            if (nx && nx->type == TOK_OPERATOR && nx->value.str && strcmp(nx->value.str, "=") == 0) {
                advance(p);
                val = parse_expression(p);
            } else if (nx && nx->type == TOK_OPERATOR && nx->value.str && strcmp(nx->value.str, "{") == 0) {
                val = parse_json_object_literal(p);
            } else {
                free(nm);
                set_error_here(p, nx,
                    "objeto <nombre> requiere `=` y una expresion (p. ej. json_parse(...)), "
                    "o un bloque `json { clave: valor ... }` iniciando con `{`.");
                return NULL;
            }
            if (!val) {
                free(nm);
                return NULL;
            }
            VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
            n->base.type = NODE_VAR_DECL;
            n->base.line = nt ? nt->line : t->line;
            n->base.col = nt ? nt->column : t->column;
            n->type_name = strdup("objeto");
            n->name = nm;
            n->value = val;
            n->is_const = 0;
            return (ASTNode *)n;
        }
        if (strcmp(t->value.str, "entero") == 0 || strcmp(t->value.str, "texto") == 0 ||
            strcmp(t->value.str, "flotante") == 0 || strcmp(t->value.str, "bool") == 0 ||
            strcmp(t->value.str, "caracter") == 0 || strcmp(t->value.str, "u32") == 0 ||
            strcmp(t->value.str, "u64") == 0 || strcmp(t->value.str, "u8") == 0 ||
            strcmp(t->value.str, "byte") == 0 || strcmp(t->value.str, "vec2") == 0 ||
            strcmp(t->value.str, "vec3") == 0 || strcmp(t->value.str, "vec4") == 0 || strcmp(t->value.str, "mat4") == 0 || strcmp(t->value.str, "mat3") == 0 ||
            strcmp(t->value.str, "bytes") == 0 || strcmp(t->value.str, "socket") == 0 || strcmp(t->value.str, "tls") == 0 ||
            strcmp(t->value.str, "http_solicitud") == 0 || strcmp(t->value.str, "http_respuesta") == 0 || strcmp(t->value.str, "http_servidor") == 0 ||
            strcmp(t->value.str, "mapa") == 0) {
            char *ty = strdup(t->value.str);
            advance(p);

            /* Suffix opcional '?' para tipos opcionales */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc(strlen(ty) + 2);
                sprintf(new_type, "%s?", ty);
                free(ty);
                ty = new_type;
                advance(p);
            }

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
            
            /* Suffix opcional '?' para tipos opcionales */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc(strlen(ty) + 2);
                sprintf(new_type, "%s?", ty);
                free(ty);
                ty = new_type;
                advance(p);
            }

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
        if (strcmp(t->value.str, "tarea") == 0) {
            char *ty = strdup("tarea");
            advance(p);

            /* Suffix opcional '?' para tipos opcionales */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc(strlen(ty) + 2);
                sprintf(new_type, "%s?", ty);
                free(ty);
                ty = new_type;
                advance(p);
            }

            char *tarea_el = NULL;
            if (!parse_optional_tarea_inner_type_after_tarea_keyword(p, &tarea_el)) {
                free(ty);
                return NULL;
            }
            const Token *nt = peek(p, 0);
            if (!validate_user_defined_name_tok(p, nt)) {
                free(ty);
                free(tarea_el);
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
            n->list_element_type = tarea_el;
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
            const char *ends[] = {"caso", "defecto", "fin_cuando", "fin_si", "sino"};
            ASTNode *body = parse_block(p, ends, 5);
            
            // Check if error occurred inside block
            if (p->last_error) return NULL;

            // Si es un 'cuando' con casos
            const Token *nt = peek(p, 0);
            if (strcmp(si_tok->value.str, "cuando") == 0 && nt && nt->type == TOK_KEYWORD && 
                (strcmp(nt->value.str, "caso") == 0 || strcmp(nt->value.str, "defecto") == 0)) {
                
                SelectCaseVec cases = {0};
                ASTNode *default_body = NULL;
                
                while (1) {
                    const Token *t2 = peek(p, 0);
                    if (!t2 || t2->type == TOK_EOF) break;
                    if (t2->type == TOK_KEYWORD && t2->value.str) {
                        if (strcmp(t2->value.str, "caso") == 0) {
                            advance(p);
                            NodeVec current_case_exprs = {0};
                            while (1) {
                                ASTNode *ce = parse_expression(p);
                                if (!ce) break;
                                node_vec_push(&current_case_exprs, ce);
                                if (!match(p, TOK_OPERATOR, ",")) break;
                            }
                            if (!expect(p, TOK_KEYWORD, "entonces", "Se esperaba 'entonces' tras la lista de valores de 'caso'")) {
                                for (size_t i = 0; i < current_case_exprs.n; i++) ast_free(current_case_exprs.arr[i]);
                                free(current_case_exprs.arr);
                                goto switch_fail;
                            }
                            
                            const char *case_ends[] = {"caso", "defecto", "fin_cuando"};
                            ASTNode *b = parse_block(p, case_ends, 3);
                            
                            SelectCase sc = {0};
                            sc.values = current_case_exprs.arr;
                            sc.n_values = current_case_exprs.n;
                            sc.body = b;
                            sc.is_range = 0;
                            sc.range_end = NULL;
                            
                            select_case_vec_push(&cases, sc);
                            continue;
                        }
                        if (strcmp(t2->value.str, "defecto") == 0) {
                            advance(p);
                            if (default_body) {
                                set_error_here(p, t2, "Solo puede haber un bloque 'defecto' en 'cuando'");
                                goto switch_fail;
                            }
                            const char *def_ends[] = {"fin_cuando"};
                            default_body = parse_block(p, def_ends, 1);
                            continue;
                        }
                        if (strcmp(t2->value.str, "fin_cuando") == 0) {
                            advance(p);
                            break;
                        }
                    }
                    set_error_here(p, t2, "Se esperaba 'caso', 'defecto' o 'fin_cuando'");
                    goto switch_fail;
                }
                
                SelectNode *sn = calloc(1, sizeof(SelectNode));
                sn->base.type = NODE_SELECT;
                sn->selector = cond;
                sn->cases = cases.arr;
                sn->n_cases = cases.n;
                sn->default_body = default_body;
                sn->base.line = si_tok->line;
                sn->base.col = si_tok->column;
                if (body) ast_free(body);
                return (ASTNode*)sn;

            switch_fail:
                if (cond) ast_free(cond);
                if (body) ast_free(body);
                for (size_t i = 0; i < cases.n; i++) {
                    for (size_t j = 0; j < cases.arr[i].n_values; j++) ast_free(cases.arr[i].values[j]);
                    free(cases.arr[i].values);
                    ast_free(cases.arr[i].body);
                }
                if (default_body) ast_free(default_body);
                free(cases.arr);
                return NULL;
            }

            ASTNode *else_b = NULL;
            if (match(p, TOK_KEYWORD, "sino")) {
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
                /* Si el siguiente token no puede iniciar una expresion, es retorno void
                 * (fin de linea / siguiente sentencia como imprimir, si, entero x = ...).
                 * Muchas APIs incorporadas (mem_lista_*, vec2, etc.) se tokenizan como
                 * TOK_KEYWORD; sin esto se descarta la expresion y se emite retornar vacio. */
                int parse_ret_expr = 1;
                if (nx_ret->type == TOK_KEYWORD && nx_ret->value.str) {
                    const char *kw = nx_ret->value.str;
                    size_t kw_len = strlen(kw);
                    int kw_starts_expr = strcmp(kw, "llamar") == 0 || strcmp(kw, "no") == 0;
                    int ctor_kw = strcmp(kw, "vec2") == 0 || strcmp(kw, "vec3") == 0 ||
                                  strcmp(kw, "vec4") == 0 || strcmp(kw, "mat3") == 0 ||
                                  strcmp(kw, "mat4") == 0;
                    if (!kw_starts_expr && !ctor_kw && !is_sistema_llamada(kw, kw_len))
                        parse_ret_expr = 0;
                }
                if (parse_ret_expr)
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
        if (strcmp(t->value.str, "asociar") == 0) {
            int line = t->line;
            int col = t->column;
            advance(p);
            const Token *tk_a = peek(p, 0);
            if (!tk_a || tk_a->type == TOK_EOF) {
                if (p->source_path && p->source_path[0]) {
                    set_error_at(p, line, col,
                        "Archivo %s, linea %d, columna %d: tras `asociar` se esperaba el concepto origen. Ejemplo: asociar \"manzana\" con \"fruta\" o asociar \"manzana\" con \"rojo\" con peso mi_peso.",
                        p->source_path, line, col);
                } else {
                    set_error_at(p, line, col,
                        "linea %d, columna %d: tras `asociar` se esperaba el concepto origen. Ejemplo: asociar \"manzana\" con \"fruta\" o asociar \"manzana\" con \"rojo\" con peso mi_peso.",
                        line, col);
                }
                return NULL;
            }
            ASTNode *concept1 = parse_expression(p);
            if (!concept1) return NULL;
            if (!(match(p, TOK_KEYWORD, "con") || match(p, TOK_IDENTIFIER, "con"))) {
                ast_free(concept1);
                if (p->source_path && p->source_path[0]) {
                    set_error_at(p, line, col,
                        "Archivo %s, linea %d, columna %d: tras `asociar <origen>` se esperaba `con <destino>`. Ejemplo: asociar \"manzana\" con \"fruta\".",
                        p->source_path, line, col);
                } else {
                    set_error_at(p, line, col,
                        "linea %d, columna %d: tras `asociar <origen>` se esperaba `con <destino>`. Ejemplo: asociar \"manzana\" con \"fruta\".",
                        line, col);
                }
                return NULL;
            }
            const Token *tk_b = peek(p, 0);
            if (!tk_b || tk_b->type == TOK_EOF ||
                (tk_b->type == TOK_KEYWORD && tk_b->value.str &&
                 !is_sistema_llamada(tk_b->value.str, strlen(tk_b->value.str)))) {
                ast_free(concept1);
                if (p->source_path && p->source_path[0]) {
                    set_error_at(p, tk_b ? tk_b->line : line, tk_b ? tk_b->column : col,
                        "Archivo %s, linea %d, columna %d: tras `asociar ... con` se esperaba el concepto destino.",
                        p->source_path, tk_b ? tk_b->line : line, tk_b ? tk_b->column : col);
                } else {
                    set_error_at(p, tk_b ? tk_b->line : line, tk_b ? tk_b->column : col,
                        "linea %d, columna %d: tras `asociar ... con` se esperaba el concepto destino.",
                        tk_b ? tk_b->line : line, tk_b ? tk_b->column : col);
                }
                return NULL;
            }
            ASTNode *concept2 = parse_expression(p);
            if (!concept2) {
                ast_free(concept1);
                return NULL;
            }
            ASTNode *weight = NULL;
            if (match(p, TOK_KEYWORD, "peso")) {
                weight = parse_expression(p);
            } else if (match(p, TOK_KEYWORD, "con")) {
                if (!match(p, TOK_KEYWORD, "peso")) {
                    ast_free(concept1);
                    ast_free(concept2);
                    const Token *bad = peek(p, 0);
                    if (p->source_path && p->source_path[0]) {
                        set_error_at(p, bad ? bad->line : line, bad ? bad->column : col,
                            "Archivo %s, linea %d, columna %d: tras `asociar <origen> con <destino> con` se esperaba `peso <valor>`.",
                            p->source_path, bad ? bad->line : line, bad ? bad->column : col);
                    } else {
                        set_error_at(p, bad ? bad->line : line, bad ? bad->column : col,
                            "linea %d, columna %d: tras `asociar <origen> con <destino> con` se esperaba `peso <valor>`.",
                            bad ? bad->line : line, bad ? bad->column : col);
                    }
                    return NULL;
                }
                weight = parse_expression(p);
            }
            AsociarNode *n = calloc(1, sizeof(AsociarNode));
            n->base.type = NODE_ASOCIAR;
            n->base.line = line;
            n->base.col = col;
            n->concept1 = concept1;
            n->concept2 = concept2;
            n->weight = weight;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "aprender") == 0) {
            int line = t->line;
            int col = t->column;
            if (peek(p, 1) && peek(p, 1)->type == TOK_OPERATOR && peek(p, 1)->value.str &&
                strcmp(peek(p, 1)->value.str, "(") == 0) {
                char *name = strdup_safe(t->value.str);
                NodeVec args = {0};
                advance(p);
                if (!expect(p, TOK_OPERATOR, "(", "Se esperaba `(` tras `aprender`.")) {
                    free(name);
                    return NULL;
                }
                while (!match(p, TOK_OPERATOR, ")")) {
                    ASTNode *arg = parse_expression(p);
                    if (!arg) {
                        free(name);
                        for (size_t i = 0; i < args.n; i++) ast_free(args.arr[i]);
                        free(args.arr);
                        return NULL;
                    }
                    node_vec_push(&args, arg);
                    if (match(p, TOK_OPERATOR, ")")) break;
                    if (!expect(p, TOK_OPERATOR, ",", "Entre argumentos de `aprender(...)` se esperaba `,` o `)`. ")) {
                        free(name);
                        for (size_t i = 0; i < args.n; i++) ast_free(args.arr[i]);
                        free(args.arr);
                        return NULL;
                    }
                }
                ASTNode *call = make_call_named(name, args.arr, args.n, line, col);
                free(name);
                return call;
            }
            advance(p);
            const Token *tk_concepto = peek(p, 0);
            if (!tk_concepto || tk_concepto->type == TOK_EOF ||
                (tk_concepto->type == TOK_KEYWORD && tk_concepto->value.str &&
                 !is_sistema_llamada(tk_concepto->value.str, strlen(tk_concepto->value.str)))) {
                if (p->source_path && p->source_path[0]) {
                    set_error_at(p, tk_concepto ? tk_concepto->line : line, tk_concepto ? tk_concepto->column : col,
                        "Archivo %s, linea %d, columna %d: tras `aprender` se esperaba el concepto a reforzar. Ejemplo: aprender \"temperatura\" o aprender \"temperatura\" con peso mi_peso.",
                        p->source_path, tk_concepto ? tk_concepto->line : line, tk_concepto ? tk_concepto->column : col);
                } else {
                    set_error_at(p, tk_concepto ? tk_concepto->line : line, tk_concepto ? tk_concepto->column : col,
                        "linea %d, columna %d: tras `aprender` se esperaba el concepto a reforzar. Ejemplo: aprender \"temperatura\" o aprender \"temperatura\" con peso mi_peso.",
                        tk_concepto ? tk_concepto->line : line, tk_concepto ? tk_concepto->column : col);
                }
                return NULL;
            }
            ASTNode *concept = parse_expression(p);
            if (!concept) return NULL;
            ASTNode *weight = NULL;
            if (match(p, TOK_KEYWORD, "con")) {
                if (!match(p, TOK_KEYWORD, "peso")) {
                    ast_free(concept);
                    const Token *bad = peek(p, 0);
                    if (p->source_path && p->source_path[0]) {
                        set_error_at(p, bad ? bad->line : line, bad ? bad->column : col,
                            "Archivo %s, linea %d, columna %d: tras `aprender <concepto> con` se esperaba `peso <valor>`.",
                            p->source_path, bad ? bad->line : line, bad ? bad->column : col);
                    } else {
                        set_error_at(p, bad ? bad->line : line, bad ? bad->column : col,
                            "linea %d, columna %d: tras `aprender <concepto> con` se esperaba `peso <valor>`.",
                            bad ? bad->line : line, bad ? bad->column : col);
                    }
                    return NULL;
                }
                weight = parse_expression(p);
            } else if (match(p, TOK_KEYWORD, "peso")) {
                weight = parse_expression(p);
            }
            AprenderNode *n = calloc(1, sizeof(AprenderNode));
            n->base.type = NODE_APRENDER;
            n->base.line = line;
            n->base.col = col;
            n->concept = concept;
            n->weight = weight;
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
            int line = t->line;
            int col = t->column;
            advance(p);
            if (match(p, TOK_OPERATOR, "(")) {
                if (!match(p, TOK_OPERATOR, ")")) {
                    const Token *bad = peek(p, 0);
                    if (p->source_path && p->source_path[0]) {
                        set_error_at(p, bad ? bad->line : line, bad ? bad->column : col,
                            "Archivo %s, linea %d, columna %d: `cerrar_memoria` no recibe argumentos. Usa `cerrar_memoria()`.",
                            p->source_path, bad ? bad->line : line, bad ? bad->column : col);
                    } else {
                        set_error_at(p, bad ? bad->line : line, bad ? bad->column : col,
                            "linea %d, columna %d: `cerrar_memoria` no recibe argumentos. Usa `cerrar_memoria()`.",
                            bad ? bad->line : line, bad ? bad->column : col);
                    }
                    return NULL;
                }
            }
            CerrarMemoriaNode *n = calloc(1, sizeof(CerrarMemoriaNode));
            n->base.type = NODE_CERRAR_MEMORIA;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "define_concepto") == 0) {
            int line = t->line;
            int col = t->column;
            advance(p);
            const Token *tk_concepto = peek(p, 0);
            if (!tk_concepto || tk_concepto->type == TOK_EOF) {
                if (p->source_path && p->source_path[0]) {
                    set_error_at(p, line, col,
                        "Archivo %s, linea %d, columna %d: tras `define_concepto` se esperaba el concepto a declarar. Ejemplo: define_concepto \"temperatura\" o define_concepto \"temperatura\" como \"Temperatura del hardware\".",
                        p->source_path, line, col);
                } else {
                    set_error_at(p, line, col,
                        "linea %d, columna %d: tras `define_concepto` se esperaba el concepto a declarar. Ejemplo: define_concepto \"temperatura\" o define_concepto \"temperatura\" como \"Temperatura del hardware\".",
                        line, col);
                }
                return NULL;
            }
            ASTNode *concepto = parse_expression(p);
            if (!concepto) return NULL;
            ASTNode *descripcion = NULL;
            if (match(p, TOK_KEYWORD, "como")) {
                const Token *tk_desc = peek(p, 0);
                if (!tk_desc || tk_desc->type == TOK_EOF ||
                    (tk_desc->type == TOK_KEYWORD && tk_desc->value.str &&
                     !is_sistema_llamada(tk_desc->value.str, strlen(tk_desc->value.str)))) {
                    ast_free(concepto);
                    if (p->source_path && p->source_path[0]) {
                        set_error_at(p, tk_desc ? tk_desc->line : line, tk_desc ? tk_desc->column : col,
                            "Archivo %s, linea %d, columna %d: tras `define_concepto ... como` se esperaba una descripcion (normalmente un texto). Ejemplo valido: define_concepto \"temperatura\" como \"Temperatura del hardware en grados\".",
                            p->source_path, tk_desc ? tk_desc->line : line, tk_desc ? tk_desc->column : col);
                    } else {
                        set_error_at(p, tk_desc ? tk_desc->line : line, tk_desc ? tk_desc->column : col,
                            "linea %d, columna %d: tras `define_concepto ... como` se esperaba una descripcion (normalmente un texto). Ejemplo valido: define_concepto \"temperatura\" como \"Temperatura del hardware en grados\".",
                            tk_desc ? tk_desc->line : line, tk_desc ? tk_desc->column : col);
                    }
                    return NULL;
                }
                descripcion = parse_expression(p);
                if (!descripcion) {
                    ast_free(concepto);
                    return NULL;
                }
            }
            DefineConceptoNode *n = calloc(1, sizeof(DefineConceptoNode));
            n->base.type = NODE_DEFINE_CONCEPTO;
            n->base.line = line;
            n->base.col = col;
            n->concepto = concepto;
            n->descripcion = descripcion;
            return (ASTNode*)n;
        }
        if (strcmp(t->value.str, "copiar_texto") == 0 || strcmp(t->value.str, "ultima_palabra") == 0) {
            int is_copiar = (strcmp(t->value.str, "copiar_texto") == 0);
            int line = t->line;
            int col = t->column;
            advance(p);
            if (!expect(p, TOK_OPERATOR, "(", "Se esperaba `(` tras la sentencia de texto.")) return NULL;
            ASTNode *source = parse_expression(p);
            if (!source) return NULL;
            if (!expect(p, TOK_OPERATOR, ")", "Se esperaba `)` tras la expresion de texto.")) {
                ast_free(source);
                return NULL;
            }
            if (!(match(p, TOK_IDENTIFIER, "en") || match(p, TOK_KEYWORD, "en"))) {
                ast_free(source);
                if (p->source_path && p->source_path[0]) {
                    set_error_at(p, line, col,
                        "Archivo %s, linea %d, columna %d: tras `%s(...)` se esperaba `en <destino>`. Ejemplo: %s(\"hola mundo\") en resultado.",
                        p->source_path, line, col, is_copiar ? "copiar_texto" : "ultima_palabra",
                        is_copiar ? "copiar_texto" : "ultima_palabra");
                } else {
                    set_error_at(p, line, col,
                        "linea %d, columna %d: tras `%s(...)` se esperaba `en <destino>`. Ejemplo: %s(\"hola mundo\") en resultado.",
                        line, col, is_copiar ? "copiar_texto" : "ultima_palabra",
                        is_copiar ? "copiar_texto" : "ultima_palabra");
                }
                return NULL;
            }
            const Token *target_tok = peek(p, 0);
            if (!validate_user_defined_name_tok(p, target_tok)) {
                ast_free(source);
                return NULL;
            }
            char *target = target_tok && target_tok->value.str ? strdup(target_tok->value.str) : NULL;
            advance(p);
            if (is_copiar) {
                CopiarTextoNode *n = calloc(1, sizeof(CopiarTextoNode));
                n->base.type = NODE_COPIAR_TEXTO;
                n->base.line = line;
                n->base.col = col;
                n->source = source;
                n->target = target;
                return (ASTNode*)n;
            }
            UltimaPalabraNode *n = calloc(1, sizeof(UltimaPalabraNode));
            n->base.type = NODE_ULTIMA_PALABRA;
            n->base.line = line;
            n->base.col = col;
            n->source = source;
            n->target = target;
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
        /* Guardar posicion para backtrack si no es una declaracion */
        size_t start_pos = p->pos;
        char *ty = parse_full_type_name(p);
        if (ty) {
            const Token *name_tok = peek(p, 0);
            /* Si despues del tipo viene un identificador, es una declaracion: Tipo var */
            if (name_tok && (name_tok->type == TOK_IDENTIFIER || 
                            (name_tok->type == TOK_KEYWORD && name_tok->value.str && keyword_ok_as_user_identifier(name_tok->value.str)))) {
                char *nm = strdup(name_tok->value.str);
                advance(p);
                
                /* Suffix opcional '?' para tipos opcionales (ej: mapa?) */
                const Token *q_var = peek(p, 0);
                if (q_var && q_var->type == TOK_OPERATOR && q_var->value.str && strcmp(q_var->value.str, "?") == 0) {
                    char *new_ty = malloc(strlen(ty) + 2);
                    sprintf(new_ty, "%s?", ty);
                    free(ty);
                    ty = new_ty;
                    advance(p);
                }

                char *lista_el = NULL;
                if (ty && (strcmp(ty, "lista") == 0 || strcmp(ty, "lista?") == 0 ||
                           strcmp(ty, "mapa") == 0 || strcmp(ty, "mapa?") == 0)) {
                    lista_el = parse_optional_lista_element_type(p);
                    if (p->last_error) {
                        free(ty); free(nm);
                        return NULL;
                    }
                } else if (ty && (strcmp(ty, "tarea") == 0 || strcmp(ty, "tarea?") == 0)) {
                    if (!parse_optional_tarea_inner_type_after_tarea_keyword(p, &lista_el)) {
                        free(ty); free(nm);
                        return NULL;
                    }
                }

                ASTNode *val = NULL;
                if (match(p, TOK_OPERATOR, "=")) {
                    if (strcmp(ty, "macro") == 0) {
                        val = parse_lambda(p);
                    } else {
                        val = parse_expression(p);
                    }
                    if (!val) {
                        free(ty); free(nm); free(lista_el);
                        return NULL;
                    }
                }
                VarDeclNode *n = calloc(1, sizeof(VarDeclNode));
                n->base.type = NODE_VAR_DECL;
                n->base.line = name_tok->line;
                n->base.col = name_tok->column;
                n->type_name = ty;
                n->name = nm;
                n->value = val;
                n->is_const = 0;
                n->list_element_type = lista_el;
                return (ASTNode*)n;
            }
            /* No era una declaracion, backtrack */
            free(ty);
            p->pos = start_pos;
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

static ASTNode *parse_function(Parser *p, int is_exported, int is_async) {
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
            if (ty_err && ty_err->type == TOK_KEYWORD && ty_err->value.str && 
                (strcmp(ty_err->value.str, "lista") == 0 || strcmp(ty_err->value.str, "mapa") == 0)) {
                type_str = strdup(ty_err->value.str);
                advance(p);
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
                type_str = parse_full_type_name(p);
                if (!type_str) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, ty_err ? ty_err->line : 0, ty_err ? ty_err->column : 0,
                                  "Archivo %s, linea %d, columna %d: parametro invalido en firma de funcion. Se esperaba un tipo (entero/texto/flotante/...), pero se encontro `%s`.",
                                  p->source_path, ty_err ? ty_err->line : 0, ty_err ? ty_err->column : 0, ty_err && ty_err->value.str ? ty_err->value.str : "EOF");
                    else
                        set_error_at(p, ty_err ? ty_err->line : 0, ty_err ? ty_err->column : 0,
                                  "linea %d, columna %d: parametro invalido en firma de funcion. Se esperaba un tipo (entero/texto/flotante/...), pero se encontro `%s`.",
                                  ty_err ? ty_err->line : 0, ty_err ? ty_err->column : 0, ty_err && ty_err->value.str ? ty_err->value.str : "EOF");
                    for (size_t k = 0; k < params.n; k++) ast_free((ASTNode *)params.arr[k]);
                    free(params.arr);
                    free(name);
                    return NULL;
                }
            }
            
            /* Suffix opcional '?' para tipos opcionales */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc(strlen(type_str) + 2);
                sprintf(new_type, "%s?", type_str);
                free(type_str);
                type_str = new_type;
                advance(p);
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
    {
        const Token *pk_ret = peek(p, 0);
        const char *rts = token_union_str(pk_ret);
        if (!match(p, TOK_KEYWORD, "retorna") && rts && strcmp(rts, "retorna") == 0)
            advance(p);  /* consumir "retorna" si match fallo (p.ej. lexed como ID) */
    }
    char *ret_type = strdup("entero");
    char *return_task_elem = NULL;
    /* Solo consumir tipo de retorno si acabamos de ver "retorna" Y el siguiente token es un tipo válido */
    const Token *rt = peek(p, 0);
    const char *rts_rt = token_union_str(rt);
    if (rts_rt && strcmp(rts_rt, "fin_funcion") != 0 &&
        (strcmp(rts_rt, "entero") == 0 || strcmp(rts_rt, "texto") == 0 ||
         strcmp(rts_rt, "flotante") == 0 || strcmp(rts_rt, "caracter") == 0 ||
         strcmp(rts_rt, "bool") == 0 || strcmp(rts_rt, "lista") == 0 ||
         strcmp(rts_rt, "mapa") == 0 || strcmp(rts_rt, "u32") == 0 ||
         strcmp(rts_rt, "u64") == 0 || strcmp(rts_rt, "u8") == 0 ||
         strcmp(rts_rt, "byte") == 0 || strcmp(rts_rt, "vec2") == 0 ||
         strcmp(rts_rt, "vec3") == 0 || strcmp(rts_rt, "vec4") == 0 || strcmp(rts_rt, "mat4") == 0 || strcmp(rts_rt, "mat3") == 0 ||
         strcmp(rts_rt, "bytes") == 0 || strcmp(rts_rt, "socket") == 0 || strcmp(rts_rt, "tls") == 0 ||
         strcmp(rts_rt, "http_solicitud") == 0 || strcmp(rts_rt, "http_respuesta") == 0 || strcmp(rts_rt, "http_servidor") == 0 ||
         strcmp(rts_rt, "tarea") == 0)) {
        free(ret_type);
        ret_type = strdup_safe(rts_rt);
        advance(p);
        
        /* Suffix opcional '?' para tipos de retorno opcionales */
        const Token *q_ret = peek(p, 0);
        if (q_ret && q_ret->type == TOK_OPERATOR && q_ret->value.str && strcmp(q_ret->value.str, "?") == 0) {
            char *new_ret = malloc(strlen(ret_type) + 2);
            sprintf(new_ret, "%s?", ret_type);
            free(ret_type);
            ret_type = new_ret;
            advance(p);
        }

        if (ret_type && (strcmp(ret_type, "lista") == 0 || strcmp(ret_type, "lista?") == 0)) {
            return_task_elem = parse_optional_lista_element_type(p);
        } else if (ret_type && (strcmp(ret_type, "mapa") == 0 || strcmp(ret_type, "mapa?") == 0)) {
            return_task_elem = parse_optional_lista_element_type(p); /* Para mapa<T> si se desea */
        } else if (ret_type && strcmp(ret_type, "tarea") == 0) {
            if (!parse_optional_tarea_inner_type_after_tarea_keyword(p, &return_task_elem)) {
                free(ret_type);
                for (size_t k = 0; k < params.n; k++) ast_free((ASTNode*)params.arr[k]);
                free(params.arr);
                free(name);
                return NULL;
            }
        }
    }
    const char *ends[] = {"fin_funcion"};
    ASTNode *body = parse_block(p, ends, 1);
    
    // Check if we encountered an error while parsing the body and didn't find fin_funcion
    if (!match(p, TOK_KEYWORD, "fin_funcion")) {
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
        free(return_task_elem);
        return NULL;
    }
    FunctionNode *fn = calloc(1, sizeof(FunctionNode));
    fn->base.type = NODE_FUNCTION;
    fn->base.line = func_tok ? func_tok->line : 0;
    fn->base.col = func_tok ? func_tok->column : 0;
    fn->name = name;
    fn->return_type = (char*)ret_type;
    fn->return_task_elem = return_task_elem;
    fn->params = (ASTNode**)params.arr;
    fn->n_params = params.n;
    fn->body = body;
    fn->is_exported = is_exported ? 1 : 0;
    fn->is_async = is_async ? 1 : 0;
    return (ASTNode*)fn;
}

/* 1 = clase/fin_clase; 0 = registro/fin_registro */
static int struct_kw_is_closer(const char *kw, int is_clase) {
    if (!kw) return 0;
    if (is_clase) return strcmp(kw, "fin_clase") == 0;
    return strcmp(kw, "fin_registro") == 0;
}

/* Tras consumir `registro` o `clase` desde el nivel superior. */
static ASTNode *parse_struct_body(Parser *p, int is_clase, int is_exported, const char *parent_name) {
    const Token *nt = peek(p, 0);
    if (!validate_user_defined_name_tok(p, nt)) return NULL;
    advance(p);
    
    char *raw_name = nt && nt->value.str ? strdup(nt->value.str) : NULL;
    char *name = NULL;
    if (parent_name && raw_name) {
        name = malloc(strlen(parent_name) + strlen(raw_name) + 2);
        sprintf(name, "%s.%s", parent_name, raw_name);
        free(raw_name);
    } else {
        name = raw_name;
    }
    
    char *extends_name = NULL;
    if (peek(p, 0) && peek(p, 0)->type == TOK_KEYWORD && peek(p, 0)->value.str &&
        strcmp(peek(p, 0)->value.str, "extiende") == 0) {
        advance(p);
        const Token *base_tok = peek(p, 0);
        if (!validate_user_defined_name_tok(p, base_tok)) {
            free(name);
            return NULL;
        }
        extends_name = base_tok && base_tok->value.str ? strdup(base_tok->value.str) : NULL;
        advance(p);
    }
    char **ft = NULL, **fn = NULL;
    int *fv = NULL;
    size_t nf = 0, fcap = 0;
    
    ASTNode **methods = NULL;
    int *mv = NULL;
    size_t nm = 0, mcap = 0;

    ASTNode **nested = NULL;
    size_t nn_structs = 0, ncap = 0;

    while (peek(p, 0)) {
        const Token *phead = peek(p, 0);
        const char *kws = token_union_str(phead);
        if (!kws || struct_kw_is_closer(kws, is_clase)) break;
        
        int visibility = 0; /* 0 = publico, 1 = privado */
        if (phead->type == TOK_KEYWORD && strcmp(phead->value.str, "privado") == 0) {
            visibility = 1;
            advance(p);
            phead = peek(p, 0);
        }

        const Token *tblk = peek(p, 0);
        if (tblk && tblk->type == TOK_KEYWORD && tblk->value.str) {
            const char *kw = tblk->value.str;
            if (strcmp(kw, "funcion") == 0) {
                if (!is_clase) {
                    set_error_at(p, tblk->line, tblk->column, "No se permiten funciones en registros; use una clase.");
                    goto parse_struct_body_fail;
                }
                ASTNode *method = parse_function(p, 0, 0);
                if (!method) goto parse_struct_body_fail;
                
                if (nm >= mcap) {
                    mcap = mcap ? mcap * 2 : 4;
                    methods = realloc(methods, mcap * sizeof(ASTNode*));
                    mv = realloc(mv, mcap * sizeof(int));
                }
                methods[nm] = method;
                mv[nm] = visibility;
                nm++;
                continue;
            }
            if (strcmp(kw, "registro") == 0 || strcmp(kw, "clase") == 0) {
                advance(p);
                int nested_is_clase = (strcmp(kw, "clase") == 0);
                ASTNode *nested_node = parse_struct_body(p, nested_is_clase, 0, name);
                if (!nested_node) goto parse_struct_body_fail;
                
                if (nn_structs >= ncap) {
                    ncap = ncap ? ncap * 2 : 4;
                    nested = realloc(nested, ncap * sizeof(ASTNode*));
                }
                nested[nn_structs++] = nested_node;
                continue;
            }
            if (is_clase && strcmp(kw, "fin_registro") == 0) {
                set_error_at(p, tblk->line, tblk->column, "Cierre de clase '%s' debe ser `fin_clase`.", name);
                goto parse_struct_body_fail;
            }
            if (!is_clase && strcmp(kw, "fin_clase") == 0) {
                set_error_at(p, tblk->line, tblk->column, "Cierre de registro '%s' debe ser `fin_registro`.", name);
                goto parse_struct_body_fail;
            }
            if (strcmp(kw, "principal") == 0 || strcmp(kw, "asincrono") == 0 ||
                strcmp(kw, "extiende") == 0 ||
                strcmp(kw, "activar_modulo") == 0 || strcmp(kw, "usar") == 0 || strcmp(kw, "fin_principal") == 0) {
                goto parse_struct_body_fail;
            }
        }
        
        const Token *ty_tok = peek(p, 0);
        char *type_name = NULL;
        if (ty_tok && ty_tok->type == TOK_KEYWORD && ty_tok->value.str && 
            (strcmp(ty_tok->value.str, "lista") == 0 || strcmp(ty_tok->value.str, "mapa") == 0)) {
            advance(p);
            type_name = strdup(ty_tok->value.str);
            /* Suffix opcional '?' */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc(strlen(type_name) + 2);
                sprintf(new_type, "%s?", type_name);
                free(type_name);
                type_name = new_type;
                advance(p);
            }
            char *elem_type = parse_optional_lista_element_type(p);
            if (elem_type) {
                char *new_type = malloc(strlen(type_name) + strlen(elem_type) + 3);
                sprintf(new_type, "%s<%s>", type_name, elem_type);
                free(type_name);
                type_name = new_type;
                free(elem_type);
            }
        } else if (ty_tok && ty_tok->type == TOK_KEYWORD && ty_tok->value.str && strcmp(ty_tok->value.str, "tarea") == 0) {
            advance(p);
            type_name = strdup("tarea");
            /* Suffix opcional '?' */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc(strlen(type_name) + 2);
                sprintf(new_type, "%s?", type_name);
                free(type_name);
                type_name = new_type;
                advance(p);
            }
            char *tarea_el = NULL;
            if (parse_optional_tarea_inner_type_after_tarea_keyword(p, &tarea_el) && tarea_el) {
                char *new_type = malloc(strlen(type_name) + strlen(tarea_el) + 3);
                sprintf(new_type, "%s<%s>", type_name, tarea_el);
                free(type_name);
                type_name = new_type;
                free(tarea_el);
            }
        } else {
            const Token *ty = peek(p, 0);
            if (!ty) goto parse_struct_body_fail;
            advance(p);
            type_name = ty->value.str ? strdup(ty->value.str) : NULL;
            /* Suffix opcional '?' */
            const Token *q = peek(p, 0);
            if (q && q->type == TOK_OPERATOR && q->value.str && strcmp(q->value.str, "?") == 0) {
                char *new_type = malloc((type_name ? strlen(type_name) : 0) + 2);
                sprintf(new_type, "%s?", type_name ? type_name : "");
                if (type_name) free(type_name);
                type_name = new_type;
                advance(p);
            }
        }

        const Token *fld = peek(p, 0);
        if (!fld || !validate_user_defined_name_tok(p, fld)) {
            free(type_name);
            goto parse_struct_body_fail;
        }
        advance(p);
        
        if (nf >= fcap) {
            fcap = fcap ? fcap * 2 : 4;
            ft = realloc(ft, fcap * sizeof(char*));
            fn = realloc(fn, fcap * sizeof(char*));
            fv = realloc(fv, fcap * sizeof(int));
        }
        ft[nf] = type_name;
        fn[nf] = fld->value.str ? strdup(fld->value.str) : NULL;
        fv[nf] = visibility;
        nf++;
    }
    
    const char *fkw = is_clase ? "fin_clase" : "fin_registro";
    if (!expect(p, TOK_KEYWORD, fkw, "Cierre de clase/registro esperado"))
        goto parse_struct_body_fail;

    StructDefNode *sn = calloc(1, sizeof(StructDefNode));
    sn->base.type = NODE_STRUCT_DEF;
    sn->base.line = nt ? nt->line : 0;
    sn->base.col = nt ? nt->column : 0;
    sn->name = name;
    sn->extends_name = extends_name;
    sn->field_types = ft;
    sn->field_names = fn;
    sn->field_visibilities = fv;
    sn->n_fields = nf;
    sn->methods = methods;
    sn->method_visibilities = mv;
    sn->n_methods = nm;
    sn->nested_structs = nested;
    sn->n_nested_structs = nn_structs;
    sn->is_exported = is_exported;
    return (ASTNode*)sn;

parse_struct_body_fail:
    for (size_t i = 0; i < nf; i++) { free(ft[i]); free(fn[i]); }
    free(ft); free(fn); free(fv);
    for (size_t i = 0; i < nm; i++) ast_free(methods[i]);
    free(methods); free(mv);
    for (size_t i = 0; i < nn_structs; i++) ast_free(nested[i]);
    free(nested);
    free(name); free(extends_name);
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
    int estructura_imports_added = 0;
    while (p->pos < token_vec_size(p->tokens)) {
        const Token *start_t = peek(p, 0);
        int stmt_line = start_t ? start_t->line : 0;
        int stmt_col = start_t ? start_t->column : 0;
        const Token *t = peek(p, 0);
        if (!t || t->type == TOK_EOF) break;

        if (t->type == TOK_KEYWORD && t->value.str) {
            if (strcmp(t->value.str, "asincrono") == 0) {
                advance(p);
                const Token *t2 = peek(p, 0);
                if (!t2 || t2->type != TOK_KEYWORD || !t2->value.str) {
                    set_error_here(p, t2 ? t2 : peek(p, 0), "Tras `asincrono` se esperaba `funcion` o `enviar funcion`.");
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    parser_synchronize(p);
                    continue;
                }
                if (strcmp(t2->value.str, "enviar") == 0) {
                    advance(p);
                    const Token *t3 = peek(p, 0);
                    if (t3 && t3->type == TOK_KEYWORD && t3->value.str && strcmp(t3->value.str, "funcion") == 0) {
                        ASTNode *fn = parse_function(p, 1, 1);
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
                    set_error_here(p, t3 ? t3 : peek(p, 0), "Tras `asincrono enviar` se esperaba `funcion`.");
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    parser_synchronize(p);
                    continue;
                }
                if (strcmp(t2->value.str, "funcion") == 0) {
                    ASTNode *fn = parse_function(p, 0, 1);
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
                set_error_here(p, t2, "Tras `asincrono` se esperaba `funcion` o `enviar funcion`.");
                parser_accumulate_error(p, p->last_error);
                free(p->last_error);
                p->last_error = NULL;
                parser_synchronize(p);
                continue;
            }
            if (strcmp(t->value.str, "enviar") == 0) {
                advance(p);
                const Token *nx = peek(p, 0);
                if (nx && nx->value.str) {
                    if (nx->type == TOK_KEYWORD) {
                        if (strcmp(nx->value.str, "funcion") == 0) {
                            ASTNode *fn = parse_function(p, 1, 0);
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
                        if (strcmp(nx->value.str, "clase") == 0) {
                            advance(p);
                            ASTNode *sd = parse_struct_body(p, 1, 1, NULL);
                            if (sd) {
                                node_vec_push(&globals, sd);
                            } else if (p->last_error) {
                                parser_accumulate_error(p, p->last_error);
                                free(p->last_error);
                                p->last_error = NULL;
                                parser_synchronize(p);
                            }
                            continue;
                        }
                        if (strcmp(nx->value.str, "registro") == 0) {
                            advance(p);
                            ASTNode *sd = parse_struct_body(p, 0, 1, NULL);
                            if (sd) {
                                node_vec_push(&globals, sd);
                            } else if (p->last_error) {
                                parser_accumulate_error(p, p->last_error);
                                free(p->last_error);
                                p->last_error = NULL;
                                parser_synchronize(p);
                            }
                            continue;
                        }
                    }
                    if (nx->type == TOK_OPERATOR && strcmp(nx->value.str, "{") == 0) {
                        int eline = nx->line, ecol = nx->column;
                        advance(p);
                        char **names = NULL;
                        size_t nn = 0, cap = 0;
                        while (p->pos < token_vec_size(p->tokens)) {
                            const Token *id = peek(p, 0);
                            if (!id || (id->type == TOK_OPERATOR && id->value.str && strcmp(id->value.str, "}") == 0))
                                break;
                            if (!validate_user_defined_name_tok(p, id)) break;
                            if (nn >= cap) {
                                cap = cap ? cap * 2 : 4;
                                char **n2 = realloc(names, cap * sizeof(char *));
                                if (!n2) break;
                                names = n2;
                            }
                            names[nn++] = strdup_safe(id->value.str);
                            advance(p);
                            if (!match(p, TOK_OPERATOR, ",")) break;
                        }
                        if (!expect(p, TOK_OPERATOR, "}", "Falta `}` para cerrar `enviar { ... }`.")) {
                            for (size_t k = 0; k < nn; k++) free(names[k]);
                            free(names);
                            parser_synchronize(p);
                            continue;
                        }
                        ASTNode *en = make_export_directive(names, nn, eline, ecol);
                        node_vec_push(&globals, en);
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
                ASTNode *fn = parse_function(p, 0, 0);
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
                advance(p);
                ASTNode *sd = parse_struct_body(p, 0, 0, NULL);
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
            if (strcmp(t->value.str, "clase") == 0) {
                advance(p);
                ASTNode *sd = parse_struct_body(p, 1, 0, NULL);
                if (sd) {
                    node_vec_push(&globals, sd);
                } else if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
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
            if (strcmp(t->value.str, "fin_clase") == 0) {
                const Token *fr = t;
                if (p->source_path && p->source_path[0])
                    set_error_at(p, fr->line, fr->column,
                        "Archivo %s, linea %d, columna %d: `fin_clase` sin un `clase` que lo abra. "
                        "Cada clase debe empezar con `clase` y el nombre del tipo antes de los campos, y terminar con `fin_clase`.",
                        p->source_path, fr->line, fr->column);
                else
                    set_error_at(p, fr->line, fr->column,
                        "linea %d, columna %d: `fin_clase` sin un `clase` que lo abra.",
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

        if (token_is_word(t, "Estructa")) {
            if (!ensure_estructura_runtime_imports(p, &globals, &estructura_imports_added, t->line, t->column)) {
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                }
                break;
            }
            ASTNode *fn = parse_estructura_component(p);
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

        if (token_is_word(t, "componente")) {
            if (!ensure_estructura_runtime_imports(p, &globals, &estructura_imports_added, t->line, t->column)) {
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                }
                break;
            }
            ASTNode *fn = parse_estructura_component_v2(p);
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

        if (token_is_word(t, "vista")) {
            if (!ensure_estructura_runtime_imports(p, &globals, &estructura_imports_added, t->line, t->column)) {
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                }
                break;
            }
            ASTNode *fn = parse_estructura_view_v2(p);
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

        if (token_is_word(t, "tema")) {
            if (!ensure_estructura_runtime_imports(p, &globals, &estructura_imports_added, t->line, t->column)) {
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                }
                break;
            }
            ASTNode *fn = parse_estructura_theme_v2(p);
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

        if (token_is_word(t, "aplicacion")) {
            if (!parse_estructura_application(p, &funcs, &globals, &main_stmts, &estructura_imports_added)) {
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    parser_synchronize(p);
                    continue;
                }
                break;
            }
            continue;
        }

        if (token_is_word(t, "app")) {
            if (!parse_estructura_app_v2(p, &funcs, &globals, &main_stmts, &estructura_imports_added)) {
                if (p->last_error) {
                    parser_accumulate_error(p, p->last_error);
                    free(p->last_error);
                    p->last_error = NULL;
                    parser_synchronize(p);
                    continue;
                }
                break;
            }
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
                if (nx && nx->type == TOK_KEYWORD && nx->value.str &&
                    strcmp(nx->value.str, "fin_clase") == 0) {
                    if (p->source_path && p->source_path[0])
                        set_error_at(p, nx->line, nx->column,
                            "Archivo %s, linea %d, columna %d: `fin_clase` sin un `clase` que lo abra.",
                            p->source_path, nx->line, nx->column);
                    else
                        set_error_at(p, nx->line, nx->column,
                            "linea %d, columna %d: `fin_clase` sin un `clase` que lo abra.",
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
        rem = peek(p, 0);
        if (rem && rem->type == TOK_KEYWORD && rem->value.str &&
            strcmp(rem->value.str, "fin_clase") == 0) {
            if (p->source_path && p->source_path[0])
                set_error_at(p, rem->line, rem->column,
                          "Archivo %s, linea %d, columna %d: `fin_clase` sin un `clase` que lo abra.",
                          p->source_path, rem->line, rem->column);
            else
                set_error_at(p, rem->line, rem->column,
                          "linea %d, columna %d: `fin_clase` sin un `clase` que lo abra.",
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
