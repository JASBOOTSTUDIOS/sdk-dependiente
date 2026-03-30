/* jbc - Compilador Jasboot en C - Nivel 9 CLI */
#include "lexer.h"
#include "token_vec.h"
#include "parser.h"
#include "diagnostic.h"
#include "nodes.h"
#include "symbol_table.h"
#include "resolve.h"
#include "codegen.h"
#include "jbc_ir_opt.h"
#include "opcodes.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <process.h>
#include <direct.h>
#include <io.h>
/* No incluir windows.h: en MinGW choca con TokenType en lexer.h */
typedef void *HMODULE_;
typedef unsigned long DWORD_;
#ifndef WINAPI
#define WINAPI __stdcall
#endif
__declspec(dllimport) DWORD_ WINAPI GetModuleFileNameA(HMODULE_ hModule, char *lpFilename, DWORD_ nSize);
#define chdir _chdir
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#define PATH_SEP '/'
#endif

/* Directorio que contiene jbc.exe (sin barra final); para encontrar la VM aunque cwd este en tests profundos. */
static char g_jbc_exe_dir[1024];

static void init_jbc_exe_dir(void) {
    g_jbc_exe_dir[0] = '\0';
#ifdef _WIN32
    char abs[2048];
    if (GetModuleFileNameA((HMODULE_)NULL, abs, (DWORD_)sizeof abs) == 0 || abs[0] == '\0')
        return;
    char *last = strrchr(abs, '\\');
    if (!last)
        last = strrchr(abs, '/');
    if (!last)
        return;
    *last = '\0';
    snprintf(g_jbc_exe_dir, sizeof g_jbc_exe_dir, "%s", abs);
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", g_jbc_exe_dir, sizeof g_jbc_exe_dir - 1);
    if (n <= 0)
        return;
    g_jbc_exe_dir[n] = '\0';
    char *last = strrchr(g_jbc_exe_dir, '/');
    if (last)
        *last = '\0';
#endif
}

static int verbose_flag = 0;
/* 1 = tras codegen, pasar IR por ir_optimize (experimental; usar --ir-opt). */
static int jbc_ir_opt_enabled = 0;
int werror_unused = 0;

#define ANSI_YELLOW "\x1b[33m"
#define ANSI_RED    "\x1b[31m"
#define ANSI_RESET  "\x1b[0m"

static const char *node_type_str(NodeType t) {
    switch (t) {
        case NODE_PROGRAM: return "Program";
        case NODE_BLOCK: return "Block";
        case NODE_FUNCTION: return "Function";
        case NODE_PRINT: return "Print";
        case NODE_VAR_DECL: return "VarDecl";
        case NODE_ASSIGNMENT: return "Assignment";
        case NODE_IDENTIFIER: return "Identifier";
        case NODE_LITERAL: return "Literal";
        case NODE_CALL: return "Call";
        case NODE_WHILE: return "While";
        case NODE_FOREACH: return "ForEach";
        case NODE_DO_WHILE: return "DoWhile";
        case NODE_POSTFIX_UPDATE: return "PostfixUpdate";
        case NODE_SELECT: return "Select";
        case NODE_TRY: return "Try";
        case NODE_THROW: return "Throw";
        case NODE_IF: return "If";
        case NODE_RETURN: return "Return";
        case NODE_RECORDAR: return "Recordar";
        default: return "?";
    }
}

static void print_ast(ASTNode *n, int indent) {
    if (!n) return;
    for (int i = 0; i < indent; i++) printf("  ");
    printf("%s", node_type_str(n->type));
    switch (n->type) {
        case NODE_PROGRAM: {
            ProgramNode *p = (ProgramNode*)n;
            printf(" (funcs=%zu, globals=%zu)\n", p->n_funcs, p->n_globals);
            for (size_t i = 0; i < p->n_funcs; i++) print_ast(p->functions[i], indent + 1);
            print_ast(p->main_block, indent + 1);
            for (size_t i = 0; i < p->n_globals; i++) print_ast(p->globals[i], indent + 1);
            break;
        }
        case NODE_BLOCK: {
            BlockNode *b = (BlockNode*)n;
            printf(" (%zu stmts)\n", b->n);
            for (size_t i = 0; i < b->n; i++) print_ast(b->statements[i], indent + 1);
            break;
        }
        case NODE_FUNCTION: {
            FunctionNode *f = (FunctionNode*)n;
            printf(" %s\n", f->name ? f->name : "?");
            print_ast(f->body, indent + 1);
            break;
        }
        case NODE_PRINT:
            printf("\n");
            print_ast(((PrintNode*)n)->expression, indent + 1);
            break;
        case NODE_VAR_DECL: {
            VarDeclNode *v = (VarDeclNode*)n;
            printf(" %s %s\n", v->type_name ? v->type_name : "?", v->name ? v->name : "?");
            if (v->value) print_ast(v->value, indent + 1);
            break;
        }
        case NODE_IDENTIFIER:
            printf(" %s\n", ((IdentifierNode*)n)->name ? ((IdentifierNode*)n)->name : "?");
            break;
        case NODE_LITERAL: {
            LiteralNode *l = (LiteralNode*)n;
            if (l->type_name && strcmp(l->type_name, "texto") == 0)
                printf(" \"%s\"\n", l->value.str ? l->value.str : "");
            else if (l->is_float)
                printf(" %g\n", l->value.f);
            else
                printf(" %lld\n", (long long)l->value.i);
            break;
        }
        case NODE_RECORDAR:
            printf("\n");
            print_ast(((RecordarNode*)n)->key, indent + 1);
            if (((RecordarNode*)n)->value) print_ast(((RecordarNode*)n)->value, indent + 1);
            break;
        default:
            printf("\n");
            break;
    }
}

/* Carga transitiva de `usar`/`activar_modulo`: fusion de funciones, firmas externas si el parse falla,
 * error si falta el archivo, dependencia circular, o carga duplicada idempotente. */
typedef struct {
    char *module_canon;   /* ruta canonica del .jasb que se esta cargando en este nivel */
    char *referrer_path;  /* archivo donde aparece el `usar` que importa module_canon */
    int usar_line;
    int usar_col;
} UsarStackFrame;

typedef struct {
    UsarStackFrame *frames;
    size_t n;
    size_t cap;
} UsarPathStack;

typedef struct {
    char **paths;
    size_t n;
    size_t cap;
} UsarLoadedSet;

static void usar_stack_free(UsarPathStack *s) {
    if (!s) return;
    for (size_t i = 0; i < s->n; i++) {
        free(s->frames[i].module_canon);
        free(s->frames[i].referrer_path);
    }
    free(s->frames);
    s->frames = NULL;
    s->n = s->cap = 0;
}

static int usar_stack_push(UsarPathStack *s, const char *module_canon, const char *referrer_path, int usar_line, int usar_col) {
    char *mc = strdup(module_canon ? module_canon : "");
    char *rp = strdup(referrer_path ? referrer_path : "");
    if (!mc || !rp) {
        free(mc);
        free(rp);
        return -1;
    }
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 8;
        UsarStackFrame *nf = realloc(s->frames, nc * sizeof(UsarStackFrame));
        if (!nf) {
            free(mc);
            free(rp);
            return -1;
        }
        s->frames = nf;
        s->cap = nc;
    }
    s->frames[s->n].module_canon = mc;
    s->frames[s->n].referrer_path = rp;
    s->frames[s->n].usar_line = usar_line;
    s->frames[s->n].usar_col = usar_col;
    s->n++;
    return 0;
}

static void usar_stack_pop(UsarPathStack *s) {
    if (!s || s->n == 0) return;
    size_t i = --s->n;
    free(s->frames[i].module_canon);
    free(s->frames[i].referrer_path);
    s->frames[i].module_canon = NULL;
    s->frames[i].referrer_path = NULL;
}

static int usar_stack_contains(const UsarPathStack *s, const char *canon) {
    if (!s || !canon) return 0;
    for (size_t i = 0; i < s->n; i++)
        if (s->frames[i].module_canon && strcmp(s->frames[i].module_canon, canon) == 0) return 1;
    return 0;
}

/* Indice del primer marco cuyo modulo coincide (inicio del ciclo en la pila), o (size_t)-1. */
static size_t usar_stack_cycle_start(const UsarPathStack *s, const char *canon) {
    if (!s || !canon) return (size_t)-1;
    for (size_t i = 0; i < s->n; i++)
        if (s->frames[i].module_canon && strcmp(s->frames[i].module_canon, canon) == 0) return i;
    return (size_t)-1;
}

static void usar_emit_cycle_error(const UsarPathStack *stack, const char *duplicate_canon,
    const char *trigger_referrer, int trigger_line, int trigger_col, const char *trigger_target_display,
    const char *compile_entry_diag) {
    size_t first = usar_stack_cycle_start(stack, duplicate_canon);
    int tl = trigger_line >= 1 ? trigger_line : 1;
    int tc = trigger_col >= 1 ? trigger_col : 1;

    fprintf(stderr, "%s%s, linea %d, columna %d: error: `usar`: dependencia circular entre modulos.%s\n",
        ANSI_RED, trigger_referrer, tl, tc, ANSI_RESET);
    if (compile_entry_diag && (!trigger_referrer || strcmp(compile_entry_diag, trigger_referrer) != 0))
        fprintf(stderr, "%s  Compilacion iniciada desde: '%s'%s\n", ANSI_RED, compile_entry_diag, ANSI_RESET);
    fprintf(stderr, "%s  Se intenta cargar de nuevo '%s', que ya esta en la pila de modulos en curso.%s\n",
        ANSI_RED, trigger_target_display ? trigger_target_display : duplicate_canon, ANSI_RESET);

    if (first == (size_t)-1 || !stack->n) {
        fprintf(stderr, "%s  (Cadena de archivos no disponible; modulo repetido: '%s'.)%s\n",
            ANSI_RED, duplicate_canon, ANSI_RESET);
        return;
    }

    fprintf(stderr, "%s  Cadena de `usar` desde la primera aparicion del modulo repetido (modulo <- archivo del `usar`, linea:columna):%s\n",
        ANSI_RED, ANSI_RESET);
    size_t step = 1;
    for (size_t j = first; j < stack->n; j++) {
        const UsarStackFrame *fr = &stack->frames[j];
        int fl = fr->usar_line >= 1 ? fr->usar_line : 1;
        int fc = fr->usar_col >= 1 ? fr->usar_col : 1;
        fprintf(stderr, "%s    %zu) modulo '%s' <- '%s', linea %d, columna %d%s\n",
            ANSI_RED, step++, fr->module_canon ? fr->module_canon : "?",
            fr->referrer_path ? fr->referrer_path : "?", fl, fc, ANSI_RESET);
    }
    fprintf(stderr, "%s    %zu) reintento: modulo '%s' <- '%s', linea %d, columna %d  (cierra el ciclo)%s\n",
        ANSI_RED, step, duplicate_canon, trigger_referrer, tl, tc, ANSI_RESET);
}

