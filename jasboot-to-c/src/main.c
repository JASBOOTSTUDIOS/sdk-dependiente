#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen_c.h"
#include "lexer.h"
#include "token_vec.h"
#include "parser.h"
#include "nodes.h"
#include "diagnostic.h"
#include "aot_usar_merge.h"

#ifdef _WIN32
#include <process.h>
#include <direct.h>
#include <io.h>
#define PATH_SEP '\\'
typedef void *HMODULE_;
typedef unsigned long DWORD_;
#ifndef WINAPI
#define WINAPI __stdcall
#endif
__declspec(dllimport) DWORD_ WINAPI GetModuleFileNameA(HMODULE_ hModule, char *lpFilename, DWORD_ nSize);
__declspec(dllimport) int WINAPI SetConsoleOutputCP(unsigned int wCodePageID);
__declspec(dllimport) int WINAPI SetConsoleCP(unsigned int wCodePageID);
#else
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#define PATH_SEP '/'
#endif

#define ANSI_YELLOW "\x1b[33m"
#define ANSI_RED    "\x1b[31m"
#define ANSI_RESET  "\x1b[0m"

static void jb_init_console_unicode(void) {
    static int inited = 0;
    if (inited) return;
    inited = 1;
#ifdef _WIN32
    SetConsoleOutputCP(65001u);
    SetConsoleCP(65001u);
#else
    setlocale(LC_CTYPE, "");
#endif
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Jasboot to C Transpiler (AOT)\n");
    fprintf(stderr, "Uso: %s <archivo.jasb> [opciones]\n\n", prog);
    fprintf(stderr, "Opciones:\n");
    fprintf(stderr, "  -o, --output <salida>       Ejecutable final (por defecto: <mismo_dir>/<nombre>.exe o sin extension)\n");
    fprintf(stderr, "  -e, --ejecutar              Ejecutar el programa tras la compilacion\n");
    fprintf(stderr, "  --strict                    Propaga modo estricto al generador (reservado)\n");
    fprintf(stderr, "  -h, --help                  Mostrar esta ayuda\n");
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1u, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static void get_exe_dir(char *out, size_t size) {
    if (!out || size == 0) return;
    out[0] = '.';
    out[1] = '\0';
#ifdef _WIN32
    if (!GetModuleFileNameA(NULL, out, (DWORD_)size) || strlen(out) == 0) {
        out[0] = '.';
        out[1] = '\0';
    }
#else
    ssize_t n = readlink("/proc/self/exe", out, size - 1u);
    if (n < 0) {
        out[0] = '.';
        out[1] = '\0';
    } else
        out[(size_t)n] = '\0';
#endif
    char *last = strrchr(out, PATH_SEP);
    if (last) *last = '\0';
}

static const char *path_basename(const char *p) {
    const char *a = strrchr(p, '/');
    const char *b = strrchr(p, '\\');
    const char *last = NULL;
    if (a && b)
        last = (a > b) ? a : b;
    else
        last = a ? a : b;
    return last ? last + 1 : p;
}

static void path_dirname_copy(const char *input_file, char *out, size_t cap) {
    if (!input_file || !out || cap == 0) return;
    size_t n = strlen(input_file);
    if (n + 1u > cap) n = cap - 1u;
    memcpy(out, input_file, n + 1u);
    char *ls = strrchr(out, '/');
    char *ls2 = strrchr(out, '\\');
    char *last = NULL;
    if (ls && ls2)
        last = (ls > ls2) ? ls : ls2;
    else
        last = ls ? ls : ls2;
    if (last)
        *last = '\0';
    else {
        out[0] = '.';
        out[1] = '\0';
    }
}

static void path_join2(char *out, size_t cap, const char *dir, const char *name) {
    if (!out || cap == 0) return;
    size_t nd = strlen(dir);
    int need = (nd > 0 && dir[nd - 1u] != '/' && dir[nd - 1u] != '\\');
    snprintf(out, cap, "%s%s%s", dir, need ? "/" : "", name);
}

static int path_to_abs(const char *in, char *out, size_t osz) {
    if (!in || !out || osz == 0) return 0;
#ifdef _WIN32
    return _fullpath(out, in, (int)osz) != NULL;
#else
    {
        char *r = realpath(in, NULL);
        if (!r) return 0;
        snprintf(out, osz, "%s", r);
        free(r);
        return 1;
    }
#endif
}

static void quote_shell_path(const char *p, char *q, size_t qcap) {
    if (!p || !q || qcap < 4u) {
        if (q && qcap) q[0] = '\0';
        return;
    }
    size_t j = 0;
    q[j++] = '"';
    for (size_t i = 0; p[i] && j + 2u < qcap; i++) {
        if (p[i] == '"') {
            if (j + 3u >= qcap) break;
            q[j++] = '\\';
        }
        q[j++] = (char)p[i];
    }
    q[j++] = '"';
    q[j] = '\0';
}

int main(int argc, char **argv) {
    jb_init_console_unicode();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    const char *input_file = argv[1];
    if (input_file[0] == '-') {
        fprintf(stderr, "%sEl primer argumento debe ser el archivo .jasb (no una opcion).%s\n", ANSI_RED, ANSI_RESET);
        print_usage(argv[0]);
        return 1;
    }

    char output_exe_buf[4096];
    int do_execute = 0;
    int strict = 0;
    const char *output_exe_opt = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s%s requiere un argumento%s\n", ANSI_RED, argv[i], ANSI_RESET);
                return 1;
            }
            output_exe_opt = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--ejecutar") == 0) {
            do_execute = 1;
        } else if (strcmp(argv[i], "--strict") == 0) {
            strict = 1;
        } else {
            fprintf(stderr, "%sOpcion desconocida: %s%s\n", ANSI_RED, argv[i], ANSI_RESET);
            print_usage(argv[0]);
            return 1;
        }
    }

    char src_dir[4096];
    path_dirname_copy(input_file, src_dir, sizeof src_dir);

    const char *base = path_basename(input_file);
    char stem[512];
    size_t blen = strlen(base);
    if (blen > 5u && strcmp(base + blen - 5u, ".jasb") == 0)
        snprintf(stem, sizeof stem, "%.*s", (int)(blen - 5u), base);
    else
        snprintf(stem, sizeof stem, "%s", base);

    char cname[640];
    snprintf(cname, sizeof cname, "%s.c", stem);
    char output_c[4096];
    path_join2(output_c, sizeof output_c, src_dir, cname);

