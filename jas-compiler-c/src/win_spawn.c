/* Lanzar un .exe con un solo argumento y esperar (solo Windows).
 * MinGW _spawnv falla a veces con rutas absolutas; CreateProcess es fiable.
 *
 * La VM debe heredar stdin/stdout/stderr del proceso jbc (que a su vez suele ser
 * hijo de Node con run-jasb.cjs). Con bInheritHandles=FALSE el hijo a veces obtiene
 * consola/handles raros y fprintf/imprimir en la VM puede bloquearse (tuberia llena).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

int jbc_win_spawn_wait(const char *exe_path, const char *first_arg) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[32768];
    int n;

    if (!exe_path || !exe_path[0] || !first_arg) {
        fprintf(stderr, "jbc: rutas de VM invalidas\n");
        return -1;
    }

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    {
        HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
        int ok = (hin && hin != INVALID_HANDLE_VALUE && hout && hout != INVALID_HANDLE_VALUE && herr &&
                  herr != INVALID_HANDLE_VALUE);
        if (ok) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = hin;
            si.hStdOutput = hout;
            si.hStdError = herr;
        }
    }
    memset(&pi, 0, sizeof pi);

    n = snprintf(cmdline, sizeof cmdline, "\"%s\" \"%s\"", exe_path, first_arg);
    if (n <= 0 || (size_t)n >= sizeof cmdline) {
        fprintf(stderr, "jbc: linea de comando demasiado larga\n");
        return -1;
    }

    /* TRUE: heredar handles; con STARTF_USESTDHANDLES la VM escribe donde jbc/Node. */
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "jbc: CreateProcess fallo (%lu)\n", (unsigned long)GetLastError());
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