static void usar_loaded_free(UsarLoadedSet *s) {
    if (!s) return;
    for (size_t i = 0; i < s->n; i++) free(s->paths[i]);
    free(s->paths);
    s->paths = NULL;
    s->n = s->cap = 0;
}

static int usar_loaded_contains(const UsarLoadedSet *s, const char *canon) {
    if (!s || !canon) return 0;
    for (size_t i = 0; i < s->n; i++)
        if (strcmp(s->paths[i], canon) == 0) return 1;
    return 0;
}

static int usar_loaded_add(UsarLoadedSet *s, const char *canon) {
    if (!s || !canon || usar_loaded_contains(s, canon)) return 0;
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 8;
        char **np = realloc(s->paths, nc * sizeof(char *));
        if (!np) return -1;
        s->paths = np;
        s->cap = nc;
    }
    char *copy = strdup(canon);
    if (!copy) return -1;
    s->paths[s->n++] = copy;
    return 0;
}

static char *usar_canonical_path(const char *full_open_path) {
    char buf[4096];
#ifdef _WIN32
    if (_fullpath(buf, full_open_path, sizeof(buf)))
        return strdup(buf);
#else
    char *r = realpath(full_open_path, NULL);
    if (r) return r;
#endif
    return strdup(full_open_path);
}

static void usar_module_dirname(const char *file_path, char *base_dir, size_t base_sz) {
    if (!file_path || !base_dir || base_sz < 2) return;
    const char *last_sep = strrchr(file_path, '/');
    if (!last_sep) last_sep = strrchr(file_path, PATH_SEP);
    if (last_sep && last_sep > file_path) {
        size_t len = (size_t)(last_sep - file_path);
        if (len >= base_sz) len = base_sz - 1;
        memcpy(base_dir, file_path, len);
        base_dir[len] = '\0';
    } else {
        base_dir[0] = '.'; base_dir[1] = '\0';
    }
}

static void register_usar_fallback_regex(CodeGen *cg, char *mbuf) {
    char *p = mbuf;
    while ((p = strstr(p, "funcion ")) != NULL) {
        p += 8;
        char *paren = strchr(p, '(');
        if (!paren) break;
        char name_buf[128];
        size_t nlen = (size_t)(paren - p);
        if (nlen >= sizeof name_buf) { p = paren; continue; }
        memcpy(name_buf, p, nlen);
        name_buf[nlen] = '\0';
        char *ret = strstr(paren, "retorna ");
        if (!ret) { p = paren; continue; }
        ret += 8;
        char type_buf[32];
        size_t j = 0;
        while (ret[j] && ret[j] != ' ' && ret[j] != '\n' && ret[j] != '\r' && ret[j] != '\t' && j < sizeof type_buf - 1)
            type_buf[j] = ret[j], j++;
        type_buf[j] = '\0';
        if (j > 0)
            codegen_register_external_func(cg, name_buf, type_buf);
        p = ret;
    }
}

static int should_merge_function(const FunctionNode *f, const ProgramNode *mp, const ActivarModuloNode *spec) {
    (void)mp;
    if (!f || !spec) return 0;
    if (!f->is_exported) return 0;
    if (spec->import_kind == USAR_IMPORT_TODO) return 1;
    if (spec->import_kind == USAR_IMPORT_NAMES) {
        for (size_t i = 0; i < spec->n_import_names; i++)
            if (spec->import_names[i] && f->name && strcmp(spec->import_names[i], f->name) == 0) return 1;
    }
    return 0;
}

static int should_merge_global_var(const VarDeclNode *vd, const ProgramNode *mp, const ActivarModuloNode *spec) {
    (void)mp;
    if (!vd || !spec) return 0;
    if (!vd->is_exported) return 0;
    if (spec->import_kind == USAR_IMPORT_TODO) return 1;
    if (spec->import_kind == USAR_IMPORT_NAMES) {
        for (size_t i = 0; i < spec->n_import_names; i++)
            if (spec->import_names[i] && vd->name && strcmp(spec->import_names[i], vd->name) == 0) return 1;
    }
    return 0;
}

/* Valida `usar { ... }` contra el modulo parseado.
 * import_site_* = donde esta el `usar` en el archivo que importa.
 * module_source_path = ruta del .jasb del modulo cargado (mp), se muestra canonica si es posible. */
static int validate_named_imports_for_module(const ProgramNode *mp, const ActivarModuloNode *spec,
    const char *import_site_path, int import_line, int import_col,
    const char *module_source_path) {
    if (!spec || spec->import_kind != USAR_IMPORT_NAMES) return 0;
    int il = import_line >= 1 ? import_line : 1;
    int ic = import_col >= 1 ? import_col : 1;
    char *mod_canon = (module_source_path && module_source_path[0]) ? usar_canonical_path(module_source_path) : NULL;
    const char *mod_show = (mod_canon && mod_canon[0]) ? mod_canon
                          : (module_source_path && module_source_path[0] ? module_source_path : "(modulo sin ruta)");

    if (spec->n_import_names == 0) {
        fprintf(stderr, "%s%s, linea %d, columna %d: error: `usar { ... }` requiere al menos un nombre.%s\n",
            ANSI_RED, import_site_path, il, ic, ANSI_RESET);
        free(mod_canon);
        return 1;
    }
    for (size_t i = 0; i < spec->n_import_names; i++) {
        const char *want = spec->import_names[i];
        FunctionNode *ff = NULL;
        VarDeclNode *vv = NULL;
        for (size_t j = 0; j < mp->n_funcs; j++) {
            FunctionNode *cf = (FunctionNode *)mp->functions[j];
            if (cf && cf->name && strcmp(cf->name, want) == 0) {
                ff = cf;
                break;
            }
        }
        for (size_t j = 0; j < mp->n_globals; j++) {
            ASTNode *g = mp->globals[j];
            if (g && g->type == NODE_VAR_DECL) {
                VarDeclNode *vvd = (VarDeclNode *)g;
                if (vvd->name && strcmp(vvd->name, want) == 0) {
                    vv = vvd;
                    break;
                }
            }
        }
        if (!ff && !vv) {
            fprintf(stderr, "%s%s, linea %d, columna %d: error: `usar { ... }`: no se hallo `%s` entre las funciones y globales exportados (`enviar`) del modulo '%s'.%s\n",
                ANSI_RED, import_site_path, il, ic, want, mod_show, ANSI_RESET);
            fprintf(stderr, "%s  Nota: si en el archivo del modulo \"deberia\" estar `%s`, suele deberse a que esa declaracion no llego al analizador (error de sintaxis, palabra reservada como nombre de parametro o variable, macro mal cerrada, etc.). Corrija primero el modulo; el nombre en `usar { ... }` debe coincidir exactamente con un simbolo `enviar` valido.%s\n",
                ANSI_RED, want, ANSI_RESET);
            free(mod_canon);
            return 1;
        }
        if (ff && vv) {
            fprintf(stderr, "%s%s, linea %d, columna %d: error: en el modulo '%s' el nombre `%s` esta duplicado como funcion y como variable global.%s\n",
                ANSI_RED, import_site_path, il, ic, mod_show, want, ANSI_RESET);
            free(mod_canon);
            return 1;
        }
        if (ff && !ff->is_exported) {
            int dl = ff->base.line >= 1 ? ff->base.line : 1;
            int dc = ff->base.col >= 1 ? ff->base.col : 1;
            fprintf(stderr, "%s%s, linea %d, columna %d: error: `usar { ... }`: no se puede importar la funcion `%s` porque en el modulo no lleva `enviar`.%s\n",
                ANSI_RED, import_site_path, il, ic, want, ANSI_RESET);
            fprintf(stderr, "%s  Declaracion en el modulo: archivo '%s', linea %d, columna %d (anadir `enviar` antes de `funcion`).%s\n",
                ANSI_RED, mod_show, dl, dc, ANSI_RESET);
            free(mod_canon);
            return 1;
        }
        if (vv && !vv->is_exported) {
            int dl = vv->base.line >= 1 ? vv->base.line : 1;
            int dc = vv->base.col >= 1 ? vv->base.col : 1;
            fprintf(stderr, "%s%s, linea %d, columna %d: error: `usar { ... }`: no se puede importar `%s` porque en el modulo no esta enviada con `enviar`.%s\n",
                ANSI_RED, import_site_path, il, ic, want, ANSI_RESET);
            fprintf(stderr, "%s  Declaracion en el modulo: archivo '%s', linea %d, columna %d (anadir `enviar` antes del tipo en la declaracion global).%s\n",
                ANSI_RED, mod_show, dl, dc, ANSI_RESET);
            free(mod_canon);
            return 1;
        }
    }
    free(mod_canon);
    return 0;
}

