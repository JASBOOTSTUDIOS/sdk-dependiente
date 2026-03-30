/*
 * Instalado como <repo>/bin/jbc.exe: reenvia todos los argumentos al compilador real
 * en sdk-dependiente/jas-compiler-c/bin/jbc.exe. Asi build.bat solo actualiza el exe
 * del SDK y no hace falta sobrescribir bin/jbc.exe (suele estar bloqueado en Windows).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_exe_dir(char *path) {
    char *p = strrchr(path, '\\');
    if (p) *p = '\0';
}

int main(int argc, char **argv) {
    char mod[MAX_PATH];
    char rel[MAX_PATH * 2];
    char resolved[MAX_PATH * 2];

    if (!GetModuleFileNameA(NULL, mod, MAX_PATH)) {
        fprintf(stderr, "jbc: GetModuleFileName fallo\n");
        return 1;
    }
    strip_exe_dir(mod);
    /* mod = ...\jasboot\bin */
    {
        int nw = snprintf(rel, sizeof rel, "%s\\..\\sdk-dependiente\\jas-compiler-c\\bin\\jbc.exe", mod);
        if (nw < 0 || (size_t)nw >= sizeof rel) {
            fprintf(stderr, "jbc: ruta demasiado larga\n");
            return 1;
        }
    }
    if (!GetFullPathNameA(rel, (DWORD)sizeof resolved, resolved, NULL)) {
        fprintf(stderr, "jbc: GetFullPathName fallo\n");
        return 1;
    }
    if (GetFileAttributesA(resolved) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr,
                "jbc: no existe el compilador en:\n  %s\n"
                "Ejecute: sdk-dependiente\\jas-compiler-c\\build.bat\n",
                resolved);
        return 1;
    }

    char cmdline[32768];
    size_t pos = 0;
    int n = snprintf(cmdline + pos, sizeof(cmdline) - pos, "\"%s\"", resolved);
    if (n < 0 || (size_t)n >= sizeof(cmdline) - pos) {
        fprintf(stderr, "jbc: linea de orden demasiado larga\n");
        return 1;
    }
    pos += (size_t)n;
    for (int i = 1; i < argc; i++) {
        n = snprintf(cmdline + pos, sizeof(cmdline) - pos, " \"%s\"", argv[i]);
        if (n < 0 || (size_t)n >= sizeof(cmdline) - pos) {
            fprintf(stderr, "jbc: linea de orden demasiado larga\n");
            return 1;
        }
        pos += (size_t)n;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    char *mutable_cmd = (char *)malloc(pos + 2);
    if (!mutable_cmd)
        return 1;
    memcpy(mutable_cmd, cmdline, pos + 1);

    if (!CreateProcessA(NULL, mutable_cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "jbc: CreateProcess fallo (%lu)\n", (unsigned long)GetLastError());
        free(mutable_cmd);
        return 1;
    }
    free(mutable_cmd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