#ifdef _WIN32
#define JB_EXE_SUFFIX ".exe"
#else
#define JB_EXE_SUFFIX ""
#endif
    if (output_exe_opt)
        snprintf(output_exe_buf, sizeof output_exe_buf, "%s", output_exe_opt);
    else {
        char exe_name[640];
        snprintf(exe_name, sizeof exe_name, "%s%s", stem, JB_EXE_SUFFIX);
        path_join2(output_exe_buf, sizeof output_exe_buf, src_dir, exe_name);
    }

    fprintf(stderr, "%sJasboot to C Transpiler%s\n", ANSI_YELLOW, ANSI_RESET);
    fprintf(stderr, "Transpilando: %s\n", input_file);

    char *source = read_file(input_file);
    if (!source) {
        fprintf(stderr, "%sError: no se pudo leer '%s'%s\n", ANSI_RED, input_file, ANSI_RESET);
        return 1;
    }

    Lexer lex;
    lexer_init(&lex, source);
    TokenVec tvec;
    token_vec_init(&tvec);
    Token tok;
    while (lexer_next(&lex, &tok) == 0) {
        token_vec_push(&tvec, &tok);
        token_free_value(&tok);
        if (tok.type == TOK_EOF) break;
    }

    if (lex.last_error) {
        if (source && lex.err_line >= 1 && lex.err_column >= 1) {
            char head[2048];
            snprintf(head, sizeof head, "Archivo %s, linea %d, columna %d: %s",
                     input_file, lex.err_line, lex.err_column, lex.last_error);
            char *full = diag_attach_snippet(source, lex.err_line, lex.err_column, head);
            fprintf(stderr, "%s%s%s", ANSI_RED, full, ANSI_RESET);
            free(full);
        } else
            fprintf(stderr, "%s%s: %s%s\n", ANSI_RED, input_file, lex.last_error, ANSI_RESET);
        lexer_free(&lex);
        token_vec_free(&tvec);
        free(source);
        return 1;
    }
    lexer_free(&lex);

    Parser par;
    parser_init(&par, &tvec, input_file, source);
    ASTNode *ast = parser_parse(&par);

    if (par.last_error) {
        fprintf(stderr, "%s%s%s", ANSI_RED, par.last_error, ANSI_RESET);
        size_t L = strlen(par.last_error);
        if (L == 0 || par.last_error[L - 1u] != '\n')
            fputc('\n', stderr);
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(source);
        return 1;
    }

    char entry_abs[4096];
    if (!path_to_abs(input_file, entry_abs, sizeof entry_abs))
        snprintf(entry_abs, sizeof entry_abs, "%s", input_file);

    if (aot_merge_usar_modules(ast, entry_abs) != 0) {
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(source);
        return 1;
    }

    FILE *out = fopen(output_c, "wb");
    if (!out) {
        fprintf(stderr, "%sError: no se pudo crear %s%s\n", ANSI_RED, output_c, ANSI_RESET);
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(source);
        return 1;
    }

    fprintf(stderr, "Generando codigo C: %s\n", output_c);
    jbc_generate_c_opts(ast, out, output_c, strict);
    fclose(out);

    char exe_dir[4096];
    get_exe_dir(exe_dir, sizeof exe_dir);

    char rt_c[4096], rt_json_c[4096], rt_jmn_vm[4096], rt_inc[4096];
    char sdk_jmn_src[4096], jmn_neu_inc[4096];
    path_join2(rt_c, sizeof rt_c, exe_dir, "runtime/jasboot_rt.c");
    path_join2(rt_json_c, sizeof rt_json_c, exe_dir, "runtime/jasboot_rt_json.c");
    path_join2(rt_jmn_vm, sizeof rt_jmn_vm, exe_dir, "runtime/jasboot_rt_jmn_vm.c");
    path_join2(rt_inc, sizeof rt_inc, exe_dir, "runtime");
    path_join2(sdk_jmn_src, sizeof sdk_jmn_src, exe_dir, "../jasboot-jmn-core/src");
    path_join2(jmn_neu_inc, sizeof jmn_neu_inc, exe_dir, "../jasboot-jmn-core/src/memoria_neuronal");

    char q_c[8192], q_rt[8192], q_rtj[8192], q_rtjmn[8192], q_inc[8192], q_out[8192];
    char q_sdk_jmn[8192], q_jmn_neu[8192];
    char jmn_objs[8192];
    static const char *jmn_rel_paths[] = {
        "memoria_neuronal/memoria_neuronal_core.c",
        "memoria_neuronal/memoria_neuronal_nodos.c",
        "memoria_neuronal/memoria_neuronal_conexiones.c",
        "memoria_neuronal/memoria_neuronal_io.c",
        "memoria_neuronal/memoria_neuronal_busqueda.c",
        "memoria_neuronal/memoria_neuronal_cognitivo.c",
        "memoria_neuronal/memoria_neuronal_utilidades.c",
        "memoria_neuronal/memoria_neuronal_estructuras.c",
        "memoria_neuronal/memoria_neuronal_texto_fix.c",
        "platform_compat.c",
    };
    size_t ji;
    quote_shell_path(output_c, q_c, sizeof q_c);
    quote_shell_path(rt_c, q_rt, sizeof q_rt);
    quote_shell_path(rt_json_c, q_rtj, sizeof q_rtj);
    quote_shell_path(rt_jmn_vm, q_rtjmn, sizeof q_rtjmn);
    quote_shell_path(rt_inc, q_inc, sizeof q_inc);
    quote_shell_path(output_exe_buf, q_out, sizeof q_out);
    quote_shell_path(sdk_jmn_src, q_sdk_jmn, sizeof q_sdk_jmn);
    quote_shell_path(jmn_neu_inc, q_jmn_neu, sizeof q_jmn_neu);

    jmn_objs[0] = '\0';
    for (ji = 0; ji < sizeof jmn_rel_paths / sizeof jmn_rel_paths[0]; ji++) {
        char absf[4096], qf[8192];
        path_join2(absf, sizeof absf, sdk_jmn_src, jmn_rel_paths[ji]);
        quote_shell_path(absf, qf, sizeof qf);
        if (jmn_objs[0])
            strncat(jmn_objs, " ", sizeof jmn_objs - strlen(jmn_objs) - 1u);
        strncat(jmn_objs, qf, sizeof jmn_objs - strlen(jmn_objs) - 1u);
    }

    char cmd[131072];
    snprintf(cmd, sizeof cmd,
             "gcc -std=c11 -Wall %s %s %s %s %s -I%s -I%s -I%s -o %s -lm",
             q_c, q_rt, q_rtj, q_rtjmn, jmn_objs, q_inc, q_jmn_neu, q_sdk_jmn, q_out);

    fprintf(stderr, "Compilando: %s\n", output_exe_buf);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "%sError: fallo gcc (codigo %d)%s\n", ANSI_RED, ret, ANSI_RESET);
        ast_free(ast);
        parser_free(&par);
        token_vec_free(&tvec);
        free(source);
        return 1;
    }

    fprintf(stderr, "%sListo:%s %s\n", ANSI_YELLOW, ANSI_RESET, output_exe_buf);

    if (do_execute) {
        char run_cmd[8192];
        quote_shell_path(output_exe_buf, run_cmd, sizeof run_cmd);
        fprintf(stderr, "--- Ejecucion ---\n");
        (void)system(run_cmd);
    }

    ast_free(ast);
    parser_free(&par);
    token_vec_free(&tvec);
    free(source);
    return 0;
}