/* 0 = ok; 1 = error (falta archivo o ciclo). */
static int process_usar_module_recursive(ProgramNode *main_p, const char *full_open_path,
    const char *referrer_diag_path, int usar_line, int usar_col,
    CodeGen *cg, UsarPathStack *stack, UsarLoadedSet *loaded,
    const ActivarModuloNode *import_spec, const char *compile_entry_diag) {
    if (!main_p || !full_open_path || !referrer_diag_path || !cg || !stack || !loaded) return 1;

    char *canon = usar_canonical_path(full_open_path);
    if (!canon) return 1;

    if (usar_loaded_contains(loaded, canon)) {
        free(canon);
        return 0;
    }
    if (usar_stack_contains(stack, canon)) {
        usar_emit_cycle_error(stack, canon, referrer_diag_path, usar_line, usar_col, full_open_path, compile_entry_diag);
        free(canon);
        return 1;
    }
    if (usar_stack_push(stack, canon, referrer_diag_path, usar_line, usar_col) != 0) {
        free(canon);
        return 1;
    }
    free(canon);

    FILE *f = fopen(full_open_path, "rb");
    if (!f) {
        fprintf(stderr, "%s%s, linea %d, columna %d: error: `usar`: no se encuentra el modulo '%s'%s\n",
            ANSI_RED, referrer_diag_path, usar_line >= 1 ? usar_line : 1, usar_col >= 1 ? usar_col : 1, full_open_path, ANSI_RESET);
        usar_stack_pop(stack);
        return 1;
    }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
    char *mbuf = malloc((size_t)sz + 1);
    if (!mbuf) { fclose(f); usar_stack_pop(stack); return 1; }
    size_t nr = fread(mbuf, 1, (size_t)sz, f);
        fclose(f);
        mbuf[nr] = '\0';

        Lexer mlex;
        lexer_init(&mlex, mbuf);
        TokenVec mtvec;
        token_vec_init(&mtvec);
        Token mtok;
        while (lexer_next(&mlex, &mtok) == 0) {
            token_vec_push(&mtvec, &mtok);
            token_free_value(&mtok);
            if (mtok.type == TOK_EOF) break;
        }
        lexer_free(&mlex);
        Parser mpar;
    parser_init(&mpar, &mtvec, full_open_path, mbuf);
        ASTNode *mast = parser_parse(&mpar);
        token_vec_free(&mtvec);

        if (!mast || mpar.last_error) {
            if (mast) ast_free(mast);
            fprintf(stderr, "%s%s: error: no se pudo analizar el modulo `%s` (no se fusionara con el programa).%s\n",
                ANSI_RED, referrer_diag_path, full_open_path, ANSI_RESET);
            if (mpar.last_error && mpar.last_error[0]) {
                fprintf(stderr, "%s%s%s", ANSI_RED, mpar.last_error, ANSI_RESET);
                size_t L = strlen(mpar.last_error);
                if (L == 0 || mpar.last_error[L - 1] != '\n')
                    fputc('\n', stderr);
            } else if (!mast) {
                fprintf(stderr, "%s  (AST nulo tras parsear el modulo)%s\n", ANSI_RED, ANSI_RESET);
            }
            parser_free(&mpar);
            register_usar_fallback_regex(cg, mbuf);
            free(mbuf);
            usar_stack_pop(stack);
            return 1;
        }
    parser_free(&mpar);

    if (mast->type != NODE_PROGRAM) {
        fprintf(stderr, "%s%s: error: el modulo `%s` no produjo un programa valido.%s\n",
            ANSI_RED, referrer_diag_path, full_open_path, ANSI_RESET);
        ast_free(mast);
        free(mbuf);
        usar_stack_pop(stack);
        return 1;
    }

    ProgramNode *mp = (ProgramNode *)mast;
    char child_base[1024];
    usar_module_dirname(full_open_path, child_base, sizeof child_base);

    for (size_t gi = 0; gi < mp->n_globals; gi++) {
        ASTNode *gn = mp->globals[gi];
        if (!gn || gn->type != NODE_ACTIVAR_MODULO) continue;
        ActivarModuloNode *an = (ActivarModuloNode *)gn;
        if (!an->module_path || an->module_path->type != NODE_LITERAL) continue;
        LiteralNode *ln = (LiteralNode *)an->module_path;
        if (!ln->type_name || strcmp(ln->type_name, "texto") != 0) continue;
        const char *rel = ln->value.str;
        if (!rel || !rel[0]) continue;
        char child_full[2048];
        snprintf(child_full, sizeof(child_full), "%s%c%s", child_base, PATH_SEP, rel);
        for (char *c = child_full; *c; c++) if (*c == '/') *c = PATH_SEP;
        int sub_err = process_usar_module_recursive(main_p, child_full, full_open_path,
            an->base.line, an->base.col, cg, stack, loaded, an, compile_entry_diag);
        if (sub_err) {
            ast_free(mast);
            free(mbuf);
            usar_stack_pop(stack);
            return 1;
        }
    }

    if (import_spec && import_spec->import_kind == USAR_IMPORT_NAMES) {
        if (validate_named_imports_for_module(mp, import_spec, referrer_diag_path, usar_line, usar_col, full_open_path)) {
            ast_free(mast);
            free(mbuf);
            usar_stack_pop(stack);
            return 1;
        }
    }

        if (mp->n_funcs > 0) {
        size_t add = 0;
        for (size_t i = 0; i < mp->n_funcs; i++) {
            FunctionNode *f = (FunctionNode *)mp->functions[i];
            if (f && should_merge_function(f, mp, import_spec)) add++;
        }
        if (add > 0) {
            size_t old_n = main_p->n_funcs;
            ASTNode **new_funcs = realloc(main_p->functions, (old_n + add) * sizeof(ASTNode *));
            if (!new_funcs) {
                ast_free(mast);
                free(mbuf);
                usar_stack_pop(stack);
                return 1;
            }
            main_p->functions = new_funcs;
            size_t w = old_n;
            for (size_t i = 0; i < mp->n_funcs; i++) {
                FunctionNode *f = (FunctionNode *)mp->functions[i];
                if (!f || !should_merge_function(f, mp, import_spec)) continue;
                main_p->functions[w++] = (ASTNode *)f;
                mp->functions[i] = NULL;
            }
            main_p->n_funcs = w;
        }
    }

    for (size_t gi = 0; gi < mp->n_globals; gi++) {
        ASTNode *g = mp->globals[gi];
        if (!g || g->type != NODE_VAR_DECL) continue;
        VarDeclNode *vd = (VarDeclNode *)g;
        if (!should_merge_global_var(vd, mp, import_spec)) continue;
        size_t new_n = main_p->n_globals + 1;
        ASTNode **ng = realloc(main_p->globals, new_n * sizeof(ASTNode *));
        if (!ng) {
        ast_free(mast);
        free(mbuf);
            usar_stack_pop(stack);
            return 1;
        }
        main_p->globals = ng;
        main_p->globals[new_n - 1] = g;
        main_p->n_globals = new_n;
        mp->globals[gi] = NULL;
    }

    char *canon_mark = usar_canonical_path(full_open_path);
    if (canon_mark && usar_loaded_add(loaded, canon_mark) != 0) {
        free(canon_mark);
        ast_free(mast);
        free(mbuf);
        usar_stack_pop(stack);
        return 1;
    }
    free(canon_mark);

    ast_free(mast);
    free(mbuf);
    usar_stack_pop(stack);
    return 0;
}

static int register_usar_modules(ProgramNode *p, const char *in_path, const char *diag_path, CodeGen *cg) {
    if (!p || !in_path || !diag_path || !cg) return 0;
    char base_dir[1024];
    char abs_in[2048] = {0};
    const char *path_for_base = in_path;
#ifdef _WIN32
    if (_fullpath(abs_in, in_path, sizeof(abs_in))) path_for_base = abs_in;
#else
    {
        char *r = realpath(in_path, NULL);
        if (r) {
            strncpy(abs_in, r, sizeof(abs_in) - 1);
            abs_in[sizeof(abs_in) - 1] = '\0';
            free(r);
            path_for_base = abs_in;
        }
    }
#endif
    usar_module_dirname(path_for_base, base_dir, sizeof base_dir);

    UsarPathStack stack = {0};
    UsarLoadedSet loaded = {0};
    int errs = 0;

    for (size_t i = 0; i < p->n_globals; i++) {
        ASTNode *n = p->globals[i];
        if (!n || n->type != NODE_ACTIVAR_MODULO) continue;
        ActivarModuloNode *an = (ActivarModuloNode *)n;
        if (!an->module_path || an->module_path->type != NODE_LITERAL) continue;
        LiteralNode *ln = (LiteralNode *)an->module_path;
        if (!ln->type_name || strcmp(ln->type_name, "texto") != 0) continue;
        const char *rel = ln->value.str;
        if (!rel || !rel[0]) continue;
        char full[2048];
        snprintf(full, sizeof(full), "%s%c%s", base_dir, PATH_SEP, rel);
        for (char *c = full; *c; c++) if (*c == '/') *c = PATH_SEP;
        int one = process_usar_module_recursive(p, full, diag_path, an->base.line, an->base.col, cg, &stack, &loaded, an, diag_path);
        if (one) errs++;
    }

    usar_stack_free(&stack);
    usar_loaded_free(&loaded);
    return errs;
}

/* Aviso si no hay codigo (archivo vacio, solo espacios o solo comentarios). */
static void warn_if_empty_program(const char *in_path, ASTNode *ast) {
    if (!in_path || !ast || ast->type != NODE_PROGRAM) return;
    ProgramNode *prog = (ProgramNode *)ast;
    BlockNode *mb = (prog->main_block && prog->main_block->type == NODE_BLOCK)
                        ? (BlockNode *)prog->main_block
                        : NULL;
    size_t n_main = mb ? mb->n : 0;
    if (prog->n_funcs == 0 && prog->n_globals == 0 && n_main == 0)
        fprintf(stderr, "%s: aviso: programa vacio (sin sentencias, funciones ni declaraciones globales).\n",
                in_path);
}

typedef struct {
    char *name;
    int line;
    int col;
    int depth;
    int used;
} UnusedDecl;

typedef struct {
    UnusedDecl *arr;
    size_t n;
    size_t cap;
} UnusedDeclVec;

/* Nombres que aparecen como objetivo de NODE_CALL en todo el programa (tras fusionar modulos). */
typedef struct {
    char **names;
    size_t n;
    size_t cap;
} CalleeVec;

static void callee_vec_free(CalleeVec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->n; i++) free(v->names[i]);
    free(v->names);
    v->names = NULL;
    v->n = v->cap = 0;
}

static void callee_vec_add(CalleeVec *v, const char *name) {
    if (!name || !name[0]) return;
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->names[i], name) == 0) return;
    if (v->n >= v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 16;
        char **p = realloc(v->names, nc * sizeof(char *));
        if (!p) return;
        v->names = p;
        v->cap = nc;
    }
    char *cp = strdup(name);
    if (!cp) return;
    v->names[v->n++] = cp;
}

static int callee_vec_has(const CalleeVec *v, const char *name) {
    if (!v || !name) return 0;
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->names[i], name) == 0) return 1;
    return 0;
}

static void callees_collect_expr(ASTNode *node, CalleeVec *v);
static void callees_collect_stmt(ASTNode *node, CalleeVec *v);
static void callees_collect_block(ASTNode *node, CalleeVec *v);

static void callees_collect_expr(ASTNode *node, CalleeVec *v) {
    if (!node || !v) return;
    switch (node->type) {
        case NODE_LITERAL: {
            LiteralNode *ln = (LiteralNode *)node;
            if (ln->type_name && strcmp(ln->type_name, "texto") == 0 && ln->value.str) {
                const char *p = ln->value.str;
                while (*p) {
                    const char *dollar = strstr(p, "${");
                    if (!dollar) break;
                    const char *end_brace = strchr(dollar + 2, '}');
                    if (!end_brace) break;
                    const char *start = dollar + 2;
                    const char *end = end_brace;
                    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
                        start++;
                    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
                        end--;
                    size_t len = (size_t)(end - start);
                    if (len > 0) {
                        char *expr_src = malloc(len + 1);
                        if (expr_src) {
                            memcpy(expr_src, start, len);
                            expr_src[len] = '\0';
                            char *parse_err = NULL;
                            ASTNode *sub = parser_parse_expression_from_string(expr_src, &parse_err);
                            if (parse_err) free(parse_err);
                            if (sub) {
                                callees_collect_expr(sub, v);
                                ast_free(sub);
                            }
                            free(expr_src);
                        }
                    }
                    p = end_brace + 1;
                }
            }
            break;
        }
        case NODE_BINARY_OP:
            callees_collect_expr(((BinaryOpNode *)node)->left, v);
            callees_collect_expr(((BinaryOpNode *)node)->right, v);
            break;
        case NODE_UNARY_OP:
            callees_collect_expr(((UnaryOpNode *)node)->expression, v);
            break;
        case NODE_TERNARY:
            callees_collect_expr(((TernaryNode *)node)->condition, v);
            callees_collect_expr(((TernaryNode *)node)->true_expr, v);
            callees_collect_expr(((TernaryNode *)node)->false_expr, v);
            break;
        case NODE_CALL: {
            CallNode *cn = (CallNode *)node;
            if (cn->name) callee_vec_add(v, cn->name);
            if (cn->callee) callees_collect_expr(cn->callee, v);
            for (size_t i = 0; i < cn->n_args; i++) callees_collect_expr(cn->args[i], v);
            break;
        }
        case NODE_ASSIGNMENT:
            callees_collect_expr(((AssignmentNode *)node)->target, v);
            callees_collect_expr(((AssignmentNode *)node)->expression, v);
            break;
        case NODE_INDEX_ACCESS:
            callees_collect_expr(((IndexAccessNode *)node)->target, v);
            callees_collect_expr(((IndexAccessNode *)node)->index, v);
            break;
        case NODE_INDEX_ASSIGNMENT:
            callees_collect_expr(((IndexAssignmentNode *)node)->target, v);
            callees_collect_expr(((IndexAssignmentNode *)node)->index, v);
            callees_collect_expr(((IndexAssignmentNode *)node)->expression, v);
            break;
        case NODE_MEMBER_ACCESS:
            callees_collect_expr(((MemberAccessNode *)node)->target, v);
            break;
        case NODE_POSTFIX_UPDATE:
            callees_collect_expr(((PostfixUpdateNode *)node)->target, v);
            break;
        case NODE_THROW:
            callees_collect_expr(((ThrowNode *)node)->expression, v);
            break;
        case NODE_LAMBDA_DECL: {
            LambdaDeclNode *ld = (LambdaDeclNode *)node;
            if (ld->body) callees_collect_expr(ld->body, v);
            break;
        }
        case NODE_IDENTIFIER: {
            IdentifierNode *id = (IdentifierNode *)node;
            if (id->name) callee_vec_add(v, id->name);
            break;
        }
        default:
            break;
    }
}

static void callees_collect_block(ASTNode *node, CalleeVec *v) {
    if (!node || !v) return;
    if (node->type == NODE_BLOCK) {
        BlockNode *b = (BlockNode *)node;
        for (size_t i = 0; i < b->n; i++) callees_collect_stmt(b->statements[i], v);
    } else
        callees_collect_stmt(node, v);
}

static void callees_collect_stmt(ASTNode *node, CalleeVec *v) {
    if (!node || !v) return;
    switch (node->type) {
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode *)node;
            if (vd->value) callees_collect_expr(vd->value, v);
            break;
        }
        case NODE_PRINT:
            callees_collect_expr(((PrintNode *)node)->expression, v);
            break;
        case NODE_ASSIGNMENT:
            callees_collect_expr(((AssignmentNode *)node)->target, v);
            callees_collect_expr(((AssignmentNode *)node)->expression, v);
            break;
        case NODE_RETURN:
            callees_collect_expr(((ReturnNode *)node)->expression, v);
            break;
        case NODE_BLOCK:
            callees_collect_block(node, v);
            break;
        case NODE_IF: {
            IfNode *in = (IfNode *)node;
            callees_collect_expr(in->condition, v);
            callees_collect_block(in->body, v);
            callees_collect_block(in->else_body, v);
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode *)node;
            callees_collect_expr(wn->condition, v);
            callees_collect_block(wn->body, v);
            break;
        }
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode *)node;
            if (fe->collection) callees_collect_expr(fe->collection, v);
            callees_collect_block(fe->body, v);
            break;
        }
        case NODE_DO_WHILE: {
            DoWhileNode *dn = (DoWhileNode *)node;
            callees_collect_block(dn->body, v);
            callees_collect_expr(dn->condition, v);
            break;
        }
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode *)node;
            callees_collect_expr(sn->selector, v);
            for (size_t i = 0; i < sn->n_cases; i++) {
                for (size_t j = 0; j < sn->cases[i].n_values; j++)
                    callees_collect_expr(sn->cases[i].values[j], v);
                callees_collect_block(sn->cases[i].body, v);
            }
            callees_collect_block(sn->default_body, v);
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode *)node;
            callees_collect_block(tn->try_body, v);
            callees_collect_block(tn->catch_body, v);
            callees_collect_block(tn->final_body, v);
            break;
        }
        default:
            callees_collect_expr(node, v);
            break;
    }
}

/* Funciones definidas en el programa que no tienen `enviar` y nunca son objetivo de una llamada. */
static int warn_unused_functions(const char *in_path, const char *source_text, ASTNode *ast) {
    if (!in_path || !ast || ast->type != NODE_PROGRAM) return 0;
    ProgramNode *p = (ProgramNode *)ast;
    CalleeVec callees = {0};

    for (size_t i = 0; i < p->n_globals; i++)
        callees_collect_stmt(p->globals[i], &callees);

    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *fn = (FunctionNode *)p->functions[i];
        if (!fn) continue;
        callees_collect_block(fn->body, &callees);
    }

    callees_collect_block(p->main_block, &callees);

    int err_count = 0;
    extern int werror_unused;
    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *fn = (FunctionNode *)p->functions[i];
        if (!fn || !fn->name) continue;
        if (fn->is_exported) continue;
        if (callee_vec_has(&callees, fn->name)) continue;

        int line = fn->base.line > 0 ? fn->base.line : 1;
        int col = fn->base.col > 0 ? fn->base.col : 1;
        char head[2048];
        if (werror_unused) {
            snprintf(head, sizeof head,
                     "Archivo %s, linea %d, columna %d: error semantico: funcion `%s` definida pero no usada ni enviada con `enviar`.",
                     in_path, line, col, fn->name);
            err_count++;
        } else {
            snprintf(head, sizeof head,
                     "Archivo %s, linea %d, columna %d: aviso: funcion `%s` definida pero no usada ni enviada con `enviar`.",
                     in_path, line, col, fn->name);
        }
        if (source_text) {
            char *full = diag_attach_snippet(source_text, line, col, head);
            if (full) {
                fprintf(stderr, "%s%s%s", werror_unused ? ANSI_RED : ANSI_YELLOW, full, ANSI_RESET);
                if (full[0] && full[strlen(full) - 1] != '\n') fputc('\n', stderr);
                free(full);
            } else
                fprintf(stderr, "%s%s%s\n", werror_unused ? ANSI_RED : ANSI_YELLOW, head, ANSI_RESET);
        } else
            fprintf(stderr, "%s%s%s\n", werror_unused ? ANSI_RED : ANSI_YELLOW, head, ANSI_RESET);
    }

    callee_vec_free(&callees);
    return err_count;
}

static void unused_vec_push(UnusedDeclVec *v, const char *name, int line, int col, int depth) {
    if (!v || !name) return;
    if (v->n == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 32;
        UnusedDecl *p = (UnusedDecl*)realloc(v->arr, nc * sizeof(UnusedDecl));
        if (!p) return;
        v->arr = p;
        v->cap = nc;
    }
    v->arr[v->n].name = strdup(name);
    v->arr[v->n].line = line;
    v->arr[v->n].col = col;
    v->arr[v->n].depth = depth;
    v->arr[v->n].used = 0;
    v->n++;
}

static void unused_vec_mark_used(UnusedDeclVec *v, const char *name, int current_depth) {
    if (!v || !name) return;
    for (size_t i = v->n; i > 0; i--) {
        UnusedDecl *d = &v->arr[i - 1];
        if (d->depth <= current_depth && d->name && strcmp(d->name, name) == 0) {
            d->used = 1;
            return;
        }
    }
}

static void unused_scan_expr(ASTNode *node, UnusedDeclVec *decls, int depth);
static void unused_scan_stmt(ASTNode *node, UnusedDeclVec *decls, int *depth);
static void unused_scan_block(ASTNode *node, UnusedDeclVec *decls, int *depth);

static void unused_scan_expr(ASTNode *node, UnusedDeclVec *decls, int depth) {
    if (!node) return;
    switch (node->type) {
        case NODE_IDENTIFIER:
            unused_vec_mark_used(decls, ((IdentifierNode*)node)->name, depth);
            break;
        case NODE_LITERAL: {
            LiteralNode *ln = (LiteralNode*)node;
            if (ln->type_name && strcmp(ln->type_name, "texto") == 0 && ln->value.str) {
                const char *p = ln->value.str;
                while (*p) {
                    const char *dollar = strstr(p, "${");
                    if (!dollar) break;
                    const char *end_brace = strchr(dollar + 2, '}');
                    if (!end_brace) break;
                    const char *start = dollar + 2;
                    const char *end = end_brace;
                    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
                        start++;
                    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
                        end--;
                    size_t len = (size_t)(end - start);
                    if (len > 0) {
                        char *expr_src = malloc(len + 1);
                        if (expr_src) {
                            memcpy(expr_src, start, len);
                            expr_src[len] = '\0';
                            char *parse_err = NULL;
                            ASTNode *sub = parser_parse_expression_from_string(expr_src, &parse_err);
                            if (parse_err) free(parse_err);
                            if (sub) {
                                unused_scan_expr(sub, decls, depth);
                                ast_free(sub);
                            }
                            free(expr_src);
                        }
                    }
                    p = end_brace + 1;
                }
            }
            break;
        }
        case NODE_BINARY_OP:
            unused_scan_expr(((BinaryOpNode*)node)->left, decls, depth);
            unused_scan_expr(((BinaryOpNode*)node)->right, decls, depth);
            break;
        case NODE_UNARY_OP:
            unused_scan_expr(((UnaryOpNode*)node)->expression, decls, depth);
            break;
        case NODE_TERNARY:
            unused_scan_expr(((TernaryNode*)node)->condition, decls, depth);
            unused_scan_expr(((TernaryNode*)node)->true_expr, decls, depth);
            unused_scan_expr(((TernaryNode*)node)->false_expr, decls, depth);
            break;
        case NODE_CALL: {
            CallNode *cn = (CallNode*)node;
            if (cn->name) unused_vec_mark_used(decls, cn->name, depth);
            if (cn->callee) unused_scan_expr(cn->callee, decls, depth);
            for (size_t i = 0; i < cn->n_args; i++) unused_scan_expr(cn->args[i], decls, depth);
            break;
        }
        case NODE_ASSIGNMENT:
            unused_scan_expr(((AssignmentNode*)node)->target, decls, depth);
            unused_scan_expr(((AssignmentNode*)node)->expression, decls, depth);
            break;
        case NODE_INDEX_ACCESS:
            unused_scan_expr(((IndexAccessNode*)node)->target, decls, depth);
            unused_scan_expr(((IndexAccessNode*)node)->index, decls, depth);
            break;
        case NODE_INDEX_ASSIGNMENT:
            unused_scan_expr(((IndexAssignmentNode*)node)->target, decls, depth);
            unused_scan_expr(((IndexAssignmentNode*)node)->index, decls, depth);
            unused_scan_expr(((IndexAssignmentNode*)node)->expression, decls, depth);
            break;
        case NODE_MEMBER_ACCESS:
            unused_scan_expr(((MemberAccessNode*)node)->target, decls, depth);
            break;
        case NODE_POSTFIX_UPDATE:
            unused_scan_expr(((PostfixUpdateNode*)node)->target, decls, depth);
            break;
        case NODE_THROW:
            unused_scan_expr(((ThrowNode*)node)->expression, decls, depth);
            break;
        case NODE_LAMBDA_DECL: {
            LambdaDeclNode *ld = (LambdaDeclNode*)node;
            if (ld->body) unused_scan_expr(ld->body, decls, depth);
            break;
        }
        default:
            break;
    }
}

static void unused_scan_block(ASTNode *node, UnusedDeclVec *decls, int *depth) {
    if (!node) return;
    if (node->type == NODE_BLOCK) {
        BlockNode *b = (BlockNode*)node;
        (*depth)++;
        for (size_t i = 0; i < b->n; i++) unused_scan_stmt(b->statements[i], decls, depth);
        (*depth)--;
    } else {
        unused_scan_stmt(node, decls, depth);
    }
}

static void unused_scan_stmt(ASTNode *node, UnusedDeclVec *decls, int *depth) {
    if (!node) return;
    switch (node->type) {
        case NODE_VAR_DECL: {
            VarDeclNode *vd = (VarDeclNode*)node;
            if (vd->name) unused_vec_push(decls, vd->name, node->line, node->col, *depth);
            if (vd->value) unused_scan_expr(vd->value, decls, *depth);
            break;
        }
        case NODE_PRINT:
            unused_scan_expr(((PrintNode*)node)->expression, decls, *depth);
            break;
        case NODE_ASSIGNMENT:
            unused_scan_expr(((AssignmentNode*)node)->target, decls, *depth);
            unused_scan_expr(((AssignmentNode*)node)->expression, decls, *depth);
            break;
        case NODE_RETURN:
            unused_scan_expr(((ReturnNode*)node)->expression, decls, *depth);
            break;
        case NODE_INPUT: {
            InputNode *in = (InputNode*)node;
            if (in->variable) unused_vec_push(decls, in->variable, node->line, node->col, *depth);
            break;
        }
        case NODE_IF: {
            IfNode *in = (IfNode*)node;
            unused_scan_expr(in->condition, decls, *depth);
            unused_scan_block(in->body, decls, depth);
            unused_scan_block(in->else_body, decls, depth);
            break;
        }
        case NODE_WHILE: {
            WhileNode *wn = (WhileNode*)node;
            unused_scan_expr(wn->condition, decls, *depth);
            unused_scan_block(wn->body, decls, depth);
            break;
        }
        case NODE_FOREACH: {
            ForEachNode *fe = (ForEachNode *)node;
            if (fe->collection) unused_scan_expr(fe->collection, decls, *depth);
            (*depth)++;
            if (fe->iter_name)
                unused_vec_push(decls, fe->iter_name, fe->base.line, fe->base.col, *depth);
            unused_scan_block(fe->body, decls, depth);
            (*depth)--;
            break;
        }
        case NODE_DO_WHILE: {
            DoWhileNode *dn = (DoWhileNode*)node;
            unused_scan_block(dn->body, decls, depth);
            unused_scan_expr(dn->condition, decls, *depth);
            break;
        }
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode*)node;
            unused_scan_expr(sn->selector, decls, *depth);
            for (size_t i = 0; i < sn->n_cases; i++) {
                for (size_t j = 0; j < sn->cases[i].n_values; j++)
                    unused_scan_expr(sn->cases[i].values[j], decls, *depth);
                unused_scan_block(sn->cases[i].body, decls, depth);
            }
            unused_scan_block(sn->default_body, decls, depth);
            break;
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode*)node;
            unused_scan_block(tn->try_body, decls, depth);
            if (tn->catch_body) {
                (*depth)++;
                if (tn->catch_var) unused_vec_push(decls, tn->catch_var, node->line, node->col, *depth);
                unused_scan_block(tn->catch_body, decls, depth);
                (*depth)--;
            }
            unused_scan_block(tn->final_body, decls, depth);
            break;
        }
        default:
            unused_scan_expr(node, decls, *depth);
            break;
    }
}

static int warn_unused_variables(const char *in_path, const char *source_text, ASTNode *ast) {
    if (!in_path || !ast || ast->type != NODE_PROGRAM) return 0;
    ProgramNode *p = (ProgramNode*)ast;
    UnusedDeclVec decls = {0};
    int depth = 1;

    for (size_t i = 0; i < p->n_globals; i++)
        unused_scan_stmt(p->globals[i], &decls, &depth);

    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *fn = (FunctionNode*)p->functions[i];
        if (!fn) continue;
        depth = 2;
        for (size_t j = 0; j < fn->n_params; j++) {
            VarDeclNode *vd = (VarDeclNode*)fn->params[j];
            if (vd && vd->name) unused_vec_push(&decls, vd->name, vd->base.line, vd->base.col, depth);
        }
        unused_scan_block(fn->body, &decls, &depth);
    }

    depth = 1;
    unused_scan_block(p->main_block, &decls, &depth);

    int err_count = 0;
    extern int werror_unused;
    for (size_t i = 0; i < decls.n; i++) {
        UnusedDecl *d = &decls.arr[i];
        if (!d->used && d->name && d->line > 0 && d->col > 0) {
            char head[2048];
            if (werror_unused) {
                snprintf(head, sizeof head,
                         "Archivo %s, linea %d, columna %d: error semantico: variable `%s` declarada pero no usada.",
                         in_path, d->line, d->col, d->name);
                err_count++;
            } else {
                snprintf(head, sizeof head,
                         "Archivo %s, linea %d, columna %d: aviso: variable `%s` declarada pero no usada.",
                         in_path, d->line, d->col, d->name);
            }
            if (source_text) {
                char *full = diag_attach_snippet(source_text, d->line, d->col, head);
                if (full) {
                    fprintf(stderr, "%s%s%s", werror_unused ? ANSI_RED : ANSI_YELLOW, full, ANSI_RESET);
                    if (full[0] && full[strlen(full) - 1] != '\n') fputc('\n', stderr);
                    free(full);
                } else {
                    fprintf(stderr, "%s%s%s\n", werror_unused ? ANSI_RED : ANSI_YELLOW, head, ANSI_RESET);
                }
            } else {
                fprintf(stderr, "%s%s%s\n", werror_unused ? ANSI_RED : ANSI_YELLOW, head, ANSI_RESET);
            }
        }
        free(d->name);
    }
    free(decls.arr);
    return err_count;
}

static int node_has_return(ASTNode *n) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_RETURN:
            return 1;
        case NODE_BLOCK: {
            BlockNode *b = (BlockNode*)n;
            for (size_t i = 0; i < b->n; i++)
                if (node_has_return(b->statements[i])) return 1;
            return 0;
        }
        case NODE_IF: {
            IfNode *in = (IfNode*)n;
            return node_has_return(in->body) || node_has_return(in->else_body);
        }
        case NODE_WHILE:
            return node_has_return(((WhileNode*)n)->body);
        case NODE_FOREACH:
            return node_has_return(((ForEachNode*)n)->body);
        case NODE_DO_WHILE:
            return node_has_return(((DoWhileNode*)n)->body);
        case NODE_SELECT: {
            SelectNode *sn = (SelectNode*)n;
            for (size_t i = 0; i < sn->n_cases; i++)
                if (node_has_return(sn->cases[i].body)) return 1;
            return node_has_return(sn->default_body);
        }
        case NODE_TRY: {
            TryNode *tn = (TryNode*)n;
            return node_has_return(tn->try_body) || node_has_return(tn->catch_body) || node_has_return(tn->final_body);
        }
        default:
            return 0;
    }
}

/* Devuelve cantidad de errores semanticos detectados en funciones. */
static int validate_function_returns_and_warnings(const char *in_path, const char *source_text, ASTNode *ast) {
    if (!ast || ast->type != NODE_PROGRAM) return 0;
    ProgramNode *p = (ProgramNode*)ast;
    int errors = 0;
    for (size_t i = 0; i < p->n_funcs; i++) {
        FunctionNode *fn = (FunctionNode*)p->functions[i];
        if (!fn || !fn->body || fn->body->type != NODE_BLOCK) continue;
        BlockNode *b = (BlockNode*)fn->body;
        int line = fn->base.line > 0 ? fn->base.line : 1;
        int col = fn->base.col > 0 ? fn->base.col : 1;
        
        if (b->n == 0) {
            char head[2048];
            snprintf(head, sizeof head,
                     "Archivo %s, linea %d, columna %d: aviso: funcion `%s` declarada con cuerpo vacio.",
                     in_path, line, col, fn->name ? fn->name : "<anon>");
            char *full = source_text ? diag_attach_snippet(source_text, line, col, head) : NULL;
            fprintf(stderr, "%s%s%s\n", ANSI_YELLOW, full ? full : head, ANSI_RESET);
            free(full);
            if (werror_unused) errors++;
        }
        if (!node_has_return(fn->body)) {
            char head[2048];
            snprintf(head, sizeof head,
                     "Archivo %s, linea %d, columna %d: error semantico: la funcion `%s` declara retorno `%s` pero no tiene ninguna sentencia `retornar`.",
                     in_path, line, col, fn->name ? fn->name : "<anon>", fn->return_type ? fn->return_type : "entero");
            char *full = source_text ? diag_attach_snippet(source_text, line, col, head) : NULL;
            fprintf(stderr, "%s%s%s\n", ANSI_RED, full ? full : head, ANSI_RESET);
            free(full);
            errors++;
        }
    }
    return errors;
}

/* Compila archivo jasb -> binario, devuelve 0 si OK */
int do_compile(const char *in_path, const char *out_path, char **err_msg) {
    extern int werror_unused;
    char in_path_abs[2048] = {0};
    const char *diag_path = in_path;
#ifdef _WIN32
    if (_fullpath(in_path_abs, in_path, sizeof(in_path_abs)))
        diag_path = in_path_abs;
#else
    {
        char *rp = realpath(in_path, NULL);
        if (rp) {
            strncpy(in_path_abs, rp, sizeof(in_path_abs) - 1);
            in_path_abs[sizeof(in_path_abs) - 1] = '\0';
            free(rp);
            diag_path = in_path_abs;
        }
    }
#endif

    FILE *f = fopen(in_path, "rb");
    if (!f) { if (err_msg) *err_msg = strdup("No se puede abrir archivo"); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return 1; }
    size_t nread = fread(buf, 1, size, f);
    buf[nread] = '\0';
    fclose(f);

    Lexer lex;
    lexer_init(&lex, buf);
    TokenVec tvec;
    token_vec_init(&tvec);
    Token tok;
    while (lexer_next(&lex, &tok) == 0) {
        token_vec_push(&tvec, &tok);
        token_free_value(&tok);
        if (tok.type == TOK_EOF) break;
    }

    if (lex.last_error) {
        if (buf && lex.err_line >= 1 && lex.err_column >= 1) {
            char head[2048];
            snprintf(head, sizeof head, "Archivo %s, linea %d, columna %d: %s", diag_path, lex.err_line, lex.err_column, lex.last_error);
            char *full = diag_attach_snippet(buf, lex.err_line, lex.err_column, head);
            fprintf(stderr, "%s%s%s", ANSI_RED, full, ANSI_RESET);
            free(full);
        } else
            fprintf(stderr, "%s%s: %s%s\n", ANSI_RED, diag_path, lex.last_error, ANSI_RESET);
        lexer_free(&lex);
        token_vec_free(&tvec);
        free(buf);
        return 1;
    }
    lexer_free(&lex);

    Parser par;
    parser_init(&par, &tvec, diag_path, buf);
    ASTNode *ast = parser_parse(&par);
    int parse_errs = 0;

    if (par.last_error) {
        fprintf(stderr, "%s%s%s", ANSI_RED, par.last_error, ANSI_RESET);
        size_t L = strlen(par.last_error);
        if (L == 0 || par.last_error[L - 1] != '\n')
            fputc('\n', stderr);
        parse_errs = 1;
    }

    if (verbose_flag) {
        printf("--- AST ---\n");
        print_ast(ast, 0);
    }

    if (parse_errs > 0) {
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(buf);
        return 1;
    }

    CodeGen *cg = codegen_create();
    if (!cg) {
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(buf);
        return 1;
    }
    int mod_errs = register_usar_modules((ProgramNode *)ast, in_path, diag_path, cg);
    if (mod_errs > 0) {
        fprintf(stderr, "%sCompilacion fallida: errores al cargar modulos `usar`.%s\n", ANSI_RED, ANSI_RESET);
        codegen_free(cg);
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(buf);
        return 1;
    }

    // El CLI maneja este argumento antes de llamar a do_compile, por lo que estara globalmente disponible.
    int sem_errs = 0;
    {
        extern int werror_unused;
        sem_errs = validate_function_returns_and_warnings(diag_path, buf, ast);
        if (sem_errs > 0) {
            codegen_free(cg);
            ast_free(ast);
            parser_free(&par);
            token_vec_free(&tvec);
            free(buf);
            return 1;
        }
    }

    SymbolTable sym;
    sym_init(&sym);
    resolve_program(ast, &sym);
    
    /* Limpiar scopes de simbolos tras resolve_program. */
    sym_exit_scope(&sym);
    sym_exit_scope(&sym);

    if (verbose_flag) {
        printf("--- Symbol Table ---\n");
        printf("  next_global=0x%X  next_local=%u  structs=%u\n",
               (unsigned)sym.next_global_offset, (unsigned)sym.next_local_offset, (unsigned)sym.n_structs);
    }

    size_t len;
    uint8_t *bin = codegen_generate(cg, ast, &len);
    if (!bin) {
        int err_line, err_col;
        const char *cerr = codegen_get_error(cg, &err_line, &err_col);
        if (cerr) {
            int col_snip = err_col >= 1 ? err_col : 1;
            if (buf && err_line >= 1) {
                char head[2048];
                snprintf(head, sizeof head, "Archivo %s, linea %d, columna %d: error semantico: %s", diag_path, err_line, col_snip, cerr);
                char *full = diag_attach_snippet(buf, err_line, col_snip, head);
                fprintf(stderr, "%s%s%s", ANSI_RED, full ? full : head, ANSI_RESET);
                if (full && full[0] && full[strlen(full) - 1] != '\n')
                    fputc('\n', stderr);
                free(full);
            } else {
                fprintf(stderr, "%sArchivo %s: error semantico (generacion de codigo): %s%s\n", ANSI_RED, diag_path, cerr, ANSI_RESET);
            }
        }
        codegen_free(cg);
        ast_free(ast); sym_free(&sym); parser_free(&par); token_vec_free(&tvec); free(buf); return 1;
    }

    /* Avisos de no usados despues de codegen exitoso: si el inicializador falla en codegen
     * (p. ej. llamada no resuelta), no se avisa "variable no usada" sobre un nombre que ya es invalido. */
    {
        extern int werror_unused;
        int local_unused_ast_errs = warn_unused_variables(diag_path, buf, ast);
        local_unused_ast_errs += warn_unused_functions(diag_path, buf, ast);
        if (werror_unused && local_unused_ast_errs > 0) {
            fprintf(stderr, "Compilacion fallida debido a errores (Analisis lexico/sintactico/semantico/variables o funciones no usadas).\n");
            codegen_free(cg);
            free(bin);
            ast_free(ast);
            sym_free(&sym);
            parser_free(&par);
            token_vec_free(&tvec);
            free(buf);
            return 1;
        }
    }

    codegen_free(cg);

    if (jbc_ir_opt_enabled && bin && len >= (size_t)IR_HEADER_SIZE) {
        size_t opt_len = 0;
        uint8_t *opt = jbc_optimize_ir_blob(bin, len, &opt_len);
        if (opt && opt_len > 0) {
            free(bin);
            bin = opt;
            len = opt_len;
        } else if (opt) {
            free(opt);
        }
    }

    FILE *of = fopen(out_path, "wb");
    if (!of) {
        free(bin);
        ast_free(ast);
        sym_free(&sym);
        parser_free(&par);
        token_vec_free(&tvec);
        free(buf);
        return 1;
    }
    fwrite(bin, 1, len, of);
    fclose(of);
    free(bin);

    if (verbose_flag)
        printf("Compilado: %s\n", out_path);
    else
        printf("Compilado: %s -> %s\n", in_path, out_path);

    warn_if_empty_program(in_path, ast);

    ast_free(ast);
    sym_free(&sym);
    parser_free(&par);
    token_vec_free(&tvec);
    free(buf);
    return 0;
}

static void jbc_normalize_path_seps(char *p) {
    if (!p) return;
    for (; *p; p++)
        if (*p == '/')
            *p = (char)PATH_SEP;
}

#ifdef _WIN32
static int jbc_stat_mtime(const char *path, time_t *mt) {
    struct _stat st;
    if (_stat(path, &st) != 0)
        return -1;
    *mt = st.st_mtime;
    return 0;
}
#else
static int jbc_stat_mtime(const char *path, time_t *mt) {
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    *mt = st.st_mtime;
    return 0;
}
#endif

/* Entre jasboot-ir-vm y *-trace, usar el mas reciente por mtime (evita vm.exe bloqueado al enlazar y trace nuevo). */
static int jbc_pick_newest_vm(const char *path_main, const char *path_trace, char *out, size_t out_size) {
    time_t tm = 0, tt = 0;
    int em = jbc_stat_mtime(path_main, &tm);
    int et = jbc_stat_mtime(path_trace, &tt);
    const char *pick = NULL;
    if (em == 0 && et == 0) {
        if (tt > tm)
            pick = path_trace;
        else
            pick = path_main;
    } else if (em == 0)
        pick = path_main;
    else if (et == 0)
        pick = path_trace;
    else
        return -1;
    FILE *f = fopen(pick, "rb");
    if (!f)
        return -1;
    fclose(f);
#ifdef _WIN32
    {
        char abs[1024];
        if (_fullpath(abs, pick, sizeof abs))
            snprintf(out, out_size, "%s", abs);
        else
            snprintf(out, out_size, "%s", pick);
    }
#else
    {
        char *r = realpath(pick, NULL);
        if (r) {
            snprintf(out, out_size, "%s", r);
            free(r);
        } else
            snprintf(out, out_size, "%s", pick);
    }
#endif
    return 0;
}

static int jbc_try_first_existing_path(const char *path, char *out, size_t out_size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fclose(f);
#ifdef _WIN32
    {
        char abs[1024];
        if (_fullpath(abs, path, sizeof abs))
            snprintf(out, out_size, "%s", abs);
        else
            snprintf(out, out_size, "%s", path);
    }
#else
    {
        char *r = realpath(path, NULL);
        if (r) {
            snprintf(out, out_size, "%s", r);
            free(r);
        } else
            snprintf(out, out_size, "%s", path);
    }
#endif
    return 0;
}

/* Busca la VM en rutas conocidas (junto a jbc.exe, repo root o cwd). */
static int find_vm_path(char *out, size_t out_size) {
    char path[1024];
    char path_tr[1024];
    /* 1) Desde el directorio del ejecutable: bin/../sdk-dependiente/jasboot-ir/bin/ */
    if (g_jbc_exe_dir[0]) {
#ifdef _WIN32
        snprintf(path, sizeof path, "%s%c..%csdk-dependiente%cjasboot-ir%cbin%cjasboot-ir-vm.exe",
                 g_jbc_exe_dir, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
        snprintf(path_tr, sizeof path_tr, "%s%c..%csdk-dependiente%cjasboot-ir%cbin%cjasboot-ir-vm-trace.exe",
                 g_jbc_exe_dir, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
#else
        snprintf(path, sizeof path, "%s%c..%csdk-dependiente%cjasboot-ir%cbin%cjasboot-ir-vm",
                 g_jbc_exe_dir, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
        snprintf(path_tr, sizeof path_tr, "%s%c..%csdk-dependiente%cjasboot-ir%cbin%cjasboot-ir-vm-trace",
                 g_jbc_exe_dir, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
#endif
        jbc_normalize_path_seps(path);
        jbc_normalize_path_seps(path_tr);
        if (jbc_pick_newest_vm(path, path_tr, out, out_size) == 0)
            return 0;
    }

    const char *bases[] = { NULL, ".", "..", "../..", "../../..", "../../../..", "../../../../../.." };
    if (getenv("JASBOOT_REPO")) bases[0] = getenv("JASBOOT_REPO");
#ifdef _WIN32
    const char *pair_l[] = {
        "sdk-dependiente/jasboot-ir/bin/jasboot-ir-vm.exe",
        "bin/jasboot-ir-vm.exe",
    };
    const char *pair_r[] = {
        "sdk-dependiente/jasboot-ir/bin/jasboot-ir-vm-trace.exe",
        "bin/jasboot-ir-vm-trace.exe",
    };
    const char *single[] = { "bin/jasboot.exe", "jasboot-ir-vm.exe" };
#else
    const char *pair_l[] = { "sdk-dependiente/jasboot-ir/bin/jasboot-ir-vm", "bin/jasboot-ir-vm" };
    const char *pair_r[] = { "sdk-dependiente/jasboot-ir/bin/jasboot-ir-vm-trace", "bin/jasboot-ir-vm-trace" };
    const char *single[] = { "bin/jasboot", "jasboot-ir-vm" };
#endif
    for (int bi = 0; bi < (int)(sizeof(bases) / sizeof(bases[0])); bi++) {
        const char *base = bases[bi];
        if (!base) continue;
        for (size_t pi = 0; pi < sizeof(pair_l) / sizeof(pair_l[0]); pi++) {
            snprintf(path, sizeof path, "%s/%s", base, pair_l[pi]);
            snprintf(path_tr, sizeof path_tr, "%s/%s", base, pair_r[pi]);
            jbc_normalize_path_seps(path);
            jbc_normalize_path_seps(path_tr);
            if (jbc_pick_newest_vm(path, path_tr, out, out_size) == 0)
                return 0;
        }
        for (size_t si = 0; si < sizeof(single) / sizeof(single[0]); si++) {
            snprintf(path, sizeof path, "%s/%s", base, single[si]);
            jbc_normalize_path_seps(path);
            if (jbc_try_first_existing_path(path, out, out_size) == 0)
                return 0;
        }
    }
    snprintf(out, out_size, "jasboot-ir-vm"); /* fallback a PATH */
    return 0;
}

/* Ejecuta VM con el binario */
static int run_vm(const char *bin_path, const char *ruta_cerebro, const char *cwd) {
    char vm_path[1024];
    find_vm_path(vm_path, sizeof vm_path);

    if (cwd && chdir(cwd) != 0) { /* ignorar error cwd */ }

#ifdef _WIN32
    /* Usar _spawnv para evitar que cmd.exe interprete el comando */
    const char *argv[] = { vm_path, bin_path, NULL };
    if (ruta_cerebro) _putenv_s("JASBOOT_RUTA_CEREBRO", ruta_cerebro);
    if (verbose_flag) _putenv_s("JASBOOT_DEBUG", "1");
    intptr_t r = _spawnv(_P_WAIT, vm_path, argv);
    if (ruta_cerebro) _putenv_s("JASBOOT_RUTA_CEREBRO", "");
    if (verbose_flag) _putenv_s("JASBOOT_DEBUG", "");
    if (r < 0) {
        fprintf(stderr, "%sNo se pudo ejecutar la VM `%s` (compilacion correcta; .jbo en `%s`). "
            "Compruebe que existe jasboot-ir-vm.exe o anada la VM al PATH.%s\n",
            ANSI_RED, vm_path, bin_path, ANSI_RESET);
        return 1;
    }
    if (r != 0) {
        fprintf(stderr, "%sjbc (-e): la VM termino con codigo de salida %d.%s\n",
                ANSI_RED, (int)r, ANSI_RESET);
        if ((int)r == 1)
            fprintf(stderr,
                    "         Motivo frecuente: error de ejecucion en la VM (p. ej. `mat4_inversa` singular, "
                    "division por cero o indice de lista invalido). Revise el mensaje que imprimio la VM justo encima.\n");
    }
    return (int)r;
#else
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "JASBOOT_RUTA_CEREBRO=%s JASBOOT_DEBUG=%s \"%s\" \"%s\"",
             ruta_cerebro ? ruta_cerebro : "", verbose_flag ? "1" : "0", vm_path, bin_path);
    int r = system(cmd);
    return (r == 0) ? 0 : 1;
#endif
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Uso: %s [archivo.jasb] [opciones]\n", prog);
    fprintf(stderr, "     %s test [--dir DIR]\n\n", prog);
    fprintf(stderr, "Opciones:\n");
    fprintf(stderr, "  -o, --output ARCHIVO   Archivo de salida .jbo (default: mismo nombre con .jbo)\n");
    fprintf(stderr, "  -e, --ejecutar         Compilar y ejecutar con la VM\n");
    fprintf(stderr, "  -v, --verbose          Modo depuracion (AST, tabla de simbolos)\n");
    fprintf(stderr, "  -b, --ruta-cerebro R   JASBOOT_RUTA_CEREBRO para la VM\n");
    fprintf(stderr, "  --Werror-unused        Tratar advertencias de variables y funciones no usadas como errores\n");
    fprintf(stderr, "  --ir-opt               Optimizar IR (plegado / DCE; experimental)\n");
    fprintf(stderr, "  -h, --help             Ayuda\n\n");
    fprintf(stderr, "Subcomandos:\n");
    fprintf(stderr, "  test [--dir DIR]       Suite de pruebas (default dir: tests)\n");
}

/* Test runner: compila .jasb en dir y opcionalmente ejecuta */
static int do_test(const char *tests_dir) {
    char pattern[512];
    snprintf(pattern, sizeof pattern, "%s/*.jasb", tests_dir);
    printf("\n=== Suite de pruebas jbc ===\n");
    printf("Directorio: %s\n\n", tests_dir);

#ifdef _WIN32
    struct _finddata_t fd;
    intptr_t h = _findfirst(pattern, &fd);
    if (h == -1) {
        printf("No se encontraron archivos .jasb\n");
        return 0;
    }
    int ok = 0, fail = 0;
    do {
        if (fd.attrib & _A_SUBDIR) continue;
        char in_path[512], out_path[512];
        snprintf(in_path, sizeof in_path, "%s/%s", tests_dir, fd.name);
        { size_t il = strlen(in_path);
          if (il > 5 && strcmp(in_path + il - 5, ".jasb") == 0)
              snprintf(out_path, sizeof out_path, "%.*s.jbo", (int)(il - 5), in_path);
          else snprintf(out_path, sizeof out_path, "%s.jbo", in_path);
        }
        printf("[?] %s ... ", fd.name);
        fflush(stdout);
        if (do_compile(in_path, out_path, NULL) == 0) {
            printf("[OK]\n");
            ok++;
            remove(out_path);
        } else {
            printf("[FALLO]\n");
            fail++;
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR *d = opendir(tests_dir);
    if (!d) {
        printf("No se encontró directorio: %s\n", tests_dir);
        return 1;
    }
    int ok = 0, fail = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < 6 || strcmp(e->d_name + len - 5, ".jasb") != 0) continue;
        char in_path[512], out_path[512];
        snprintf(in_path, sizeof in_path, "%s/%s", tests_dir, e->d_name);
        { size_t il = strlen(in_path);
          if (il > 5 && strcmp(in_path + il - 5, ".jasb") == 0)
              snprintf(out_path, sizeof out_path, "%.*s.jbo", (int)(il - 5), in_path);
          else snprintf(out_path, sizeof out_path, "%s.jbo", in_path);
        }
        printf("[?] %s ... ", e->d_name);
        fflush(stdout);
        if (do_compile(in_path, out_path, NULL) == 0) {
            printf("[OK]\n");
            ok++;
            remove(out_path);
        } else {
            printf("[FALLO]\n");
            fail++;
        }
    }
    closedir(d);
#endif

    printf("\n=== Resumen: %d OK, %d Fallidos ===\n\n", ok, fail);
    return (fail > 0) ? 1 : 0;
}

#if !defined(JBC_TEST_DO_COMPILE)
#if defined(JBC_MINIMAL_MAIN)
static int find_vm_path(char *out, size_t out_size);
static int run_vm(const char *bin_path, const char *ruta_cerebro, const char *cwd);

int main(int argc, char **argv) {
    init_jbc_exe_dir();
    const char *input = NULL;
    const char *output = NULL;
    int do_execute = 0;
    const char *ruta_cerebro = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Uso: jbc [archivo.jasb] [-o salida.jbo] [-e] [-v] [-b ruta-cerebro] [--Werror-unused] [--ir-opt]\n");
            return 0;
        }
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "-o requiere argumento\n"); return 1; }
            output = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--ejecutar") == 0) {
            do_execute = 1;
            continue;
        }
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--ruta-cerebro") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "-b requiere argumento\n"); return 1; }
            ruta_cerebro = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_flag = 1;
            continue;
        }
        if (strcmp(argv[i], "--Werror-unused") == 0) {
            werror_unused = 1;
            continue;
        }
        if (strcmp(argv[i], "--ir-opt") == 0) {
            jbc_ir_opt_enabled = 1;
            continue;
        }
        if (argv[i][0] != '-' && !input) input = argv[i];
    }
    if (!input) { fprintf(stderr, "Error: especifique archivo .jasb\n"); return 1; }
    char out_buf[512];
    if (!output) {
        size_t len = strlen(input);
        if (len > 5 && strcmp(input + len - 5, ".jasb") == 0)
            snprintf(out_buf, sizeof out_buf, "%.*s.jbo", (int)(len - 5), input);
        else
            snprintf(out_buf, sizeof out_buf, "%s.jbo", input);
        output = out_buf;
    }
    if (do_compile(input, output, NULL) != 0) return 1;
    if (!do_execute) return 0;
    fflush(stdout);
    /* Ejecutar con la VM */
    char bin_abs[1024];
#ifdef _WIN32
    if (_fullpath(bin_abs, output, sizeof bin_abs)) { (void)0; }
    else snprintf(bin_abs, sizeof bin_abs, "%s", output);
#else
    { char *r = realpath(output, NULL); if (r) { strncpy(bin_abs, r, sizeof bin_abs - 1); bin_abs[sizeof bin_abs - 1] = '\0'; free(r); } else snprintf(bin_abs, sizeof bin_abs, "%s", output); }
#endif
    char cwd[1024];
    const char *in_dir = strrchr(input, '/');
    if (!in_dir) in_dir = strrchr(input, PATH_SEP);
    if (in_dir && in_dir > input) {
        size_t dlen = (size_t)(in_dir - input);
        if (dlen < sizeof cwd) { memcpy(cwd, input, dlen); cwd[dlen] = '\0'; } else cwd[0] = '\0';
    } else { cwd[0] = '.'; cwd[1] = '\0'; }
    return run_vm(bin_abs, ruta_cerebro, cwd[0] ? cwd : NULL);
}
#else
int main(int argc, char **argv) {
    init_jbc_exe_dir();
    const char *input_file = NULL;
    const char *output_file = NULL;  /* se deriva del input si no se da -o */
    int do_execute = 0;
    const char *ruta_cerebro = NULL;
    int show_help = 0;

    /* 9.6 Compatibilidad: sin subcomando, primer arg puede ser archivo.jasb */
    if (argc > 1 && argv[1][0] != '-' && strcmp(argv[1], "test") != 0) {
        input_file = argv[1];
        argv++;
        argc--;
    }

    /* Parsear opciones */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) { output_file = argv[++i]; } else { fprintf(stderr, "-o requiere argumento\n"); return 1; }
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--ejecutar") == 0) {
            do_execute = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_flag = 1;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--ruta-cerebro") == 0) {
            if (i + 1 < argc) { ruta_cerebro = argv[++i]; } else { fprintf(stderr, "-b requiere argumento\n"); return 1; }
        } else if (strcmp(argv[i], "--Werror-unused") == 0) {
            // Placeholder: global variable werror_unused needs to be defined
            extern int werror_unused;
            werror_unused = 1;
        } else if (strcmp(argv[i], "--ir-opt") == 0) {
            jbc_ir_opt_enabled = 1;
        } else if (strcmp(argv[i], "test") == 0) {
            /* 9.5 Subcomando test */
            const char *test_dir = "tests";
            if (i + 1 < argc && strcmp(argv[i + 1], "--dir") == 0) {
                if (i + 2 < argc) test_dir = argv[i + 2];
                i += 2;
            }
            return do_test(test_dir);
        } else if (argv[i][0] != '-' && !input_file) {
            input_file = argv[i];
        }
    }

    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (!input_file) {
        fprintf(stderr, "Error: especifique archivo .jasb\n");
        print_usage(argv[0]);
        return 1;
    }

    /* 9.1 Compilar: jbc archivo.jasb -o archivo.jbo */
    char out_buf[512];
    if (!output_file) {
        size_t in_len = strlen(input_file);
        if (in_len > 5 && strcmp(input_file + in_len - 5, ".jasb") == 0) {
            snprintf(out_buf, sizeof out_buf, "%.*s.jbo", (int)(in_len - 5), input_file);
        } else {
            snprintf(out_buf, sizeof out_buf, "%s.jbo", input_file);
        }
        output_file = out_buf;
    }
    if (do_compile(input_file, output_file, NULL) != 0)
        return 1;

    /* 9.2 -e: ejecutar VM */
    if (do_execute) {
        fflush(stdout);
        char bin_abs[1024];
#ifdef _WIN32
        _fullpath(bin_abs, output_file, sizeof bin_abs);
#else
        if (realpath(output_file, bin_abs) == NULL)
            snprintf(bin_abs, sizeof bin_abs, "%s", output_file);
#endif
        char cwd[1024];
        const char *in_dir = strrchr(input_file, PATH_SEP);
        if (in_dir) {
            size_t dlen = (size_t)(in_dir - input_file);
            if (dlen < sizeof cwd) {
                memcpy(cwd, input_file, dlen);
                cwd[dlen] = '\0';
            } else
                cwd[0] = '\0';
        } else
            strcpy(cwd, ".");
        return run_vm(bin_abs, ruta_cerebro, cwd[0] ? cwd : NULL);
    }

    return 0;
}
#endif /* !JBC_MINIMAL_MAIN */
#endif /* !JBC_TEST_DO_COMPILE */
