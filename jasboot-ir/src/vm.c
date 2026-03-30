#ifndef VM_TRACE_EXECUTION
//#define VM_TRACE_EXECUTION 1
#endif

#include "vm.h"
#include "reader_ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <dlfcn.h>
#include <sys/select.h>
#include <termios.h>
#endif

/* Maximo de caracteres '0'-'9' en la entrada (no cuenta '-' ni '.'). Alineado con precision util de flotante en VM. */
#define VM_ENTRADA_FLOTANTE_MAX_DIGITOS 17

static int vm_entrada_fl_cuenta_digitos(const char *s, size_t n) {
    int d = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] >= '0' && s[i] <= '9') d++;
    return d;
}

/* Tras el aviso multilinea el cursor ya no coincide con los digitos; se edita en linea "> " redibujada. */
static void vm_entrada_fl_redibujar_linea(const char *buf) {
    fputs("\r\x1b[2K> ", stdout);
    fputs(buf, stdout);
    fflush(stdout);
}

static void vm_entrada_fl_rechazo_por_limite(const char *buf, int *alerta_completa, int *modo_redibujar) {
    fputc('\a', stdout);
    if (!*alerta_completa) {
        fprintf(stdout,
                "\n[entrada_flotante] Limite: maximo %d digitos (0-9). "
                "Siga editando en la linea \">\" (Retroceso borra; Enter confirma).\n",
                VM_ENTRADA_FLOTANTE_MAX_DIGITOS);
        *modo_redibujar = 1;
        fputs("> ", stdout);
        fputs(buf, stdout);
        fflush(stdout);
        *alerta_completa = 1;
    } else {
        fflush(stdout);
    }
}

/* Lectura interactiva: solo digitos, un punto decimal, '-' al inicio; Retroceso; Enter -> float32 */
static float vm_io_entrada_flotante_leer(void) {
    char buf[128];
    size_t len = 0;
    int alerta_limite_completa = 0; /* 1 = ya se mostro el texto largo; se resetea al bajar de max digitos */
    int modo_redibujar = 0;       /* 1 = eco por linea completa (ANSI), no \\b */
    fflush(stdout);
#if defined(_WIN32) || defined(_WIN64)
    for (;;) {
        int c = _getch();
        if (c == 0 || c == 224) {
            (void)_getch();
            continue;
        }
        if (c == '\r' || c == '\n') {
            putchar('\n');
            fflush(stdout);
            break;
        }
        if (c == 8 || c == 127) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
                if (modo_redibujar) {
                    vm_entrada_fl_redibujar_linea(buf);
                } else {
                    fputs("\b \b", stdout);
                    fflush(stdout);
                }
                if (vm_entrada_fl_cuenta_digitos(buf, len) < VM_ENTRADA_FLOTANTE_MAX_DIGITOS) {
                    alerta_limite_completa = 0;
                }
            }
            continue;
        }
        if (c == '-' && len == 0) {
            buf[len++] = '-';
            buf[len] = '\0';
            if (modo_redibujar) {
                vm_entrada_fl_redibujar_linea(buf);
            } else {
                putchar('-');
                fflush(stdout);
            }
            continue;
        }
        if (c == '.' && len < sizeof(buf) - 1) {
            size_t k;
            int tiene_punto = 0;
            for (k = 0; k < len; k++)
                if (buf[k] == '.') {
                    tiene_punto = 1;
                    break;
                }
            if (!tiene_punto) {
                buf[len++] = '.';
                buf[len] = '\0';
                if (modo_redibujar) {
                    vm_entrada_fl_redibujar_linea(buf);
                } else {
                    putchar('.');
                    fflush(stdout);
                }
            }
            continue;
        }
        if (c >= '0' && c <= '9' && len < sizeof(buf) - 1) {
            if (vm_entrada_fl_cuenta_digitos(buf, len) >= VM_ENTRADA_FLOTANTE_MAX_DIGITOS) {
                vm_entrada_fl_rechazo_por_limite(buf, &alerta_limite_completa, &modo_redibujar);
                continue;
            }
            buf[len++] = (char)c;
            buf[len] = '\0';
            if (modo_redibujar) {
                vm_entrada_fl_redibujar_linea(buf);
            } else {
                putchar(c);
                fflush(stdout);
            }
            continue;
        }
    }
#else
    {
        struct termios tsav, traw;
        int raw_ok = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &tsav) == 0;
        if (raw_ok) {
            traw = tsav;
            traw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
            traw.c_cc[VMIN] = 1;
            traw.c_cc[VTIME] = 0;
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &traw);
        }
        if (!raw_ok) {
            if (fgets(buf, (int)sizeof(buf), stdin)) {
                len = strlen(buf);
                while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                    buf[--len] = '\0';
                }
            }
            if (vm_entrada_fl_cuenta_digitos(buf, len) > VM_ENTRADA_FLOTANTE_MAX_DIGITOS) {
                fprintf(stdout,
                        "[entrada_flotante] La linea tiene mas de %d digitos; "
                        "puede perder precision al convertirse a flotante.\n",
                        VM_ENTRADA_FLOTANTE_MAX_DIGITOS);
                fflush(stdout);
            }
            return strtof(buf, NULL);
        }
        for (;;) {
                int c = getchar();
                if (c == EOF) {
                    break;
                }
                if (c == '\r' || c == '\n') {
                    putchar('\n');
                    fflush(stdout);
                    break;
                }
                if (c == 8 || c == 127) {
                    if (len > 0) {
                        len--;
                        buf[len] = '\0';
                        if (modo_redibujar) {
                            vm_entrada_fl_redibujar_linea(buf);
                        } else {
                            fputs("\b \b", stdout);
                            fflush(stdout);
                        }
                        if (vm_entrada_fl_cuenta_digitos(buf, len) < VM_ENTRADA_FLOTANTE_MAX_DIGITOS) {
                            alerta_limite_completa = 0;
                        }
                    }
                    continue;
                }
                if (c == '-' && len == 0) {
                    buf[len++] = '-';
                    buf[len] = '\0';
                    if (modo_redibujar) {
                        vm_entrada_fl_redibujar_linea(buf);
                    } else {
                        putchar('-');
                        fflush(stdout);
                    }
                    continue;
                }
                if (c == '.' && len < sizeof(buf) - 1) {
                    size_t k;
                    int tiene_punto = 0;
                    for (k = 0; k < len; k++)
                        if (buf[k] == '.') {
                            tiene_punto = 1;
                            break;
                        }
                    if (!tiene_punto) {
                        buf[len++] = '.';
                        buf[len] = '\0';
                        if (modo_redibujar) {
                            vm_entrada_fl_redibujar_linea(buf);
                        } else {
                            putchar('.');
                            fflush(stdout);
                        }
                    }
                    continue;
                }
                if (c >= '0' && c <= '9' && len < sizeof(buf) - 1) {
                    if (vm_entrada_fl_cuenta_digitos(buf, len) >= VM_ENTRADA_FLOTANTE_MAX_DIGITOS) {
                        vm_entrada_fl_rechazo_por_limite(buf, &alerta_limite_completa, &modo_redibujar);
                        continue;
                    }
                    buf[len++] = (char)c;
                    buf[len] = '\0';
                    if (modo_redibujar) {
                        vm_entrada_fl_redibujar_linea(buf);
                    } else {
                        putchar(c);
                        fflush(stdout);
                    }
                    continue;
                }
        }
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &tsav);
    }
#endif
    buf[len] = '\0';
    if (len == 0) {
        return 0.0f;
    }
    return strtof(buf, NULL);
}

#define VM_PERCEPCION_CAP_DEFAULT 64u
#define VM_PERCEPCION_CAP_MIN 8u
#define VM_PERCEPCION_CAP_MAX 4096u
#define VM_PERCEPCION_LISTA_ID 0x7EC3E701u

#define VM_RASTRO_CAP_DEFAULT 128u
#define VM_RASTRO_CAP_MIN 16u
#define VM_RASTRO_CAP_MAX 2048u
#define VM_RASTRO_LISTA_ID 0x7E57AC71u

static void vm_text_cache_free(VM* vm);

static uint64_t vm_percepcion_now_ms(void) {
    return (uint64_t)time(NULL) * 1000ull;
}

static void vm_percepcion_clear(VM* vm) {
    if (!vm) return;
    vm->percepcion_head = 0;
    vm->percepcion_count = 0;
}

static int vm_percepcion_set_cap(VM* vm, uint32_t newcap) {
    if (!vm) return -1;
    if (newcap < VM_PERCEPCION_CAP_MIN) newcap = VM_PERCEPCION_CAP_MIN;
    if (newcap > VM_PERCEPCION_CAP_MAX) newcap = VM_PERCEPCION_CAP_MAX;
    VMPercepcionEntrada* p = (VMPercepcionEntrada*)calloc(newcap, sizeof(VMPercepcionEntrada));
    if (!p) return -1;
    free(vm->percepcion_buf);
    vm->percepcion_buf = p;
    vm->percepcion_cap = newcap;
    vm_percepcion_clear(vm);
    return 0;
}

static void vm_percepcion_push(VM* vm, uint32_t id) {
    if (!vm || id == 0 || !vm->percepcion_buf || vm->percepcion_cap == 0) return;
    uint64_t ts = vm_percepcion_now_ms();
    uint32_t cap = vm->percepcion_cap;
    if (vm->percepcion_count < cap) {
        uint32_t pos = (vm->percepcion_head + vm->percepcion_count) % cap;
        vm->percepcion_buf[pos].id = id;
        vm->percepcion_buf[pos].ts_ms = ts;
        vm->percepcion_count++;
    } else {
        vm->percepcion_buf[vm->percepcion_head].id = id;
        vm->percepcion_buf[vm->percepcion_head].ts_ms = ts;
        vm->percepcion_head = (vm->percepcion_head + 1u) % cap;
    }
}

static uint32_t vm_percepcion_id_at(const VM* vm, uint32_t k) {
    if (!vm || !vm->percepcion_buf || k >= vm->percepcion_count) return 0u;
    uint32_t cap = vm->percepcion_cap;
    return vm->percepcion_buf[(vm->percepcion_head + vm->percepcion_count - 1u - k + cap) % cap].id;
}

static void vm_rastro_clear(VM* vm) {
    if (!vm) return;
    vm->rastro_head = 0;
    vm->rastro_count = 0;
}

static int vm_rastro_set_cap(VM* vm, uint32_t newcap) {
    if (!vm) return -1;
    if (newcap < VM_RASTRO_CAP_MIN) newcap = VM_RASTRO_CAP_MIN;
    if (newcap > VM_RASTRO_CAP_MAX) newcap = VM_RASTRO_CAP_MAX;
    VMRastroActivacionEntrada* p = (VMRastroActivacionEntrada*)calloc(newcap, sizeof(VMRastroActivacionEntrada));
    if (!p) return -1;
    free(vm->rastro_buf);
    vm->rastro_buf = p;
    vm->rastro_cap = newcap;
    vm_rastro_clear(vm);
    return 0;
}

static void vm_rastro_push(VM* vm, uint32_t id, float act) {
    if (!vm || !vm->rastro_buf || vm->rastro_cap == 0) return;
    uint32_t cap = vm->rastro_cap;
    if (vm->rastro_count < cap) {
        uint32_t pos = (vm->rastro_head + vm->rastro_count) % cap;
        vm->rastro_buf[pos].id = id;
        vm->rastro_buf[pos].activacion = act;
        vm->rastro_count++;
    } else {
        uint32_t pos = vm->rastro_head;
        vm->rastro_buf[pos].id = id;
        vm->rastro_buf[pos].activacion = act;
        vm->rastro_head = (vm->rastro_head + 1u) % cap;
    }
}

static uint32_t vm_rastro_id_at(const VM* vm, uint32_t i) {
    if (!vm || !vm->rastro_buf || i >= vm->rastro_count) return 0u;
    uint32_t cap = vm->rastro_cap;
    return vm->rastro_buf[(vm->rastro_head + i) % cap].id;
}

static float vm_rastro_peso_at(const VM* vm, uint32_t i) {
    if (!vm || !vm->rastro_buf || i >= vm->rastro_count) return 0.f;
    uint32_t cap = vm->rastro_cap;
    return vm->rastro_buf[(vm->rastro_head + i) % cap].activacion;
}

#ifdef JASBOOT_LANG_INTEGRATION
#include "memoria_neuronal.h"

static void vm_jmn_rastro_cb(void* ud, uint32_t id, float act) {
    VM* vm = (VM*)ud;
    vm_rastro_push(vm, id, act);
}

/* ensure_jmn eliminado para soberanía de datos */

static void ensure_jmn_col(VM* vm) {
    if (!vm->mem_colecciones) {
        vm->mem_colecciones = jmn_crear_memoria_ram(100000, 1000000);
    }
}
#endif

#ifdef JASBOOT_LANG_INTEGRATION
static int jmn_callback_recolectar_ids(JMNNodo* nodo, void* user_data) {
    JMNMemoria* mem = (JMNMemoria*)user_data;
    uint32_t lista_id = 0xA11C04CE; // "ALL CONCEPTS" hash
    JMNValor val;
    val.u = nodo->id;
    jmn_lista_agregar(mem, lista_id, val);
    return 0; // Continuar iterando
}

typedef struct {
    uint32_t target_id;
    uint32_t found_id;
    JMNMemoria* mem;
} JMNBusquedaPadreCtx;

static int jmn_callback_buscar_padre_secuencia(JMNNodo* nodo, void* user_data) {
    JMNBusquedaPadreCtx* ctx = (JMNBusquedaPadreCtx*)user_data;
    uint32_t count = 0;
    JMNConexion* conns = jmn_obtener_conexiones(ctx->mem, nodo, &count);
    
    for (uint32_t i = 0; i < count; i++) {
        // Chequear si apunta al target Y es de tipo SECUENCIA
        // Nota: key_id almacena el tipo de relacion si < 100 o flags
        // Asumimos JMN_RELACION_SECUENCIA = 3
        if (conns[i].destino_id == ctx->target_id) {
             if (conns[i].key_id == JMN_RELACION_SECUENCIA) {
                 ctx->found_id = nodo->id;
                 return 1; // Stop iteration
             }
        }
    }
    return 0;
}

#define VM_ELEGIR_POR_PESO_MAX_CAND 32u

static void vm_elegir_por_peso_best(VM* vm, uint32_t ctx, uint32_t list_id,
    uint32_t* out_idx, uint32_t* out_id, int* ok_out) {
    *ok_out = 0;
    if (!vm->mem_neuronal || !vm->mem_colecciones) return;
    uint32_t n = jmn_lista_tamano(vm->mem_colecciones, list_id);
    if (n == 0) return;
    if (n > VM_ELEGIR_POR_PESO_MAX_CAND) n = VM_ELEGIR_POR_PESO_MAX_CAND;
    const float eps = 1e-6f;
    uint32_t seed = vm->elegir_por_peso_seed;
    int found = 0;
    float best_s = 0.f;
    uint32_t best_cand = 0, best_i = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t cand = jmn_lista_obtener(vm->mem_colecciones, list_id, i).u;
        float s1 = jmn_obtener_fuerza_asociacion(vm->mem_neuronal, ctx, cand);
        float s2 = jmn_obtener_fuerza_asociacion(vm->mem_neuronal, cand, ctx);
        float s = s1 > s2 ? s1 : s2;
        uint32_t lex = seed ? (cand ^ seed) : cand;
        uint32_t best_lex = seed ? (best_cand ^ seed) : best_cand;
        if (!found) {
            found = 1;
            best_s = s;
            best_cand = cand;
            best_i = i;
        } else if (s > best_s + eps) {
            best_s = s;
            best_cand = cand;
            best_i = i;
        } else if (fabsf(s - best_s) <= eps) {
            if (lex < best_lex || (lex == best_lex && i < best_i)) {
                best_s = s;
                best_cand = cand;
                best_i = i;
            }
        }
    }
    if (!found) return;
    *out_idx = best_i;
    *out_id = best_cand;
    *ok_out = 1;
}
#endif

static void vm_text_cache_free(VM* vm) {
    if (!vm || !vm->text_cache_buckets) return;
    for (size_t i = 0; i < vm->text_cache_size; i++) {
        VMTextCacheEntry* it = vm->text_cache_buckets[i];
        while (it) {
            VMTextCacheEntry* next = it->next;
            free(it->text);
            free(it);
            it = next;
        }
    }
    free(vm->text_cache_buckets);
    vm->text_cache_buckets = NULL;
}

static VMTextCacheEntry* vm_text_cache_find(VM* vm, uint32_t id) {
    if (!vm || !vm->text_cache_buckets) return NULL;
    size_t index = (size_t)id % vm->text_cache_size;
    for (VMTextCacheEntry* it = vm->text_cache_buckets[index]; it; it = it->next) {
        if (it->id == id) {
            /* Entradas antiguas sin text_len rellenado */
            if (it->text && it->text_len == 0 && it->text[0] != '\0')
                it->text_len = strlen(it->text);
            return it;
        }
    }
    return NULL;
}

static const char* vm_text_cache_get(VM* vm, uint32_t id) {
    VMTextCacheEntry* e = vm_text_cache_find(vm, id);
    return e ? e->text : NULL;
}

static int vm_text_cache_put(VM* vm, uint32_t id, const char* text) {
    if (!vm || !vm->text_cache_buckets) return -1;
    
    size_t index = id % vm->text_cache_size;
    
    // Si ya existe, actualizar
    VMTextCacheEntry* it = vm->text_cache_buckets[index];
    while (it) {
        if (it->id == id) {
            if (text && it->text && strcmp(it->text, text) == 0)
                return 0;
            char* dup = text ? strdup(text) : strdup("");
            if (!dup) return -1;
            free(it->text);
            it->text = dup;
            it->text_len = strlen(dup);
            // fprintf(stderr, "[DBG_CACHE] UPD id=%u -> '%s'\n", id, text);
            return 0;
        }
        it = it->next;
    }

    // Crear nuevo entrada en el bucket
    VMTextCacheEntry* n = (VMTextCacheEntry*)calloc(1, sizeof(VMTextCacheEntry));
    if (!n) return -1;
    n->id = id;
    n->text = text ? strdup(text) : strdup("");
    if (!n->text) {
        free(n);
        return -1;
    }
    n->text_len = strlen(n->text);
    n->next = vm->text_cache_buckets[index];
    vm->text_cache_buckets[index] = n;
    vm->text_cache_count++;
    
    // fprintf(stderr, "[DBG_CACHE] PUT id=%u -> '%s'\n", id, text);
    return 0;
}

static int vm_text_cache_get_copy(VM* vm, uint32_t id, char* buf, size_t max_len) {
    const char* txt = vm_text_cache_get(vm, id);
    if (txt) {
        strncpy(buf, txt, max_len - 1);
        buf[max_len - 1] = '\0';
        return 1;
    }
#ifdef JASBOOT_LANG_INTEGRATION
    if (vm->mem_neuronal) {
        return jmn_obtener_texto(vm->mem_neuronal, id, buf, max_len) >= 0;
    }
#endif
    return 0;
}

static uint32_t vm_hash_texto(const char* texto); /* forward */

/* Resolver path desde id: argv, cache, JMN. Para sys_argv paths que no están en cache. */
static const char* vm_resolve_path(VM* vm, uint32_t id) {
    if (!vm || id == 0) return NULL;
    /* 1. argv (paths de sys_argv) */
    if (vm->argv) {
        for (int i = 0; i < vm->argc && vm->argv[i]; i++) {
            uint32_t h = vm_hash_texto(vm->argv[i]);
            if (h == id) {
                vm_text_cache_put(vm, id, vm->argv[i]);
                return vm->argv[i];
            }
        }
        /* Debug si no encontramos */
        if (getenv("JASBOOT_DEBUG_FS")) {
            fprintf(stderr, "[vm_resolve_path] id=0x%08X no encontrado. argv hashes: ", (unsigned)id);
            for (int i = 0; i < vm->argc && vm->argv[i]; i++)
                fprintf(stderr, "[%d]%s->0x%08X ", i, vm->argv[i], (unsigned)vm_hash_texto(vm->argv[i]));
            fprintf(stderr, "\n");
        }
    }
    /* 2. cache */
    { const char* r = vm_text_cache_get(vm, id); if (r) return r; }
#ifdef JASBOOT_LANG_INTEGRATION
    /* 3. JMN */
    if (vm->mem_neuronal) {
        static char path_buf[1024];
        if (jmn_obtener_texto(vm->mem_neuronal, id, path_buf, sizeof(path_buf)) >= 0 && path_buf[0]) {
            vm_text_cache_put(vm, id, path_buf);
            return path_buf;
        }
    }
#endif
    return NULL;
}


VM* vm_create(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    VM* vm = (VM*)calloc(1, sizeof(VM));
    if (!vm) return NULL;
    
    // Inicializar registros (r0 siempre es cero)
    memset(vm->registers, 0, sizeof(vm->registers));
    
    // Inicializar memoria (4MB por defecto para soportar recursión profunda)
    vm->memory_size = 1024 * 1024 * 4;
    vm->memory = (uint8_t*)calloc(1, vm->memory_size);
    if (!vm->memory) {
        free(vm);
        return NULL;
    }
    
    // Inicializar Tabla Hash de Strings
    vm->text_cache_size = 4096; // Suficiente para un compilador
    vm->text_cache_buckets = (VMTextCacheEntry**)calloc(vm->text_cache_size, sizeof(VMTextCacheEntry*));
    vm->text_cache_count = 0;

    vm->stack_ptr = 0;
    vm->fp_stack_ptr = 0;
    vm->fp = 0x4000; // Stack starts at 16KB (globals at 2KB)
    vm->sp = 0x4000;
    vm->pc = 0;
    vm->running = 0;
    vm->exit_code = 0;
    vm->try_depth = 0;
    vm->ir = ir_file_create();
    vm->mem_neuronal = NULL;
    vm->context = NULL;
    vm->self_path = NULL;

    // Memoria neuronal se inicializa solo bajo demanda explícita (soberanía de datos)
    vm->mem_neuronal = NULL;
    vm->current_file = stdout;
    vm->modo_continuo = 0;
    vm->ffi_handles = NULL;
    vm->ffi_handles_cap = 0;
    vm->ffi_handles_count = 0;
    vm->heap_ptrs = NULL;
    vm->heap_cap = 0;
    vm->heap_count = 0;

    vm->percepcion_cap = VM_PERCEPCION_CAP_DEFAULT;
    vm->percepcion_buf = (VMPercepcionEntrada*)calloc(vm->percepcion_cap, sizeof(VMPercepcionEntrada));
    vm->percepcion_head = 0;
    vm->percepcion_count = 0;

    vm->rastro_cap = VM_RASTRO_CAP_DEFAULT;
    vm->rastro_buf = (VMRastroActivacionEntrada*)calloc(vm->rastro_cap, sizeof(VMRastroActivacionEntrada));
    vm->rastro_head = 0;
    vm->rastro_count = 0;
    vm->elegir_por_peso_seed = 0;

    // Pre-poblar cache con cadena vacía (hash 5381) para estabilidad
    vm_text_cache_put(vm, 5381, "");
    
    return vm;
}

#include <inttypes.h>

/* fflush solo en textos cortos: megacadenas + fflush por carácter lógico saturan el terminal (p. ej. VS Code). */
#define VM_IMPRIMIR_FLUSH_MAX 16384

static void vm_flush_stdout_after_text(const char* texto, size_t byte_len) {
    if (ferror(stdout)) return;
    if (byte_len == (size_t)-1) {
        if (!texto) {
            fflush(stdout);
            return;
        }
        size_t n = 0;
        while (n < VM_IMPRIMIR_FLUSH_MAX && texto[n] != '\0') n++;
        if (texto[n] == '\0' && n < VM_IMPRIMIR_FLUSH_MAX)
            fflush(stdout);
        return;
    }
    if (byte_len < VM_IMPRIMIR_FLUSH_MAX)
        fflush(stdout);
    (void)texto;
}

static void vm_escribir_cadena(const char* texto) {
    if (!texto) return;
    fputs(texto, stdout);
    vm_flush_stdout_after_text(texto, (size_t)-1);
}

static void vm_escribir_texto_bytes(const char* texto, size_t nbytes) {
    if (nbytes == 0) {
        vm_flush_stdout_after_text("", 0);
        return;
    }
    if (!texto) return;
    fwrite(texto, 1, nbytes, stdout);
    vm_flush_stdout_after_text(texto, nbytes);
}

/* Valor centinela solo para OP_IMPRIMIR_NUMERO (imprime "nan"). str_a_entero invalido ya no lo asigna: lanza error capturable. */
#define VM_U64_CONV_ENTERO_INVALIDO UINT64_C(0xFFFD000000000000)

static void vm_escribir_entero(uint64_t valor) {
    if ((int64_t)valor < 0) printf("%" PRId64, (int64_t)valor);
    else printf("%" PRIu64, valor);
    fflush(stdout);
}

/* Si valor parece timestamp Unix (segundos 2001-2038), imprime fecha/hora legible y devuelve 1; si no, 0. */
static int vm_escribir_si_timestamp(uint64_t valor) {
    if (valor < 1000000000ULL || valor > 2500000000ULL) return 0;
    time_t t = (time_t)valor;
    struct tm* tm = localtime(&t);
    if (!tm) return 0;
    char buf[64];
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm) == 0) return 0;
    vm_escribir_cadena(buf);
    return 1;
}

static void vm_escribir_flotante(uint64_t valor) {
    union { uint32_t i; float f; } u;
    u.i = (uint32_t)valor;
    if (isnan((double)u.f)) {
        vm_escribir_cadena("nan");
        return;
    }
    printf("%.4f", u.f);
    fflush(stdout);
}

/* Copia src a dst (hasta dstsz-1) sin espacio inicial ni final. */
static void vm_str_trim_copia(const char *src, char *dst, size_t dstsz) {
    size_t n;
    const char *a;
    const char *b;
    if (!dst || dstsz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    a = src;
    while (*a && isspace((unsigned char)*a)) a++;
    b = a + strlen(a);
    while (b > a && isspace((unsigned char)b[-1])) b--;
    n = (size_t)(b - a);
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, a, n);
    dst[n] = '\0';
}

/* Decimal estricto: toda la cadena (tras trim) debe ser el numero; sin basura ni solo espacios. */
static int vm_parse_decimal_entero_estricto(const char *s, int64_t *out) {
    char t[256];
    char *end = NULL;
    long long v;
    if (!out) return 0;
    vm_str_trim_copia(s, t, sizeof t);
    if (t[0] == '\0') return 0;
    errno = 0;
    v = strtoll(t, &end, 10);
    if (errno == ERANGE) return 0;
    if (end == t) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return 0;
    *out = (int64_t)v;
    return 1;
}

/* Devuelve el float parseado o NaN si la cadena no es un numero flotante valido (C strtod, consumo total). */
static float vm_parse_decimal_flotante_estricto(const char *s) {
    char t[256];
    char *end = NULL;
    double d;
    vm_str_trim_copia(s, t, sizeof t);
    if (t[0] == '\0') return nanf("");
    errno = 0;
    d = strtod(t, &end);
    if (end == t) return nanf("");
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return nanf("");
    if (errno == ERANGE && (d == HUGE_VAL || d == -HUGE_VAL)) {
        /* inf representable; no es error de "letras" */
    }
    return (float)d;
}

/* Leer/escribir float desde memoria (8 bytes por float, bits en low 32 del uint64) */
static float vm_mem_read_float(const VM* vm, size_t addr) {
    if (addr + 8 > vm->memory_size) return 0.0f;
    uint64_t u = *(uint64_t*)(vm->memory + addr);
    union { uint64_t u64; float f32; } uf = { .u64 = (uint32_t)(u & 0xFFFFFFFF) };
    return uf.f32;
}
static void vm_mem_write_float(VM* vm, size_t addr, float f) {
    if (addr + 8 > vm->memory_size) return;
    union { uint64_t u64; float f32; } uf = { .f32 = f };
    *(uint64_t*)(vm->memory + addr) = uf.u64;
}

void vm_destroy(VM* vm) {
    if (!vm) return;
#ifdef JASBOOT_LANG_INTEGRATION
    if (vm->mem_neuronal) {
        jmn_finalizar_escritura(vm->mem_neuronal);
        jmn_cerrar(vm->mem_neuronal);
        vm->mem_neuronal = NULL;
    }
    if (vm->mem_colecciones) {
        jmn_cerrar(vm->mem_colecciones);
        vm->mem_colecciones = NULL;
    }
#endif
    vm_text_cache_free(vm);
    if (vm->ffi_handles) {
#if defined(_WIN32) || defined(_WIN64)
        for (size_t i = 0; i < vm->ffi_handles_count; i++)
            if (vm->ffi_handles[i]) FreeLibrary((HMODULE)vm->ffi_handles[i]);
#else
        for (size_t i = 0; i < vm->ffi_handles_count; i++)
            if (vm->ffi_handles[i]) dlclose(vm->ffi_handles[i]);
#endif
        free(vm->ffi_handles);
        vm->ffi_handles = NULL;
    }
    if (vm->heap_ptrs) {
        for (size_t i = 0; i < vm->heap_count; i++)
            if (vm->heap_ptrs[i]) free(vm->heap_ptrs[i]);
        free(vm->heap_ptrs);
        vm->heap_ptrs = NULL;
    }
    free(vm->percepcion_buf);
    vm->percepcion_buf = NULL;
    free(vm->rastro_buf);
    vm->rastro_buf = NULL;
    if (vm->memory) free(vm->memory);
    if (vm->context) free(vm->context);
    if (vm->ir_path) free(vm->ir_path);
    // No destruimos ir porque puede ser compartido, pero si lo vamos a destruir, ir_file_destroy(vm->ir)
    free(vm);
}

static int vm_leer_u32(const uint8_t* data, size_t size, size_t offset, uint32_t* out) {
    if (!data || !out || offset + sizeof(uint32_t) > size) return -1;
    const uint8_t* p = data + offset;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 0;
}

static uint32_t vm_hash_texto(const char* texto) {
    uint32_t hash = 5381;
    if (!texto) return 0;
    for (const char* p = texto; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p; // hash * 33 + c
    }
    return hash;
}

#ifdef JASBOOT_LANG_INTEGRATION
// Integración con memoria neuronal activa
#endif

int vm_load(VM* vm, IRFile* ir) {
    if (!vm || !ir) return -1;
    vm->ir = ir;
    vm->pc = 0; // Header no está en ir->code
    vm->try_depth = 0;
    
    // Saltar metadata IA si está presente
    if (ir->header.flags & IR_FLAG_IA_METADATA && ir->ia_metadata_size > 0) {
        vm->pc += 4 + ir->ia_metadata_size;  // IA_SIZE (4 bytes) + payload
    }
    
    // Cargar datos en memoria (dirección base 0)
    if (ir->header.data_size > 0) {
        if (ir->header.data_size > vm->memory_size) {
            size_t new_size = ir->header.data_size;
            uint8_t* new_memory = realloc(vm->memory, new_size);
            if (!new_memory) {
                return -1;
            }
            if (new_size > vm->memory_size) {
                memset(new_memory + vm->memory_size, 0, new_size - vm->memory_size);
            }
            vm->memory = new_memory;
            vm->memory_size = new_size;
        }
        memcpy(vm->memory, ir->data, ir->header.data_size);
    }
    
    return 0;
}

int vm_load_file(VM* vm, const char* filename) {
    if (!vm || !filename) return -1;
    
    IRFile* ir = ir_file_create();
    if (!ir) return -1;
    
    if (ir_file_read(ir, filename) != 0) {
        ir_file_destroy(ir);
        return -1;
    }
    
    if (vm_load(vm, ir) != 0) {
        ir_file_destroy(ir);
        return -1;
    }
    
    if (vm->ir_path) free(vm->ir_path);
    vm->ir_path = strdup(filename);
    
    return 0;
}

void vm_set_modo_continuo(VM* vm, int activo) {
    if (vm) vm->modo_continuo = activo ? 1 : 0;
}

uint64_t vm_get_register(VM* vm, int reg) {
    if (!vm || reg < 0 || reg >= IR_REGISTER_COUNT) return 0;
    return vm->registers[reg];
}

void vm_set_register(VM* vm, int reg, uint64_t value) {
    if (!vm || reg < 0 || reg >= IR_REGISTER_COUNT) return;
    if (reg == 0) return;  // r0 siempre es cero
    vm->registers[reg] = value;
}

size_t vm_get_pc(VM* vm) {
    if (!vm) return 0;
    return vm->pc;
}

int vm_is_running(VM* vm) {
    if (!vm) return 0;
    return vm->running;
}

int vm_get_exit_code(VM* vm) {
    if (!vm) return 0;
    return vm->exit_code;
}

static uint64_t get_operand_value(VM* vm, IRInstruction* inst, int operand, int flag) {
    if (inst->flags & flag) {
        // Es inmediato
        return operand;
    } else {
        // Es registro
        return vm_get_register(vm, operand);
    }
}

static size_t vm_code_start(const IRFile* ir) {
    (void)ir;
    // El buffer ir->code contiene SOLO las instrucciones, empezando en offset 0.
    // El header y metadata están en la estructura IRFile pero no en el buffer de código.
    return 0;
}

static uint32_t vm_decode_u24(uint8_t a, uint8_t b, uint8_t c, uint8_t flags_ab_c) {
    uint32_t v = 0;
    v |= (uint32_t)a;
    if (flags_ab_c & IR_INST_FLAG_B_IMMEDIATE) v |= ((uint32_t)b << 8);
    if (flags_ab_c & IR_INST_FLAG_C_IMMEDIATE) v |= ((uint32_t)c << 16);
    return v;
}

static void vm_put_throw_texto(VM* vm, const char* msg) {
    if (!vm) return;
    if (!msg) msg = "";
    uint32_t h = vm_hash_texto(msg);
    vm_text_cache_put(vm, h, msg);
#ifdef JASBOOT_LANG_INTEGRATION
    if (vm->mem_neuronal)
        jmn_guardar_texto(vm->mem_neuronal, h, msg);
#endif
    vm_set_register(vm, 1, (uint64_t)h);
}

/* 1 si salto a atrapar/final con mensaje en registro 1 (id texto); 0 si no hay intentar activo. */
static int vm_try_catch_or_abort(VM* vm, const char* msg) {
    if (!vm || !vm->ir || vm->try_depth <= 0) return 0;
    size_t code_start = vm_code_start(vm->ir);
    uint32_t off = vm->try_code_off[vm->try_depth - 1];
    vm->try_depth--;
    vm_put_throw_texto(vm, msg);
    vm->pc = code_start + (size_t)off;
    vm->running = 1;
    vm->exit_code = 0;
    return 1;
}

int vm_step(VM* vm) {
    if (!vm || !vm->ir || !vm->running) return -1;
    
    // Verificar si estamos fuera de código
    size_t code_start = vm_code_start(vm->ir);
    
    if (vm->pc < code_start || vm->pc >= code_start + vm->ir->header.code_size) {
        vm->running = 0;
        return 0;  // Fin de ejecución
    }
    
    // Obtener instrucción
    IRInstruction inst;
    size_t inst_index = (vm->pc - code_start) / IR_INSTRUCTION_SIZE;
    if (inst_index >= vm->ir->code_count) {
        vm->running = 0;
        return -1;
    }
    const uint8_t* code_ptr = vm->ir->code + (inst_index * IR_INSTRUCTION_SIZE);
    inst.opcode = code_ptr[0];
    inst.flags = code_ptr[1];
    inst.operand_a = code_ptr[2];
    inst.operand_b = code_ptr[3];
    inst.operand_c = code_ptr[4];

    // Inlining de get_operand_value para máxima velocidad
    uint64_t a_val = (inst.flags & IR_INST_FLAG_A_IMMEDIATE) ? inst.operand_a : vm->registers[inst.operand_a];
    uint64_t b_val = (inst.flags & IR_INST_FLAG_B_IMMEDIATE) ? inst.operand_b : vm->registers[inst.operand_b];
    uint64_t c_val = (inst.flags & IR_INST_FLAG_C_IMMEDIATE) ? inst.operand_c : vm->registers[inst.operand_c];
    
    // Ejecutar instrucción
    switch (inst.opcode) {
        case OP_HALT:
            vm->running = 0;
            return 0; // Terminar exitosamente

        case OP_MOVER: {
            uint64_t val = b_val;
            if ((inst.flags & IR_INST_FLAG_B_IMMEDIATE) && (inst.flags & IR_INST_FLAG_C_IMMEDIATE)) {
                 val |= (uint64_t)inst.operand_c << 8;
            }
            vm_set_register(vm, inst.operand_a, val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MOVER_U24: {
            uint32_t val = (uint32_t)inst.operand_b | ((uint32_t)inst.operand_c << 8) | ((uint32_t)inst.flags << 16);
            vm_set_register(vm, inst.operand_a, (uint64_t)val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_LEER: {
            uint64_t addr = 0;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                addr = (uint64_t)inst.operand_b;
                if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                    addr |= ((uint64_t)inst.operand_c << 8);
                }
            } else {
                addr = b_val;
            }
            
            // Direccionamiento relativo al Frame Pointer (Sovereign Recursion)
            if (inst.flags & IR_INST_FLAG_RELATIVE) {
                addr += vm->fp;
            }
            
            if (addr + sizeof(uint64_t) <= vm->memory_size) {
                uint64_t value = *(uint64_t*)(vm->memory + addr);
                vm_set_register(vm, inst.operand_a, value);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_GET_FP: {
            vm_set_register(vm, inst.operand_a, (uint64_t)vm->fp);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
            
        case OP_ESCRIBIR: {
            uint64_t addr = 0;
            if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                addr = (uint64_t)inst.operand_a;
                if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                    addr |= ((uint64_t)inst.operand_c << 8);
                }
            } else {
                addr = a_val;
            }
            
            // Direccionamiento relativo al Frame Pointer
            if (inst.flags & IR_INST_FLAG_RELATIVE) {
                addr += vm->fp;
            }
            
            if (addr + sizeof(uint64_t) <= vm->memory_size) {
                 *(uint64_t*)(vm->memory + addr) = b_val;
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_LOAD_STR_HASH: {
            uint16_t offset = inst.operand_b;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                offset |= (uint16_t)(inst.operand_c << 8);
            }
            
            uint32_t hash = 0;
            if (vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                 const char* str = (const char*)(vm->ir->data + offset);
                 hash = vm_hash_texto(str);
                 vm_text_cache_put(vm, hash, str);
                 if (getenv("JASBOOT_DEBUG")) printf("[VM LOAD HASH] '%s' (off=%u, hash=%u) -> R%d\n", str, offset, hash, inst.operand_a);
#ifdef JASBOOT_LANG_INTEGRATION
                 if (vm->mem_neuronal) {
                     jmn_guardar_texto(vm->mem_neuronal, hash, str);
                 }
#endif
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)hash);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_DEBUG_LINE:
            // vm->current_line = (int)((inst.operand_b) | (inst.operand_c << 8));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_SUMAR: {
            vm_set_register(vm, inst.operand_a, b_val + c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
            
        case OP_RESTAR:
            vm_set_register(vm, inst.operand_a, b_val - c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        case OP_MULTIPLICAR:
            vm_set_register(vm, inst.operand_a, b_val * c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        case OP_DIVIDIR:
            if (c_val == 0) {
                char divmsg[160];
                if (vm->current_line > 0)
                    snprintf(divmsg, sizeof divmsg,
                             "Error de ejecucion (VM) en la linea %d: division por cero.", vm->current_line);
                else
                    snprintf(divmsg, sizeof divmsg, "Error de ejecucion (VM): division por cero.");
                if (vm_try_catch_or_abort(vm, divmsg)) return 0;
                fprintf(stderr, "%s\n", divmsg);
                vm->running = 0;
                vm->exit_code = 1;
                return 0;
            }
            vm_set_register(vm, inst.operand_a, b_val / c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_MODULO:
            if (c_val == 0) {
                char modmsg[160];
                if (vm->current_line > 0)
                    snprintf(modmsg, sizeof modmsg,
                             "Error de ejecucion (VM) en la linea %d: modulo por cero.", vm->current_line);
                else
                    snprintf(modmsg, sizeof modmsg, "Error de ejecucion (VM): modulo por cero.");
                if (vm_try_catch_or_abort(vm, modmsg)) return 0;
                fprintf(stderr, "%s\n", modmsg);
                vm->running = 0;
                vm->exit_code = 1;
                return 0;
            }
            vm_set_register(vm, inst.operand_a, b_val % c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_SUMAR_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0}, fres = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            fres.f32 = fb.f32 + fc.f32;
            vm_set_register(vm, inst.operand_a, fres.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_RESTAR_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0}, fres = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            fres.f32 = fb.f32 - fc.f32;
            vm_set_register(vm, inst.operand_a, fres.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MULTIPLICAR_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0}, fres = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            fres.f32 = fb.f32 * fc.f32;
            vm_set_register(vm, inst.operand_a, fres.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_DIVIDIR_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0}, fres = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            if (fc.f32 == 0.0f) {
                vm_set_register(vm, inst.operand_a, 0);
            } else {
                fres.f32 = fb.f32 / fc.f32;
                vm_set_register(vm, inst.operand_a, fres.u64);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
            
        case OP_Y:
            vm_set_register(vm, inst.operand_a, b_val & c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        case OP_O:
            vm_set_register(vm, inst.operand_a, b_val | c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        case OP_XOR:
            vm_set_register(vm, inst.operand_a, b_val ^ c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        case OP_NO:
            vm_set_register(vm, inst.operand_a, (b_val == 0) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case 0x24: // OP_BIT_NOT
            vm_set_register(vm, inst.operand_a, ~b_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        case OP_COMPARAR: {
            uint64_t result = 0;
            if ((int64_t)b_val < (int64_t)c_val) result = 2;
            else if ((int64_t)b_val > (int64_t)c_val) result = 3;
            else result = 0;
            vm_set_register(vm, inst.operand_a, result);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_CMP_EQ: {
            uint64_t res = (b_val == c_val) ? 1 : 0;
            vm_set_register(vm, inst.operand_a, res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_CMP_LT:
            vm_set_register(vm, inst.operand_a, ((int64_t)b_val < (int64_t)c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_GT:
            vm_set_register(vm, inst.operand_a, ((int64_t)b_val > (int64_t)c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_LE:
            vm_set_register(vm, inst.operand_a, ((int64_t)b_val <= (int64_t)c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_GE:
            vm_set_register(vm, inst.operand_a, ((int64_t)b_val >= (int64_t)c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_LT_U:
            vm_set_register(vm, inst.operand_a, (b_val < c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_GT_U:
            vm_set_register(vm, inst.operand_a, (b_val > c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_LE_U:
            vm_set_register(vm, inst.operand_a, (b_val <= c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        case OP_CMP_GE_U:
            vm_set_register(vm, inst.operand_a, (b_val >= c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_CMP_LT_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            vm_set_register(vm, inst.operand_a, (fb.f32 < fc.f32) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_CMP_GT_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            vm_set_register(vm, inst.operand_a, (fb.f32 > fc.f32) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_CMP_LE_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            vm_set_register(vm, inst.operand_a, (fb.f32 <= fc.f32) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_CMP_GE_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            vm_set_register(vm, inst.operand_a, (fb.f32 >= fc.f32) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_CMP_EQ_FLT: {
            union { uint64_t u64; float f32; } fb = {0}, fc = {0};
            fb.u64 = b_val & 0xFFFFFFFF; fc.u64 = c_val & 0xFFFFFFFF;
            vm_set_register(vm, inst.operand_a, (fb.f32 == fc.f32) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_BIT_SHL:
            vm_set_register(vm, inst.operand_a, b_val << c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_BIT_SHR:
            vm_set_register(vm, inst.operand_a, b_val >> c_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_SYS_EXEC: {
            uint32_t id = (uint32_t)b_val;
            char cmd[4096];
            if (vm_text_cache_get_copy(vm, id, cmd, sizeof(cmd))) {
                int status = system(cmd);
                vm_set_register(vm, inst.operand_a, (uint64_t)status);
            } else {
                vm_set_register(vm, inst.operand_a, (uint64_t)-1);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case 0x69: { // OP_SYS_ARGC
            vm_set_register(vm, inst.operand_a, (uint64_t)vm->argc);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case 0x6A: { // OP_SYS_ARGV
            int index = (int)b_val;
            uint32_t id = 0;
            if (index >= 0 && index < vm->argc && vm->argv[index]) {
                const char* arg = vm->argv[index];
                id = vm_hash_texto(arg);
                vm_text_cache_put(vm, id, arg);
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal) {
                    jmn_guardar_texto(vm->mem_neuronal, id, arg);
                }
#endif
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_ESCRIBIR_BYTE: {
            // A = handle_reg (opcional),
            uint8_t byte_val_local = (uint8_t)b_val;
            FILE* f = (inst.flags & IR_INST_FLAG_A_REGISTER) ? (FILE*)(uintptr_t)vm_get_register(vm, inst.operand_a) : vm->current_file;
            if (f) {
                fputc(byte_val_local, f);
                if (f == stdout || f == stderr) fflush(f);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_LEER_BYTE: {
            // A <- fgetc(handle B); si B=0 usa vm->current_file
            FILE* f = (inst.flags & IR_INST_FLAG_B_REGISTER)
                ? (FILE*)(uintptr_t)vm_get_register(vm, inst.operand_b)
                : vm->current_file;
            int c = (f && !feof(f)) ? fgetc(f) : EOF;
            vm_set_register(vm, inst.operand_a, (uint64_t)(c >= 0 ? (unsigned char)c : 0));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_MAPA_CREAR: {
            static uint32_t s_map_counter = 0;
            s_map_counter++;
            uint32_t map_id = ((uint32_t)time(NULL) ^ 0x01234567u) + (s_map_counter * 0x9E3779B9u);
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                jmn_crear_mapa(vm->mem_colecciones, map_id);
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)map_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_MAPA_PONER: {
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                JMNValor val; val.u = (uint32_t)c_val;
                jmn_mapa_insertar(vm->mem_colecciones, (uint32_t)a_val, (uint32_t)b_val, val);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_MAPA_OBTENER: {
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                JMNValor val;
                uint32_t map_id = (uint32_t)b_val;
                uint32_t map_key = (uint32_t)c_val;
                if (!jmn_mapa_obtener_si_existe(vm->mem_colecciones, map_id, map_key, &val)) {
                    char trymsg[288];
                    snprintf(trymsg, sizeof trymsg,
                             "clave de mapa inexistente: no hay valor para la clave %u; ese registro no fue creado "
                             "con mapa_poner (origen: mapa_obtener o m[clave]).",
                             (unsigned)map_key);
                    if (vm_try_catch_or_abort(vm, trymsg))
                        return 0;
                    if (vm->current_line > 0)
                        fprintf(stderr,
                                "Error de ejecucion (VM) en la linea %d: %s\n",
                                vm->current_line, trymsg);
                    else
                        fprintf(stderr, "Error de ejecucion (VM): %s\n", trymsg);
                    vm->running = 0;
                    vm->exit_code = 1;
                    return 0;
                }
                vm_set_register(vm, inst.operand_a, (uint64_t)val.u);
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            }
#endif
            vm_set_register(vm, inst.operand_a, 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_MAPA_TAMANO: {
            uint32_t map_id_val = (uint32_t)b_val;
            uint32_t tam = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones)
                tam = jmn_mapa_tamano(vm->mem_colecciones, map_id_val);
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)tam);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IR: {
            if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                uint32_t target = vm_decode_u24(inst.operand_a, inst.operand_b, inst.operand_c, inst.flags);
                if (inst.flags & IR_INST_FLAG_RELATIVE) vm->pc = vm->pc + (size_t)target;
                else vm->pc = code_start + (size_t)target;
            } else {
                if (inst.flags & IR_INST_FLAG_RELATIVE) vm->pc = vm->pc + (size_t)a_val;
                else vm->pc = code_start + (size_t)a_val;
            }
            break;
        }

        case OP_TRY_ENTER: {
            if (!(inst.flags & IR_INST_FLAG_A_IMMEDIATE) || !(inst.flags & IR_INST_FLAG_B_IMMEDIATE) ||
                !(inst.flags & IR_INST_FLAG_C_IMMEDIATE)) {
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            }
            uint32_t target = vm_decode_u24(inst.operand_a, inst.operand_b, inst.operand_c, inst.flags);
            if (vm->try_depth >= VM_TRY_STACK_MAX) {
                fprintf(stderr, "Error de ejecucion (VM): demasiados intentar anidados (max %d).\n", VM_TRY_STACK_MAX);
                vm->running = 0;
                vm->exit_code = 1;
                return 0;
            }
            vm->try_code_off[vm->try_depth++] = target;
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_TRY_LEAVE: {
            if (vm->try_depth > 0) vm->try_depth--;
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
            
        case OP_SI: {
            if (a_val != 0) {
                if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                    uint32_t target = (uint32_t)inst.operand_b;
                    if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) target |= ((uint32_t)inst.operand_c << 8);
                    size_t next_pc = (inst.flags & IR_INST_FLAG_RELATIVE) ? (vm->pc + (size_t)target) : (code_start + (size_t)target);
                    vm->pc = next_pc;
                } else {
                    size_t next_pc = (inst.flags & IR_INST_FLAG_RELATIVE) ? (vm->pc + (size_t)b_val) : (code_start + (size_t)b_val);
                    vm->pc = next_pc;
                }
            } else {
                vm->pc += IR_INSTRUCTION_SIZE;
            }
            break;
        }
            
        case OP_LLAMAR: {
            if (vm->stack_ptr >= VM_MAX_RECURSION || vm->fp_stack_ptr >= VM_MAX_RECURSION) {
                char stmsg[120];
                snprintf(stmsg, sizeof stmsg, "[VM ERR] Stack Overflow (Max Recursion: %d)", VM_MAX_RECURSION);
                if (vm_try_catch_or_abort(vm, stmsg)) return 0;
                fprintf(stderr, "%s\n", stmsg);
                vm->running = 0;
                vm->exit_code = 1;
                return 0;
            }
            // Guardar contexto (PC y FP)
            vm->stack[vm->stack_ptr++] = vm->pc + IR_INSTRUCTION_SIZE;
            vm->fp_stack[vm->fp_stack_ptr++] = vm->fp;
            
            // El nuevo frame empieza donde terminó el anterior (SP actual)
            vm->fp = vm->sp;
            
            if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                uint32_t target_call = vm_decode_u24(inst.operand_a, inst.operand_b, inst.operand_c, inst.flags);
                if (inst.flags & IR_INST_FLAG_RELATIVE) vm->pc = vm->pc + (size_t)target_call;
                else vm->pc = code_start + (size_t)target_call;
            } else {
                if (inst.flags & IR_INST_FLAG_RELATIVE) vm->pc = vm->pc + (size_t)a_val;
                else vm->pc = code_start + (size_t)a_val;
            }
            break;
        }
            
        case OP_RETORNAR: {
            if (vm->stack_ptr == 0 || vm->fp_stack_ptr == 0) {
                vm->running = 0;
                vm->exit_code = (int)a_val;
                return 0;
            }
            // Liberar variables locales del frame actual
            vm->sp = vm->fp;
            // Restaurar contexto (FP y PC)
            vm->fp = vm->fp_stack[--vm->fp_stack_ptr];
            vm->pc = vm->stack[--vm->stack_ptr];
            break;
        }

        case 0x44: { // OP_RESERVAR_PILA
            // Tamaño como u24 (A|B<<8|C<<16) para soportar >255 bytes de frame
            uint32_t bytes_to_alloc = (uint32_t)inst.operand_a 
                                    | ((uint32_t)inst.operand_b << 8) 
                                    | ((uint32_t)inst.operand_c << 16);
            // printf("[VM DBG] RESERVAR_PILA: %u bytes\n", bytes_to_alloc);
            if (vm->sp + bytes_to_alloc > vm->memory_size) {
                 const char *ms = "[VM ERR] Memory Stack Exhausted";
                 if (vm_try_catch_or_abort(vm, ms)) return 0;
                 fprintf(stderr, "%s\n", ms);
                 vm->running = 0;
                 vm->exit_code = 1;
                 return 0;
            }
            if (bytes_to_alloc > 0)
                memset(vm->memory + vm->sp, 0, (size_t)bytes_to_alloc);
            vm->sp += bytes_to_alloc;
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_HEAP_RESERVAR: {
            /* Usar b_val (resuelto por get_operand_value); si B|C inmediatos, combinar para valores >255 */
            uint64_t bytes = b_val;
            if ((inst.flags & IR_INST_FLAG_B_IMMEDIATE) && (inst.flags & IR_INST_FLAG_C_IMMEDIATE))
                bytes |= (uint64_t)inst.operand_c << 8;
            void* ptr = (bytes > 0 && bytes < (1ULL << 30)) ? malloc((size_t)bytes) : NULL;
            if (ptr) {
                if (vm->heap_count >= vm->heap_cap) {
                    size_t new_cap = vm->heap_cap ? vm->heap_cap * 2 : 16;
                    void** p = (void**)realloc(vm->heap_ptrs, new_cap * sizeof(void*));
                    if (p) {
                        vm->heap_ptrs = p;
                        vm->heap_cap = new_cap;
                    }
                }
                if (vm->heap_ptrs && vm->heap_count < vm->heap_cap) {
                    vm->heap_ptrs[vm->heap_count++] = ptr;
                }
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)(uintptr_t)ptr);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_HEAP_LIBERAR: {
            void* ptr = (void*)(uintptr_t)vm_get_register(vm, inst.operand_a);
            if (ptr) {
                /* Quitar de la lista si está */
                for (size_t i = 0; i < vm->heap_count; i++) {
                    if (vm->heap_ptrs[i] == ptr) {
                        free(ptr);
                        vm->heap_ptrs[i] = NULL; /* No compactamos para simplicidad */
                        break;
                    }
                }
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IR_ESCRIBIR: {
            uint32_t path_id = (uint32_t)vm_get_register(vm, inst.operand_a);
            const char* path_str = vm_text_cache_get(vm, path_id);
#ifdef JASBOOT_LANG_INTEGRATION
            if ((!path_str || !path_str[0]) && path_id && vm->mem_neuronal) {
                static char path_buf[512];
                if (jmn_obtener_texto(vm->mem_neuronal, path_id, path_buf, sizeof(path_buf)) >= 0)
                    path_str = path_buf;
            }
#endif
            if (path_str && path_str[0] && vm->ir)
                ir_file_write(vm->ir, path_str);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_CARGAR_BIBLIOTECA: {
            /* A = registro resultado (handle), B|C = offset 16b de ruta en data */
            uint8_t result_reg = inst.operand_a;
            size_t offset = (size_t)inst.operand_b | ((size_t)inst.operand_c << 8);
            uint64_t handle_val = 0;
            if (vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                const char* path = (const char*)vm->ir->data + offset;
#if defined(_WIN32) || defined(_WIN64)
                HMODULE h = LoadLibraryA(path);
                if (h) {
                    handle_val = (uint64_t)(uintptr_t)h;
                    if (vm->ffi_handles_count >= vm->ffi_handles_cap) {
                        size_t new_cap = vm->ffi_handles_cap ? vm->ffi_handles_cap * 2 : 8;
                        void** p = (void**)realloc(vm->ffi_handles, new_cap * sizeof(void*));
                        if (p) {
                            vm->ffi_handles = p;
                            vm->ffi_handles_cap = new_cap;
                        }
                    }
                    if (vm->ffi_handles && vm->ffi_handles_count < vm->ffi_handles_cap) {
                        vm->ffi_handles[vm->ffi_handles_count++] = (void*)h;
                    }
                }
#else
                void* h = dlopen(path, RTLD_LAZY);
                if (h) {
                    handle_val = (uint64_t)(uintptr_t)h;
                    if (vm->ffi_handles_count >= vm->ffi_handles_cap) {
                        size_t new_cap = vm->ffi_handles_cap ? vm->ffi_handles_cap * 2 : 8;
                        void** p = (void**)realloc(vm->ffi_handles, new_cap * sizeof(void*));
                        if (p) {
                            vm->ffi_handles = p;
                            vm->ffi_handles_cap = new_cap;
                        }
                    }
                    if (vm->ffi_handles && vm->ffi_handles_count < vm->ffi_handles_cap) {
                        vm->ffi_handles[vm->ffi_handles_count++] = h;
                    }
                }
#endif
            }
            vm_set_register(vm, result_reg, handle_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FFI_OBTENER_SIMBOLO: {
            /* A = reg resultado (función), B = reg handle, C = reg con offset en data del nombre del símbolo */
            uint64_t handle = vm_get_register(vm, inst.operand_b);
            uint64_t offset_val = vm_get_register(vm, inst.operand_c);
            uint64_t fn_val = 0;
            if (handle && vm->ir && vm->ir->data && (size_t)offset_val < vm->ir->header.data_size) {
                const char* name = (const char*)vm->ir->data + (size_t)offset_val;
#if defined(_WIN32) || defined(_WIN64)
                FARPROC fn = GetProcAddress((HMODULE)(uintptr_t)handle, name);
                if (fn) fn_val = (uint64_t)(uintptr_t)fn;
#else
                void* fn = dlsym((void*)(uintptr_t)handle, name);
                if (fn) fn_val = (uint64_t)(uintptr_t)fn;
#endif
            }
            vm_set_register(vm, inst.operand_a, fn_val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FFI_LLAMAR: {
            /* A = reg resultado, B = reg con puntero a función, C = num_args (0-4); args en regs 3,4,5,6 */
            typedef uint64_t (*FFIFunc4)(uint64_t, uint64_t, uint64_t, uint64_t);
            uint64_t fn_ptr = vm_get_register(vm, inst.operand_b);
            unsigned n = (unsigned)inst.operand_c;
            if (n > 4) n = 4;
            uint64_t a0 = n > 0 ? vm_get_register(vm, 3) : 0;
            uint64_t a1 = n > 1 ? vm_get_register(vm, 4) : 0;
            uint64_t a2 = n > 2 ? vm_get_register(vm, 5) : 0;
            uint64_t a3 = n > 3 ? vm_get_register(vm, 6) : 0;
            uint64_t result = 0;
            if (fn_ptr) {
                FFIFunc4 fn = (FFIFunc4)(uintptr_t)fn_ptr;
                result = fn(a0, a1, a2, a3);
            }
            vm_set_register(vm, inst.operand_a, result);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }




        case OP_IO_INPUT_REG: {
            char buffer[256];
            if (feof(stdin)) clearerr(stdin);
            if (ferror(stdin)) clearerr(stdin);
            fflush(stdout);
            if (fgets(buffer, sizeof(buffer), stdin)) {
                size_t len = strlen(buffer);
                if (len > 0 && buffer[len - 1] == '\n') buffer[len - 1] = '\0';
                uint32_t new_id = 0;
                int is_numeric = 1;
                char* endptr;
                long long val = strtoll(buffer, &endptr, 10);
                if (*buffer == '\0' || *endptr != '\0') is_numeric = 0;
                new_id = vm_hash_texto(buffer);
                vm_text_cache_put(vm, new_id, buffer);
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal) {
                    jmn_guardar_texto(vm->mem_neuronal, new_id, buffer);
                    if (!jmn_obtener_nodo(vm->mem_neuronal, new_id)) {
                        JMNValor val_uno = { .f = 1.0f };
                        jmn_agregar_nodo(vm->mem_neuronal, new_id, val_uno);
                    }
                }
#endif
                /* Siempre id de cadena: `ingresar_texto` declara tipo texto; antes, solo dígitos guardaban
                 * el entero (p. ej. "0" -> 0) y comparaciones con "0" o imprimir como texto fallaban. */
                vm_set_register(vm, inst.operand_a, (uint64_t)new_id);
                vm_percepcion_push(vm, is_numeric ? (uint32_t)val : new_id);
            } else {
                if (vm->modo_continuo) {
                    uint32_t vacio_id = 5381; /* cadena vacía pre-poblada */
                    vm_set_register(vm, inst.operand_a, (uint64_t)vacio_id);
                    vm_percepcion_push(vm, vacio_id);
                } else {
                    vm->running = 0;
                }
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_CONV_I2F: {
            union { uint64_t u64; float f32; } cast = {0};
            cast.f32 = (float)((int64_t)b_val);
            vm_set_register(vm, inst.operand_a, cast.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_CONV_F2I: {
            union { uint64_t u64; float f32; } cast = {0};
            cast.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            vm_set_register(vm, inst.operand_a, (uint64_t)cast.f32);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_RAIZ: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = (bf.f32 >= 0.0f) ? sqrtf(bf.f32) : 0.0f;
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_SIN: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = sinf(bf.f32);
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_COS: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = cosf(bf.f32);
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_TAN: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = tanf(bf.f32);
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_ATAN2: {
            union { uint64_t u64; float f32; } bf = {0}, cf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            cf.u64 = (uint32_t)(c_val & 0xFFFFFFFF);
            float r = atan2f(bf.f32, cf.f32);
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT4_MUL_VEC4: {
            size_t addr_dest = (size_t)vm_get_register(vm, inst.operand_a);
            size_t addr_mat  = (size_t)vm_get_register(vm, inst.operand_b);
            size_t addr_vec  = (size_t)vm_get_register(vm, inst.operand_c);
            float M[16], v[4], out[4];
            for (int i = 0; i < 16; i++) M[i] = vm_mem_read_float(vm, addr_mat + (size_t)(i * 8));
            for (int i = 0; i < 4; i++)  v[i] = vm_mem_read_float(vm, addr_vec + (size_t)(i * 8));
            for (int i = 0; i < 4; i++) {
                out[i] = M[i*4+0]*v[0] + M[i*4+1]*v[1] + M[i*4+2]*v[2] + M[i*4+3]*v[3];
            }
            for (int i = 0; i < 4; i++) vm_mem_write_float(vm, addr_dest + (size_t)(i * 8), out[i]);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT4_MUL: {
            size_t addr_dest = (size_t)vm_get_register(vm, inst.operand_a);
            size_t addr_L    = (size_t)vm_get_register(vm, inst.operand_b);
            size_t addr_R    = (size_t)vm_get_register(vm, inst.operand_c);
            float L[16], R[16], out[16];
            for (int i = 0; i < 16; i++) L[i] = vm_mem_read_float(vm, addr_L + (size_t)(i * 8));
            for (int i = 0; i < 16; i++) R[i] = vm_mem_read_float(vm, addr_R + (size_t)(i * 8));
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) {
                    out[i*4+j] = L[i*4+0]*R[0*4+j] + L[i*4+1]*R[1*4+j] + L[i*4+2]*R[2*4+j] + L[i*4+3]*R[3*4+j];
                }
            for (int i = 0; i < 16; i++) vm_mem_write_float(vm, addr_dest + (size_t)(i * 8), out[i]);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_EXP: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = expf(bf.f32);
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_LOG: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = (bf.f32 > 0.0f) ? logf(bf.f32) : 0.0f;
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_LOG10: {
            union { uint64_t u64; float f32; } bf = {0};
            bf.u64 = (uint32_t)(b_val & 0xFFFFFFFF);
            float r = (bf.f32 > 0.0f) ? log10f(bf.f32) : 0.0f;
            union { uint64_t u64; float f32; } rf = {0};
            rf.f32 = r;
            vm_set_register(vm, inst.operand_a, rf.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT4_IDENTIDAD: {
            size_t addr = (size_t)vm_get_register(vm, inst.operand_a);
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    vm_mem_write_float(vm, addr + (size_t)((i*4+j) * 8), (i == j) ? 1.0f : 0.0f);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT4_TRANSPUESTA: {
            size_t addr_dest = (size_t)vm_get_register(vm, inst.operand_a);
            size_t addr_src  = (size_t)vm_get_register(vm, inst.operand_b);
            float M[16], T[16];
            for (int i = 0; i < 16; i++) M[i] = vm_mem_read_float(vm, addr_src + (size_t)(i * 8));
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) T[i*4+j] = M[j*4+i];
            for (int i = 0; i < 16; i++) vm_mem_write_float(vm, addr_dest + (size_t)(i * 8), T[i]);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT4_INVERSA: {
            size_t addr_dest = (size_t)vm_get_register(vm, inst.operand_a);
            size_t addr_src  = (size_t)vm_get_register(vm, inst.operand_b);
            float A[16], inv[16];
            for (int i = 0; i < 16; i++) A[i] = vm_mem_read_float(vm, addr_src + (size_t)(i * 8));
            /* Gauss-Jordan: [A|I] -> [I|A^-1] */
            for (int i = 0; i < 16; i++) inv[i] = (i % 5 == 0) ? 1.0f : 0.0f; /* I */
            int inv_ok = 1;
            for (int col = 0; col < 4; col++) {
                int pivot = -1;
                float maxv = 0.0f;
                for (int row = col; row < 4; row++) {
                    float v = A[row*4+col];
                    if (v < 0.0f) v = -v;
                    if (v > maxv) { maxv = v; pivot = row; }
                }
                if (pivot < 0 || maxv < 1e-10f) { inv_ok = 0; break; }
                for (int j = 0; j < 4; j++) {
                    float t = A[col*4+j]; A[col*4+j] = A[pivot*4+j]; A[pivot*4+j] = t;
                    t = inv[col*4+j]; inv[col*4+j] = inv[pivot*4+j]; inv[pivot*4+j] = t;
                }
                float diag = A[col*4+col];
                if (diag < 1e-10f && diag > -1e-10f) { inv_ok = 0; break; }
                for (int j = 0; j < 4; j++) {
                    A[col*4+j] /= diag;
                    inv[col*4+j] /= diag;
                }
                for (int row = 0; row < 4; row++) {
                    if (row == col) continue;
                    float fac = A[row*4+col];
                    for (int j = 0; j < 4; j++) {
                        A[row*4+j] -= fac * A[col*4+j];
                        inv[row*4+j] -= fac * inv[col*4+j];
                    }
                }
            }
            if (inv_ok) {
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        float want = (i == j) ? 1.0f : 0.0f;
                        float g = A[i*4+j];
                        float d = g - want;
                        if (d < 0.0f) d = -d;
                        if (d > 1e-3f) { inv_ok = 0; break; }
                    }
                    if (!inv_ok) break;
                }
            }
            if (!inv_ok) {
                const char *minv = "Error de ejecucion (VM): mat4_inversa: matriz singular o mal condicionada.";
                if (vm_try_catch_or_abort(vm, minv)) return 0;
                fprintf(stderr, "%s\n", minv);
                vm->exit_code = 1;
                vm->running = 0;
                return 0;
            }
            for (int i = 0; i < 16; i++) vm_mem_write_float(vm, addr_dest + (size_t)(i * 8), inv[i]);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT3_MUL_VEC3: {
            size_t addr_dest = (size_t)vm_get_register(vm, inst.operand_a);
            size_t addr_mat  = (size_t)vm_get_register(vm, inst.operand_b);
            size_t addr_vec  = (size_t)vm_get_register(vm, inst.operand_c);
            float M[9], v[3], out[3];
            for (int i = 0; i < 9; i++) M[i] = vm_mem_read_float(vm, addr_mat + (size_t)(i * 8));
            for (int i = 0; i < 3; i++) v[i] = vm_mem_read_float(vm, addr_vec + (size_t)(i * 8));
            for (int i = 0; i < 3; i++)
                out[i] = M[i*3+0]*v[0] + M[i*3+1]*v[1] + M[i*3+2]*v[2];
            for (int i = 0; i < 3; i++) vm_mem_write_float(vm, addr_dest + (size_t)(i * 8), out[i]);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        case OP_MAT3_MUL: {
            size_t addr_dest = (size_t)vm_get_register(vm, inst.operand_a);
            size_t addr_L    = (size_t)vm_get_register(vm, inst.operand_b);
            size_t addr_R    = (size_t)vm_get_register(vm, inst.operand_c);
            float L[9], R[9], out[9];
            for (int i = 0; i < 9; i++) L[i] = vm_mem_read_float(vm, addr_L + (size_t)(i * 8));
            for (int i = 0; i < 9; i++) R[i] = vm_mem_read_float(vm, addr_R + (size_t)(i * 8));
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    out[i*3+j] = L[i*3+0]*R[0*3+j] + L[i*3+1]*R[1*3+j] + L[i*3+2]*R[2*3+j];
            for (int i = 0; i < 9; i++) vm_mem_write_float(vm, addr_dest + (size_t)(i * 8), out[i]);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IO_INGRESAR_TEXTO: {
            size_t offset = (size_t)inst.operand_a | ((size_t)inst.operand_b << 8) | ((size_t)inst.operand_c << 16);
            if (vm->ir && vm->ir->data && offset + 8 <= vm->ir->header.data_size) {
                 uint32_t dummy_id = 0, dest_addr = 0;
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset, &dummy_id);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset + 4, &dest_addr);
                 char buffer[256];
                 if (fgets(buffer, sizeof(buffer), stdin)) {
                     size_t len = strlen(buffer);
                     while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) { buffer[len-1] = '\0'; len--; }
                     uint32_t new_id = vm_hash_texto(buffer);
                     vm_text_cache_put(vm, new_id, buffer);
#ifdef JASBOOT_LANG_INTEGRATION
                     if (vm->mem_neuronal) {
                         jmn_guardar_texto(vm->mem_neuronal, new_id, buffer);
                         if (!jmn_obtener_nodo(vm->mem_neuronal, new_id)) {
                             JMNValor v_uno = { .f = 1.0f };
                             jmn_agregar_nodo(vm->mem_neuronal, new_id, v_uno);
                         }
                     }
#endif
                     if (dest_addr + sizeof(uint64_t) <= vm->memory_size) *(uint64_t*)(vm->memory + dest_addr) = (uint64_t)new_id;
                 } else {
                     if (vm->modo_continuo && dest_addr + sizeof(uint64_t) <= vm->memory_size)
                         *(uint64_t*)(vm->memory + dest_addr) = 5381; /* "" */
                     else if (!vm->modo_continuo)
                         vm->running = 0;
                 }
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IO_PERCIBIR_TECLADO: {
            /* Lectura no bloqueante stdin: A <- string ("" si no hay entrada) */
            char buf[2] = {0};
            int hay = 0;
#if defined(_WIN32) || defined(_WIN64)
            if (_kbhit()) {
                int c = _getch();
                if (c != EOF && c != '\0') { buf[0] = (char)c; hay = 1; }
            }
#else
            fd_set fds;
            struct timeval tv = {0, 0};
            FD_ZERO(&fds);
            FD_SET(0, &fds);
            if (select(1, &fds, NULL, NULL, &tv) > 0 && FD_ISSET(0, &fds)) {
                int c = getchar();
                if (c != EOF) { buf[0] = (char)c; hay = 1; }
            }
#endif
            if (hay) {
                uint32_t id = vm_hash_texto(buf);
                vm_text_cache_put(vm, id, buf);
                vm_set_register(vm, inst.operand_a, (uint64_t)id);
            } else {
                vm_set_register(vm, inst.operand_a, 5381); /* "" */
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IO_ENTRADA_FLOTANTE: {
            float f = vm_io_entrada_flotante_leer();
            union {
                uint64_t u64;
                float f32;
            } u = {0};
            u.f32 = f;
            vm_set_register(vm, inst.operand_a, u.u64);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IMPRIMIR_TEXTO: {
            uint32_t id = 0;
            if (inst.flags & IR_INST_FLAG_A_REGISTER) {
                id = (uint32_t)vm_get_register(vm, inst.operand_a);
                if (id == 0) {
                    vm_escribir_cadena("indefinido");
                    vm->pc += IR_INSTRUCTION_SIZE;
                    break;
                }
            } else if ((inst.flags & (IR_INST_FLAG_B_IMMEDIATE | IR_INST_FLAG_C_IMMEDIATE)) == 0) {
                // Si no hay flags de inmediato B/C, tratar A como registro por fallback
                id = (uint32_t)vm_get_register(vm, inst.operand_a);
                if (id == 0) {
                    vm_escribir_cadena("indefinido");
                    vm->pc += IR_INSTRUCTION_SIZE;
                    break;
                }
            }

            if (id != 0 || (inst.flags & IR_INST_FLAG_A_REGISTER)) {
                VMTextCacheEntry* ent = vm_text_cache_find(vm, id);
                if (ent && ent->text) {
                    vm_escribir_texto_bytes(ent->text, ent->text_len);
                } else {
                    char txt_buf[4096];
#ifdef JASBOOT_LANG_INTEGRATION
                    if (vm->mem_neuronal && jmn_obtener_texto(vm->mem_neuronal, id, txt_buf, sizeof(txt_buf)) >= 0 && txt_buf[0]) {
                        vm_escribir_cadena(txt_buf);
                    } else
#endif
                    {
                        char buf[32];
                        sprintf(buf, "<ID:%08X>", id);
                        vm_escribir_cadena(buf);
                    }
                }
            } else {
                // Literal de la sección de datos
                size_t offset = (size_t)inst.operand_a | ((size_t)inst.operand_b << 8) | ((size_t)inst.operand_c << 16);
                if (vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                    vm_escribir_cadena((const char*)vm->ir->data + offset);
                }
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_DIVIDIR_TEXTO: {
            uint32_t id_txt = (uint32_t)vm_get_register(vm, inst.operand_b);
            uint32_t id_sep = (uint32_t)vm_get_register(vm, inst.operand_c);
            uint32_t list_id = 0;

            const char* texto = vm_text_cache_get(vm, id_txt);
            const char* sep = vm_text_cache_get(vm, id_sep);
            
            if (texto && sep) {
                list_id = vm_hash_texto(texto) ^ vm_hash_texto(sep) ^ 0x5C5C5C5C;
#ifdef JASBOOT_LANG_INTEGRATION
                ensure_jmn_col(vm);
                if (vm->mem_colecciones) {
                    jmn_crear_lista(vm->mem_colecciones, list_id);
                    char token_norm[512];
                    size_t sep_len = strlen(sep);
                    const char* p = texto;
                    if (sep_len == 0) {
                        p = texto;
                        const char* end = p + strlen(p);
                        while (*p && ispunct((unsigned char)*p)) p++;
                        while (end > p && ispunct((unsigned char)*(end - 1))) end--;
                        size_t len = (size_t)(end - p);
                        if (len >= sizeof(token_norm)) len = sizeof(token_norm) - 1;
                        memcpy(token_norm, p, len);
                        token_norm[len] = '\0';
                        if (len > 0) {
                            uint32_t token_id = vm_hash_texto(token_norm);
                            vm_text_cache_put(vm, token_id, token_norm);
                            JMNValor v_val; v_val.u = token_id;
                            jmn_lista_agregar(vm->mem_colecciones, list_id, v_val);
                            if (vm->mem_neuronal)
                                jmn_guardar_texto(vm->mem_neuronal, token_id, token_norm);
                        }
                    } else for (;;) {
                        const char* found = strstr(p, sep);
                        const char* seg_end = found ? found : texto + strlen(texto);
                        const char* q = p;
                        while (q < seg_end && ispunct((unsigned char)*q)) q++;
                        const char* end = seg_end;
                        while (end > q && ispunct((unsigned char)*(end - 1))) end--;
                        size_t len = (size_t)(end - q);
                        if (len >= sizeof(token_norm)) len = sizeof(token_norm) - 1;
                        memcpy(token_norm, q, len);
                        token_norm[len] = '\0';
                        if (len > 0) {
                            uint32_t token_id = vm_hash_texto(token_norm);
                            vm_text_cache_put(vm, token_id, token_norm);
                            JMNValor v_val; v_val.u = token_id;
                            jmn_lista_agregar(vm->mem_colecciones, list_id, v_val);
                            if (vm->mem_neuronal)
                                jmn_guardar_texto(vm->mem_neuronal, token_id, token_norm);
                        }
                        if (!found) break;
                        p = found + sep_len;
                    }
                }
#endif
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)list_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IMPRIMIR_NUMERO: {
            uint64_t val = vm_get_register(vm, inst.operand_a);
            if (val == VM_U64_CONV_ENTERO_INVALIDO)
                vm_escribir_cadena("nan");
            else if (!vm_escribir_si_timestamp(val))
                vm_escribir_entero((int64_t)val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_IMPRIMIR_CONCEPTO: {
            size_t offset = (size_t)inst.operand_a | ((size_t)inst.operand_b << 8) | ((size_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            uint32_t id = 0, fallback = 0;
            int impreso = 0;
            if (vm->ir && vm->ir->data && offset + 8 <= vm->ir->header.data_size) {
                if (vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset, &id) == 0 &&
                    vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset + 4, &fallback) == 0) {
                    if (vm->mem_neuronal) impreso = jmn_imprimir_texto(vm->mem_neuronal, id);
                    if (!impreso) {
                        const char* cached = vm_text_cache_get(vm, id);
                        if (cached && cached[0]) { vm_escribir_cadena(cached); vm->pc += IR_INSTRUCTION_SIZE; break; }
                        if (fallback < vm->ir->header.data_size) vm_escribir_cadena((const char*)vm->ir->data + fallback);
                    }
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        
        case OP_MEM_IMPRIMIR_ID: {
            uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_a);
            int impreso = 0;

#ifdef JASBOOT_LANG_INTEGRATION
            // 0. Prioridad: cache y JMN texto (evitar {3:1} cuando el texto sí existe)
            const char* cached_name = vm_text_cache_get(vm, id);
            if (cached_name && cached_name[0]) {
                vm_escribir_cadena(cached_name);
                impreso = 1;
            }
            if (!impreso && vm->mem_neuronal && id != 0) {
                char buf_txt[512];
                if (jmn_obtener_texto(vm->mem_neuronal, id, buf_txt, sizeof(buf_txt)) >= 0 && buf_txt[0]) {
                    vm_text_cache_put(vm, id, buf_txt);
                    vm_escribir_cadena(buf_txt);
                    impreso = 1;
                }
            }
            if (impreso) {
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            }

            // 1. Intentar colecciones (listas/mapas/vectores)
            JMNMemoria* m_col = vm->mem_colecciones;
            JMNMemoria* m_neu = vm->mem_neuronal;
            
            JMNMemoria* memorias[] = { m_col, m_neu };
            for (int m = 0; m < 2; m++) {
                if (impreso || !memorias[m]) continue;
                
                uint32_t tam = jmn_lista_tamano(memorias[m], id);
                JMNNodo* nodo = jmn_obtener_nodo(memorias[m], id);
                
                if (tam > 0 || nodo) {
                    /* Prioridad: mostrar texto del concepto, nunca el mapa interno {1: 1} */
                    int imprimir_brackets = 1;
                    int es_mapa = 0;
                    uint32_t c_count = 0;
                    JMNConexion* conexiones = jmn_obtener_conexiones(memorias[m], nodo, &c_count);
                    if (conexiones && c_count > 0) {
                        for (uint32_t j = 0; j < c_count; j++) {
                            uint32_t kid = conexiones[j].key_id;
                            if (kid != 0 && (kid < 0x10000000 || kid > 0x1000FFFF)) {
                                es_mapa = 1;
                                break;
                            }
                        }
                    }
                    if (cached_name && cached_name[0]) {
                        vm_escribir_cadena(cached_name);
                        impreso = 1;
                        imprimir_brackets = 0;
                    } else if (m == 1 && tam == 0 && memorias[m]) {
                        /* Concepto en JMN: intentar obtener texto (cache, JMN) antes de mapa */
                        char buf_texto[512];
                        if (jmn_obtener_texto(memorias[m], id, buf_texto, sizeof(buf_texto)) >= 0 && buf_texto[0]) {
                            vm_text_cache_put(vm, id, buf_texto);
                            vm_escribir_cadena(buf_texto);
                            impreso = 1;
                            imprimir_brackets = 0;
                        }
                    }
                    /* Solo imprimir mapa/lista si no es un concepto con texto (evitar {1: 1}) */
                    if (m == 1 && tam == 0 && es_mapa) {
                        /* Intentar texto del primer destino con tipo SECUENCIA/ASOCIACION antes de "?" */
                        int fallback_ok = 0;
                        for (uint32_t j = 0; j < c_count && !fallback_ok; j++) {
                            uint32_t dest = conexiones[j].destino_id;
                            if (dest == 0 || dest == id) continue;
                            char buf_dest[512];
                            if (jmn_obtener_texto(memorias[m], dest, buf_dest, sizeof(buf_dest)) >= 0 && buf_dest[0]) {
                                vm_escribir_cadena(buf_dest);
                                fallback_ok = 1;
                            }
                        }
                        if (!fallback_ok) vm_escribir_cadena("?");
                        impreso = 1;
                        imprimir_brackets = 0;
                    }
                    if (imprimir_brackets) {
                    /* Evitar {1:1} {3:1}: si es mapa interno, intentar texto de destinos; si no hay, imprimir ? */
                    if (es_mapa && c_count > 0) {
                        int fallback_ok = 0;
                        for (uint32_t j = 0; j < c_count && j < 64 && !fallback_ok; j++) {
                            uint32_t dest = conexiones[j].destino_id;
                            if (dest == 0 || dest == id) continue;
                            char buf_dest[512];
                            if (m_neu && jmn_obtener_texto(m_neu, dest, buf_dest, sizeof(buf_dest)) >= 0 && buf_dest[0]) {
                                vm_escribir_cadena(buf_dest);
                                fallback_ok = 1;
                            } else if (memorias[m] && jmn_obtener_texto(memorias[m], dest, buf_dest, sizeof(buf_dest)) >= 0 && buf_dest[0]) {
                                vm_escribir_cadena(buf_dest);
                                fallback_ok = 1;
                            }
                        }
                        if (!fallback_ok) {
                            vm_escribir_cadena("?");
                            fallback_ok = 1;
                        }
                        if (fallback_ok) {
                            impreso = 1;
                            imprimir_brackets = 0;
                        }
                    }
                    if (imprimir_brackets) {
                    vm_escribir_cadena(es_mapa ? "{" : "[");
                    if (es_mapa) {
                        int first = 1;
                        for (uint32_t j = 0; j < c_count; j++) {
                            if (conexiones[j].key_id == 0 || conexiones[j].destino_id == 0) continue;
                            if (!first) vm_escribir_cadena(", ");
                            first = 0;
                            
                            // Imprimir Clave
                            const char* k_txt = vm_text_cache_get(vm, conexiones[j].key_id);
                            if (k_txt) vm_escribir_cadena(k_txt);
                            else { char b[32]; snprintf(b, sizeof(b), "%u", conexiones[j].key_id); vm_escribir_cadena(b); }
                            
                            vm_escribir_cadena(": ");
                            
                            // Imprimir Valor
                            uint32_t item_node_id = conexiones[j].destino_id;
                            JMNNodo* v_nodo = jmn_obtener_nodo(memorias[m], item_node_id);
                            uint32_t val_id = v_nodo ? v_nodo->peso.u : 0;
                            const char* v_txt = val_id ? vm_text_cache_get(vm, val_id) : NULL;
                            char v_buf[512];
                            if (!v_txt && val_id && m_neu && jmn_obtener_texto(m_neu, val_id, v_buf, sizeof(v_buf)) >= 0 && v_buf[0])
                                v_txt = v_buf;
                            if (v_txt) {
                                vm_escribir_cadena("\""); vm_escribir_cadena(v_txt); vm_escribir_cadena("\"");
                            } else {
                                JMNValor weight = v_nodo ? v_nodo->peso : (JMNValor){0};
                                char b[64];
                                // Heurística: si parece un ID de texto o un float razonable
                                if (weight.u > 0 && weight.u < 1000000) {
                                    snprintf(b, sizeof(b), "%u", weight.u);
                                } else if (weight.f == (float)((long long)weight.f)) {
                                    snprintf(b, sizeof(b), "%lld", (long long)weight.f);
                                } else {
                                    snprintf(b, sizeof(b), "%.2f", weight.f);
                                }
                                vm_escribir_cadena(b);
                            }
                        }
                    } else {
                        // Es una lista tradicional
                        for (uint32_t i = 0; i < tam; i++) {
                            JMNValor v = jmn_lista_obtener(memorias[m], id, i);
                            const char* v_txt = v.u ? vm_text_cache_get(vm, v.u) : NULL;
                            char v_li[512];
                            if (!v_txt && v.u && m_neu && jmn_obtener_texto(m_neu, v.u, v_li, sizeof(v_li)) >= 0 && v_li[0])
                                v_txt = v_li;
                            if (v_txt) {
                                vm_escribir_cadena("\""); vm_escribir_cadena(v_txt); vm_escribir_cadena("\"");
                            } else {
                                char b[64];
                                if (v.u > 0 && v.u < 1000000) {
                                    snprintf(b, sizeof(b), "%u", v.u);
                                } else if (v.f == (float)((long long)v.f)) {
                                    snprintf(b, sizeof(b), "%lld", (long long)v.f);
                                } else {
                                    snprintf(b, sizeof(b), "%.2f", v.f);
                                }
                                vm_escribir_cadena(b);
                            }
                            if (i < tam - 1) vm_escribir_cadena(", ");
                            if (i >= 50) { vm_escribir_cadena("..."); break; }
                        }
                    }
                    vm_escribir_cadena(es_mapa ? "}" : "]");
                    impreso = 1;
                    }
                    }
                }
            }

            // 2. Intentar texto en memoria neuronal cognitiva si no se imprimió como colección (ids hash suelen ser < 0x10000)
            if (!impreso && m_neu && id != 0) {
                char buffer[4096];
                buffer[0] = '\0';
                if (jmn_obtener_texto(m_neu, id, buffer, sizeof(buffer)) >= 0 && buffer[0]) {
                    vm_text_cache_put(vm, id, buffer);
                    vm_escribir_cadena(buffer);
                    impreso = 1;
                }
            }
#endif
            // 3. Intentar texto en cache (literal o IDs registrados)
            if (!impreso) {
                const char* cached = vm_text_cache_get(vm, id);
                if (cached && cached[0]) {
                    vm_escribir_cadena(cached);
                    impreso = 1;
                }
            }

            // 4. Fallback: Heurística de flotante vs entero
            if (!impreso) {
                uint32_t exp = (id >> 23) & 0xFF;
                if (id > 1000000 && exp > 0x30 && exp < 0xA0) {
                    union { uint32_t u; float f; } cast;
                    cast.u = id;
                    vm_escribir_flotante(cast.f);
                } else {
                    vm_escribir_entero((uint64_t)id);
                }
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_COMPARAR_TEXTO: {
            // Comparar IDs (hashes) directamente es suficiente para igualdad
            vm_set_register(vm, inst.operand_a, (b_val == c_val) ? 1 : 0);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_PROCESAR_TEXTO: {
            uint32_t id = 0;
            if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                size_t offset = (size_t)inst.operand_a | ((size_t)inst.operand_b << 8) | ((size_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal && vm->ir && vm->ir->data && offset + 4 <= vm->ir->header.data_size) vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset, &id);
#endif
            } else id = (uint32_t)vm->registers[inst.operand_a];
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && id != 0) jmn_procesar_texto(vm->mem_neuronal, id);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_CONTIENE_TEXTO: {
            uint32_t off24 = (uint32_t)inst.operand_b | ((uint32_t)inst.operand_c << 8) | ((uint32_t)inst.flags << 16);
            uint64_t res = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 8 <= vm->ir->header.data_size) {
                uint32_t id_frase = 0, id_patron = 0;
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_frase);
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 4, &id_patron);
                char frase[1024] = {0}, patron[256] = {0};
                int got_f = 0, got_p = 0;
                const char* cf = vm_text_cache_get(vm, id_frase);
                const char* cp = vm_text_cache_get(vm, id_patron);
                if (cf) {
                    strncpy(frase, cf, sizeof(frase) - 1);
                    got_f = 1;
                } else if (jmn_obtener_texto(vm->mem_neuronal, id_frase, frase, sizeof(frase)) >= 0 && frase[0])
                    got_f = 1;
                if (cp) {
                    strncpy(patron, cp, sizeof(patron) - 1);
                    got_p = 1;
                } else if (jmn_obtener_texto(vm->mem_neuronal, id_patron, patron, sizeof(patron)) >= 0 && patron[0])
                    got_p = 1;
                if (got_f && got_p)
                    res = strstr(frase, patron) ? 1u : 0u;
            }
#endif
            vm_set_register(vm, inst.operand_a, res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_TERMINA_CON: {
            uint32_t off24 = (uint32_t)inst.operand_b | ((uint32_t)inst.operand_c << 8) | ((uint32_t)inst.flags << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 8 <= vm->ir->header.data_size) {
                uint32_t id_frase = 0, id_sufijo = 0;
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_frase);
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 4, &id_sufijo);
                vm->registers[inst.operand_a] = (uint64_t)jmn_termina_con(vm->mem_neuronal, id_frase, id_sufijo);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_CONTIENE_TEXTO_REG: {
            uint32_t id_frase = (uint32_t)vm_get_register(vm, inst.operand_b);
            uint32_t id_patron = (uint32_t)vm_get_register(vm, inst.operand_c);
            uint64_t res = 0;
            
            // Try cache first
            const char* s_f = vm_text_cache_get(vm, id_frase);
            const char* s_p = vm_text_cache_get(vm, id_patron);
            if (s_f && s_p) {
                if (strstr(s_f, s_p)) res = 1;
            } else {
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal) {
                    char frase[1024] = {0}, patron[256] = {0};
                    if (jmn_obtener_texto(vm->mem_neuronal, id_frase, frase, 1024) >= 0 && 
                        jmn_obtener_texto(vm->mem_neuronal, id_patron, patron, 256) >= 0) {
                        res = strstr(frase, patron) ? 1 : 0;
                    }
                }
#endif
            }
            vm_set_register(vm, inst.operand_a, res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_TERMINA_CON_REG: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint32_t id_frase = (uint32_t)vm_get_register(vm, inst.operand_b), id_sufijo = (uint32_t)vm_get_register(vm, inst.operand_c);
                vm->registers[inst.operand_a] = (uint64_t)jmn_termina_con(vm->mem_neuronal, id_frase, id_sufijo);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_APRENDER_CONCEPTO: {
            uint32_t concepto_id = (uint32_t)a_val;
            uint64_t val = b_val;
            
            // Tratar val como entero 0-100
            float peso = (float)val / 100.0f;
            if (peso > 1.0f) peso = 1.0f;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                /* Sincronizar texto de cache a JMN para que jmn_obtener_texto/responder lo encuentre */
                const char* txt = vm_text_cache_get(vm, concepto_id);
                if (txt && txt[0]) jmn_guardar_texto(vm->mem_neuronal, concepto_id, txt);
                JMNNodo* nodo = jmn_obtener_nodo(vm->mem_neuronal, concepto_id);
                if (nodo) nodo->peso.f = peso;
                else {
                    JMNValor v_peso;
                    v_peso.f = peso;
                    jmn_agregar_nodo(vm->mem_neuronal, concepto_id, v_peso);
                }
            }
#endif
            vm_percepcion_push(vm, concepto_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_BUSCAR_CONCEPTO: {
            uint32_t concepto_id = (uint32_t)a_val;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNNodo* nodo = jmn_obtener_nodo(vm->mem_neuronal, concepto_id);
                // Retornar valor real (entero 0-100) para mayor compatibilidad
                uint64_t valor = (nodo ? (uint64_t)(nodo->peso.f * 100.0f) : 0);
                vm_set_register(vm, inst.operand_b, valor);
            } else vm_set_register(vm, inst.operand_b, 0);
#else
            vm_set_register(vm, inst.operand_b, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ASOCIAR_CONCEPTOS: {
            uint32_t id1 = (uint32_t)a_val;
            uint32_t id2 = (uint32_t)b_val;
            
            float peso = 0.0f;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                peso = (float)inst.operand_c / 100.0f;
            } else {
                // Registro: Tratar como bits IEEE754 (Union cast)
                union { uint64_t u64; float f32; } u = { .u64 = (uint32_t)c_val };
                peso = u.f32;
            }

#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNValor v_media = { .f = 0.5f };
                if (!jmn_obtener_nodo(vm->mem_neuronal, id1)) jmn_agregar_nodo(vm->mem_neuronal, id1, v_media);
                if (!jmn_obtener_nodo(vm->mem_neuronal, id2)) jmn_agregar_nodo(vm->mem_neuronal, id2, v_media);
                /* Sincronizar cadenas del cache a JMN para que buscar/imprimir_id resuelvan el texto tras recordar. */
                {
                    const char* t1 = vm_text_cache_get(vm, id1);
                    if (t1 && t1[0]) jmn_guardar_texto(vm->mem_neuronal, id1, t1);
                    const char* t2 = vm_text_cache_get(vm, id2);
                    if (t2 && t2[0]) jmn_guardar_texto(vm->mem_neuronal, id2, t2);
                }
                JMNValor v_peso; v_peso.f = peso;
                jmn_agregar_conexion(vm->mem_neuronal, id1, id2, v_peso, 1);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ACTUALIZAR_PESO: {
            uint32_t id = (uint32_t)a_val;
            float peso = 0.0f;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                peso = (float)inst.operand_b / 100.0f;
            } else {
                union { uint64_t u64; float f32; } u = { .u64 = (uint32_t)b_val };
                peso = u.f32;
            }

#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNNodo* nodo = jmn_obtener_nodo(vm->mem_neuronal, id);
                if (nodo) nodo->peso.f = peso;
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_OBTENER_ASOCIACIONES: {
            uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_a);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNNodo* nodo = jmn_obtener_nodo(vm->mem_neuronal, id);
                uint32_t count = 0;
                if (nodo) jmn_obtener_conexiones(vm->mem_neuronal, nodo, &count);
                vm_set_register(vm, inst.operand_b, count);
            } else vm_set_register(vm, inst.operand_b, 0);
#else
            vm_set_register(vm, inst.operand_b, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_COPIAR_TEXTO: {
            uint32_t off24 = (uint32_t)inst.operand_a | ((uint32_t)inst.operand_b << 8) | ((uint32_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 8 <= vm->ir->header.data_size) {
                uint32_t id_d = 0, id_o = 0;
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_d);
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 4, &id_o);
                jmn_copiar_texto(vm->mem_neuronal, id_o, id_d);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ULTIMA_PALABRA: {
            uint32_t off24 = (uint32_t)inst.operand_a | ((uint32_t)inst.operand_b << 8) | ((uint32_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 8 <= vm->ir->header.data_size) {
                uint32_t id_f = 0, id_d = 0;
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_f);
                vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 4, &id_d);
                jmn_ultima_palabra(vm->mem_neuronal, id_f, id_d);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ULTIMA_SILABA: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint32_t id_f = (uint32_t)vm->registers[inst.operand_a], addr = (uint32_t)inst.operand_b | ((uint32_t)inst.operand_c << 8);
                uint32_t res = jmn_ultima_silaba(vm->mem_neuronal, id_f, addr);
                if (res != 0) {
                    char buf[4096];
                    if (jmn_obtener_texto(vm->mem_neuronal, res, buf, sizeof(buf)) == 0) vm_text_cache_put(vm, res, buf);
                    if (addr + sizeof(uint64_t) <= vm->memory_size) *(uint64_t*)(vm->memory + addr) = (uint64_t)res;
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_PENSAR_RESPUESTA: {
            uint32_t id_frase = (uint32_t)vm_get_register(vm, inst.operand_b);
            uint32_t mapa_id = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && id_frase != 0) {
                JMNMemoria* mem = vm->mem_neuronal;
                JMNBusquedaResultado resultados[16];
                vm_rastro_clear(vm);
                vm_rastro_push(vm, id_frase, 1.0f);
                
                // --- FASE 2: Parámetros Configurables ---
                float umbral = 0.1f;
                int profundidad = 2;
                
                if (c_val > 0) {
                    // Byte bajo (0-7): Creatividad/Profundidad (1-5)
                    uint8_t c_creativity = (uint8_t)(c_val & 0xFF);
                    // Byte alto (8-15): Umbral (0-100)
                    uint8_t c_threshold = (uint8_t)((c_val >> 8) & 0xFF);
                    
                    if (c_creativity > 0) profundidad = c_creativity;
                    if (profundidad > 5) profundidad = 5; // Clamping de seguridad
                    
                    if (c_threshold > 0) umbral = (float)c_threshold / 100.0f;
                    if (umbral > 1.0f) umbral = 1.0f;
                }
                // ----------------------------------------

                int n_res = jmn_buscar_asociaciones(mem, id_frase, 0, umbral, (uint16_t)profundidad, resultados, 16);
                for (int ri = 0; ri < n_res; ri++)
                    vm_rastro_push(vm, resultados[ri].id, resultados[ri].fuerza);
                // Ver cuántos resultados se encontraron
                // printf("[VM DEBUG] n_res=%d (Umbral=%.2f, Prof=%d)\n", n_res, umbral, profundidad);
                
                // Generar un ID de mapa dinámico
                mapa_id = id_frase ^ (uint32_t)time(NULL) ^ 0xCAFECAFE;
                jmn_crear_mapa(mem, mapa_id);
                
                char full_response[4096] = "";
                char auditoria[8000];
                int pos = 0;
                pos += snprintf(auditoria + pos, sizeof(auditoria) - pos, "--- TRAZA DE RAZONAMIENTO COGNITIVO ---\n");
                pos += snprintf(auditoria + pos, sizeof(auditoria) - pos, "Entrada ID: %u [U: %.2f | C: %d]\n", id_frase, umbral, profundidad);
                
                if (n_res > 0) {
                    for (int i = 0; i < n_res; i++) {
                        char t_buf[256] = "";
                        int has_text = jmn_obtener_texto(mem, resultados[i].id, t_buf, 256) > 0;
                        
                        if (has_text) {
                            // Filter out PATTERNS (Episodes/Lists) to prevent recursion
                            if (resultados[i].tipo_relacion == JMN_RELACION_PATRON) {
                                // Pattern found! Skip adding its text to response.
                                continue;
                            }

                            // Añadir al ganador solo si tiene texto legible
                            if (full_response[0] != '\0') {
                                strncat(full_response, " ", sizeof(full_response) - strlen(full_response) - 1);
                            }
                            strncat(full_response, t_buf, sizeof(full_response) - strlen(full_response) - 1);
                        } else {
                            // Fallback para auditoría únicamente
                            sprintf(t_buf, "[%u]", resultados[i].id);
                        }

                        // Documentar en auditoría
                        if (i < 8) { // Mostrar top 8 en auditoría para no saturar
                            pos += snprintf(auditoria + pos, sizeof(auditoria) - pos, "[%d] '%s' (Confianza: %.2f) [Tipo: %u]\n", 
                                          i + 1, t_buf, resultados[i].fuerza, resultados[i].tipo_relacion);
                        }
                    }
                } else {
                    pos += snprintf(auditoria + pos, sizeof(auditoria) - pos, "No se encontraron asociaciones relevantes.\n");
                }
                
                // Guardar la respuesta generada en el nodo del mapa
                if (full_response[0] != '\0') {
                    jmn_guardar_texto(mem, mapa_id, full_response);
                    vm_text_cache_put(vm, mapa_id, full_response);
                } else {
                    // Si no hay respuesta, evitar guardar basura o dejarlo vacío
                }
                
                // Imprimir la respuesta completa generada
                // printf("[VM DEBUG] Respuesta Combinada: '%s'\n", full_response);

                // Guardar en mapa cognitivo
                uint32_t key_res = vm_hash_texto("resultado_esperado");
                uint32_t key_audit = vm_hash_texto("auditoria");
                
                // El resultado es el hash de la cadena completa concatenada
                uint32_t id_full_res = vm_hash_texto(full_response);
                // También agregamos el nodo para que sea "recordable"
                JMNValor v_u = { .f = 1.0f };
                jmn_agregar_nodo(mem, id_full_res, v_u);

                JMNValor v_res; v_res.u = id_full_res;
                jmn_mapa_insertar(mem, mapa_id, key_res, v_res);
                
                uint32_t id_audit_txt = vm_hash_texto(auditoria);
                jmn_guardar_texto(mem, id_audit_txt, auditoria);
                vm_text_cache_put(vm, id_audit_txt, auditoria);

                JMNValor v_audit; v_audit.u = id_audit_txt;
                jmn_mapa_insertar(mem, mapa_id, key_audit, v_audit);
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)mapa_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }


        case OP_MEM_PENSAR: {
            // Se mantiene por compatibilidad legacy, pero redirige internamente si es necesario
            // ... (implementación anterior omitida para brevedad si no se usa)
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }


        case OP_MEM_ECO: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint32_t id_origen = (uint32_t)vm->registers[inst.operand_a];
                uint32_t dest_addr = (uint32_t)inst.operand_b | ((uint32_t)inst.operand_c << 8);
                
                // Lógica de Fase 3: Eco Inteligente / Balbuceo
                // 1. Intentar obtener la última sílaba
                uint32_t id_res = jmn_ultima_silaba(vm->mem_neuronal, id_origen, 0);

                if (id_res == 0) {
                    // 2. Si falla (palabra monosílaba o vacía), intentar palabra completa
                    id_res = jmn_ultima_palabra(vm->mem_neuronal, id_origen, 0);
                }
                
                if (id_res == 0) {
                     // 3. Si todo falla, usar identidad (eco directo)
                     id_res = id_origen;
                }
                
                // Guardar resultado en memoria
                if (dest_addr + sizeof(uint64_t) <= vm->memory_size) {
                    *(uint64_t*)(vm->memory + dest_addr) = (uint64_t)id_res;
                }
                
                // Cachear texto para optimizar visualización posterior
                char buffer[4096];
                if (jmn_obtener_texto(vm->mem_neuronal, id_res, buffer, sizeof(buffer)) >= 0) {
                    vm_text_cache_put(vm, id_res, buffer);
                }
            } else {
                // Sin memoria neuronal, comportamiento indefinido o error silencioso
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ASOCIAR: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                    // Modo Estático: offset de 24 bits
                    uint32_t off24 = (uint32_t)inst.operand_a | 
                                     ((uint32_t)inst.operand_b << 8) |
                                     ((uint32_t)inst.operand_c << 16);
                    if (vm->ir && vm->ir->data && off24 + 12 <= vm->ir->header.data_size) {
                        uint32_t id_a = 0, id_b = 0, f_bits = 0;
                        vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_a);
                        vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 4, &id_b);
                        vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 8, &f_bits);
                        float fuerza = *(float*)&f_bits;
                        // fprintf(stderr, "[VM_ASOCIAR] (off) id_a=%u id_b=%u fuerza=%.2f\n", id_a, id_b, fuerza);
                        JMNValor v_f; v_f.f = fuerza;
                        jmn_agregar_conexion(vm->mem_neuronal, id_a, id_b, v_f, 0);
                    }
                } else {
                    // Modo Dinámico: a_val=id_a, b_val=id_b, c_val=fuerza (0-100)
                    uint32_t id_origen = (uint32_t)a_val;
                    uint32_t id_destino = (uint32_t)b_val;
                    float fuerza = (float)c_val / 100.0f;
                    
                    JMNValor v_fuerza; v_fuerza.f = fuerza;
                    JMNValor v_uno = { .f = 1.0f };
                    if (!jmn_obtener_nodo(vm->mem_neuronal, id_origen)) {
                        jmn_agregar_nodo(vm->mem_neuronal, id_origen, v_uno);
                    }
                    if (!jmn_obtener_nodo(vm->mem_neuronal, id_destino)) {
                        jmn_agregar_nodo(vm->mem_neuronal, id_destino, v_uno);
                    }
                    jmn_agregar_conexion(vm->mem_neuronal, id_origen, id_destino, v_fuerza, 0);
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_PENALIZAR: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                if (inst.flags & IR_INST_FLAG_A_IMMEDIATE) {
                    // Modo Estático
                    uint32_t off24 = (uint32_t)inst.operand_a | 
                                     ((uint32_t)inst.operand_b << 8) |
                                     ((uint32_t)inst.operand_c << 16);
                    if (vm->ir && vm->ir->data && off24 + 12 <= vm->ir->header.data_size) {
                        uint32_t id_a = 0, id_b = 0, f_bits = 0;
                        vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_a);
                        vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 4, &id_b);
                        vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24 + 8, &f_bits);
                        float delta = *(float*)&f_bits;
                        // fprintf(stderr, "[VM_PENALIZAR] (off) id_a=%u id_b=%u delta=%.2f\n", id_a, id_b, delta);
                        jmn_penalizar_asociacion(vm->mem_neuronal, id_a, id_b, delta);
                    }
                } else {
                    // Modo Dinámico: a_val=id_a, b_val=id_b, c_val=delta (0-100)
                    uint32_t id_origen = (uint32_t)a_val;
                    uint32_t id_destino = (uint32_t)b_val;
                    float delta = (float)c_val / 100.0f;
                    // fprintf(stderr, "[VM_PENALIZAR] (dyn) origen=%u destino=%u delta=%.2f\n", id_origen, id_destino, delta);
                    jmn_penalizar_asociacion(vm->mem_neuronal, id_origen, id_destino, delta);
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_REFORZAR_CONCEPTO: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_a);
                uint8_t mag = inst.operand_c ? inst.operand_c : 10;
                if (mag > 100) mag = 100;
                float delta = (float)mag / 100.0f;
                if (id != 0)
                    jmn_reforzar_concepto(vm->mem_neuronal, id, delta);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_PENALIZAR_CONCEPTO: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_a);
                uint8_t mag = inst.operand_c ? inst.operand_c : 10;
                if (mag > 100) mag = 100;
                float delta = (float)mag / 100.0f;
                if (id != 0)
                    jmn_penalizar_concepto(vm->mem_neuronal, id, delta);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_CONSOLIDAR_SUENO: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint8_t fc = (inst.flags & IR_INST_FLAG_B_IMMEDIATE) ? inst.operand_b : 0;
                uint8_t uc = (inst.flags & IR_INST_FLAG_C_IMMEDIATE) ? inst.operand_c : 0;
                if (fc == 0) fc = 5;
                if (uc == 0) uc = 10;
                float factor = (float)fc / 100.0f;
                float umbral = (float)uc / 1000.0f;
                jmn_consolidar_memoria_sueno(vm->mem_neuronal, factor, 1, umbral, 0.05f);
                vm_set_register(vm, inst.operand_a, 1);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_OLVIDAR_DEBILES: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint8_t uc = (inst.flags & IR_INST_FLAG_C_IMMEDIATE) ? inst.operand_c : 0;
                if (uc == 0) uc = 10;
                float umbral = (float)uc / 1000.0f;
                uint32_t n = jmn_olvidar_conexiones_debiles(vm->mem_neuronal, umbral);
                vm_set_register(vm, inst.operand_a, (uint64_t)n);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_PERCEPCION_REGISTRAR: {
            uint32_t cid = (uint32_t)vm_get_register(vm, inst.operand_b);
            vm_percepcion_push(vm, cid);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_PERCEPCION_VENTANA: {
            uint32_t cap = VM_PERCEPCION_CAP_DEFAULT;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                cap = inst.operand_b;
                if (inst.flags & IR_INST_FLAG_C_IMMEDIATE)
                    cap |= (uint32_t)inst.operand_c << 8;
            }
            if (cap == 0) cap = VM_PERCEPCION_CAP_DEFAULT;
            int ok = vm_percepcion_set_cap(vm, cap) == 0;
            vm_set_register(vm, inst.operand_a, ok ? 1u : 0u);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_PERCEPCION_LIMPIAR: {
            vm_percepcion_clear(vm);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_PERCEPCION_TAMANO: {
            vm_set_register(vm, inst.operand_a, (uint64_t)vm->percepcion_count);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_PERCEPCION_ANTERIOR: {
            uint32_t k;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE)
                k = inst.operand_b;
            else
                k = (uint32_t)vm_get_register(vm, inst.operand_b);
            vm_set_register(vm, inst.operand_a, (uint64_t)vm_percepcion_id_at(vm, k));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_PERCEPCION_LISTA: {
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                jmn_vector_limpiar(vm->mem_colecciones, VM_PERCEPCION_LISTA_ID);
                jmn_crear_lista(vm->mem_colecciones, VM_PERCEPCION_LISTA_ID);
                for (uint32_t k = 0; k < vm->percepcion_count; k++) {
                    JMNValor v;
                    v.u = vm_percepcion_id_at(vm, k);
                    jmn_lista_agregar(vm->mem_colecciones, VM_PERCEPCION_LISTA_ID, v);
                }
                vm_set_register(vm, inst.operand_a, (uint64_t)VM_PERCEPCION_LISTA_ID);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_RASTRO_ACTIVACION_VENTANA: {
            uint32_t cap = VM_RASTRO_CAP_DEFAULT;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                cap = inst.operand_b;
                if (inst.flags & IR_INST_FLAG_C_IMMEDIATE)
                    cap |= (uint32_t)inst.operand_c << 8;
            }
            if (cap == 0) cap = VM_RASTRO_CAP_DEFAULT;
            int ok = vm_rastro_set_cap(vm, cap) == 0;
            vm_set_register(vm, inst.operand_a, ok ? 1u : 0u);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_RASTRO_ACTIVACION_LIMPIAR: {
            vm_rastro_clear(vm);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_RASTRO_ACTIVACION_TAMANO: {
            vm_set_register(vm, inst.operand_a, (uint64_t)vm->rastro_count);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_RASTRO_ACTIVACION_OBTENER: {
            uint32_t idx;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE)
                idx = inst.operand_b;
            else
                idx = (uint32_t)vm_get_register(vm, inst.operand_b);
            vm_set_register(vm, inst.operand_a, (uint64_t)vm_rastro_id_at(vm, idx));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_RASTRO_ACTIVACION_PESO: {
            uint32_t idx;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE)
                idx = inst.operand_b;
            else
                idx = (uint32_t)vm_get_register(vm, inst.operand_b);
            float f = vm_rastro_peso_at(vm, idx);
            union { float f; uint32_t u; } u = { .f = f };
            vm_set_register(vm, inst.operand_a, (uint64_t)u.u);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_RASTRO_ACTIVACION_LISTA: {
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                jmn_vector_limpiar(vm->mem_colecciones, VM_RASTRO_LISTA_ID);
                jmn_crear_lista(vm->mem_colecciones, VM_RASTRO_LISTA_ID);
                for (uint32_t i = 0; i < vm->rastro_count; i++) {
                    JMNValor v;
                    v.u = vm_rastro_id_at(vm, i);
                    jmn_lista_agregar(vm->mem_colecciones, VM_RASTRO_LISTA_ID, v);
                }
                vm_set_register(vm, inst.operand_a, (uint64_t)VM_RASTRO_LISTA_ID);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_CREAR: {
            uint32_t id_nombre = (uint32_t)vm_get_register(vm, inst.operand_a);
            const char* nombre = vm_text_cache_get(vm, id_nombre);
            
            if (!nombre || !nombre[0]) {
                nombre = "cerebro.jmn"; 
            }

#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                jmn_finalizar_escritura(vm->mem_neuronal);
                jmn_cerrar(vm->mem_neuronal);
                vm->mem_neuronal = NULL;
            }
            
            // Usar jmn_abrir_escritura que maneja apertura/creación sin truncar
            // Capacidad opcional en R2 (nodos) y R3 (conexiones)
            uint64_t n_cap = vm->registers[2]; 
            uint64_t c_cap = vm->registers[3];
            
            if (n_cap == 0) n_cap = 200000;
            if (c_cap == 0) c_cap = 10000000;

            vm->mem_neuronal = jmn_abrir_escritura(nombre);
            if (!vm->mem_neuronal) {
                // Si falla abrir_escritura (que por defecto crea con defaults si no existe),
                // intentamos creación explícita con las capacidades solicitadas.
                vm->mem_neuronal = jmn_crear(nombre);
            }
            
            if (!vm->mem_neuronal) {
                fprintf(stderr, "Error: No se pudo crear/cargar memoria '%s'\n", nombre);
                vm_set_register(vm, inst.operand_a, 0); 
            } else {
                vm_set_register(vm, inst.operand_a, 1); 
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }



        case OP_MEM_CERRAR: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                jmn_finalizar_escritura(vm->mem_neuronal);
                jmn_cerrar(vm->mem_neuronal);
                vm->mem_neuronal = NULL;
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_A_ENTERO: {
            uint32_t id_frase = (uint32_t)vm_get_register(vm, inst.operand_b);
            char buffer[256];
            int64_t iv;
            if (vm_text_cache_get_copy(vm, id_frase, buffer, sizeof(buffer)) &&
                vm_parse_decimal_entero_estricto(buffer, &iv)) {
                vm->registers[inst.operand_a] = (uint64_t)iv;
            } else {
                char trymsg[400];
                if (vm_text_cache_get_copy(vm, id_frase, buffer, sizeof(buffer)))
                    snprintf(trymsg, sizeof trymsg,
                             "str_a_entero: el texto no es un entero decimal valido (\"%.120s\")", buffer);
                else
                    snprintf(trymsg, sizeof trymsg,
                             "str_a_entero: no se pudo leer el texto (id %u)", (unsigned)id_frase);
                if (vm_try_catch_or_abort(vm, trymsg)) return 0;
                if (vm->current_line > 0)
                    fprintf(stderr,
                            "Error de ejecucion (VM) en la linea %d: %s\n",
                            vm->current_line, trymsg);
                else
                    fprintf(stderr, "Error de ejecucion (VM): %s\n", trymsg);
                vm->running = 0;
                vm->exit_code = 1;
                return 0;
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_A_FLOTANTE: {
            uint32_t id_frase = (uint32_t)vm_get_register(vm, inst.operand_b);
            char buffer[256];
            float val = nanf("");
            if (vm_text_cache_get_copy(vm, id_frase, buffer, sizeof(buffer)))
                val = vm_parse_decimal_flotante_estricto(buffer);
            if (isnan((double)val)) {
                char trymsg[400];
                if (vm_text_cache_get_copy(vm, id_frase, buffer, sizeof(buffer)))
                    snprintf(trymsg, sizeof trymsg,
                             "str_a_flotante: el texto no es un numero flotante valido (\"%.120s\")", buffer);
                else
                    snprintf(trymsg, sizeof trymsg,
                             "str_a_flotante: no se pudo leer el texto (id %u)", (unsigned)id_frase);
                if (vm_try_catch_or_abort(vm, trymsg)) return 0;
                if (vm->current_line > 0)
                    fprintf(stderr,
                            "Error de ejecucion (VM) en la linea %d: %s\n",
                            vm->current_line, trymsg);
                else
                    fprintf(stderr, "Error de ejecucion (VM): %s\n", trymsg);
                vm->running = 0;
                vm->exit_code = 1;
                return 0;
            }
            vm->registers[inst.operand_a] = 0;
            *(float*)&vm->registers[inst.operand_a] = val;
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }


        case OP_STR_EXTRAER_ANTES: {
            uint32_t off24 = (uint32_t)inst.operand_a | ((uint32_t)inst.operand_b << 8) | ((uint32_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 12 <= vm->ir->header.data_size) {
                 uint32_t id_frase=0, id_patron=0, id_dest=0;
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_frase);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24+4, &id_patron);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24+8, &id_dest);
                 jmn_extraer_antes_de(vm->mem_neuronal, id_frase, id_patron, id_dest);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_EXTRAER_DESPUES: {
            uint32_t off24 = (uint32_t)inst.operand_a | ((uint32_t)inst.operand_b << 8) | ((uint32_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 12 <= vm->ir->header.data_size) {
                 uint32_t id_frase=0, id_patron=0, id_dest=0;
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_frase);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24+4, &id_patron);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24+8, &id_dest);
                 jmn_extraer_despues_de(vm->mem_neuronal, id_frase, id_patron, id_dest);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_CONCATENAR: {
            uint32_t off24 = (uint32_t)inst.operand_a | ((uint32_t)inst.operand_b << 8) | ((uint32_t)inst.operand_c << 16);
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && off24 + 12 <= vm->ir->header.data_size) {
                 uint32_t id_izq=0, id_der=0, id_dest=0;
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24, &id_izq);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24+4, &id_der);
                 vm_leer_u32(vm->ir->data, vm->ir->header.data_size, off24+8, &id_dest);
                 jmn_concatenar_texto(vm->mem_neuronal, id_izq, id_der, id_dest);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_CONCATENAR_REG: {
            uint32_t id_izq = (uint32_t)vm_get_register(vm, inst.operand_b);
            uint32_t id_der = (uint32_t)vm_get_register(vm, inst.operand_c);
            uint32_t id_res = 0;
            /* Bypasseamos integración neuronal para mayor estabilidad en compilador */
            /*
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                id_res = jmn_concatenar_dinamico(vm->mem_neuronal, id_izq, id_der);
            }
#endif
            */
            // Fallback a cache de texto si no hay integración o falló
            if (id_res == 0) {
                const char* s1 = vm_text_cache_get(vm, id_izq);
                const char* s2 = vm_text_cache_get(vm, id_der);
                if (!s1 && id_izq == 0) s1 = "";
                if (!s2 && id_der == 0) s2 = "";
                if (s1 && s2) {
                    VMTextCacheEntry* e1 = vm_text_cache_find(vm, id_izq);
                    VMTextCacheEntry* e2 = vm_text_cache_find(vm, id_der);
                    size_t l1 = (e1 && e1->text == s1) ? e1->text_len : strlen(s1);
                    size_t l2 = (e2 && e2->text == s2) ? e2->text_len : strlen(s2);
                    char* combined = (char*)malloc(l1 + l2 + 1);
                    if (combined) {
                        strcpy(combined, s1);
                        strcat(combined, s2);
                        id_res = vm_hash_texto(combined);
                        vm_text_cache_put(vm, id_res, combined);
                        free(combined);
                    }
                }
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)id_res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_LONGITUD: { // 0xBA
            uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_b);
            size_t len = 0;
            VMTextCacheEntry* e = vm_text_cache_find(vm, id);
            if (e && e->text) {
                len = e->text_len;
            } else {
                const char* s = vm_text_cache_get(vm, id);
                len = s ? strlen(s) : 0;
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)len);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_EXTRAER_CARACTER: { // 0xEC
            uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_b);
            int index = (int)c_val;
            const char* s = vm_text_cache_get(vm, id);
            uint32_t res_id = 0;
            if (s && index >= 0 && index < (int)strlen(s)) {
                char buf[2] = { s[index], '\0' };
                res_id = vm_hash_texto(buf);
                vm_text_cache_put(vm, res_id, buf);
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)res_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_CODIGO_CARACTER: { /* 0xEE: A <- ASCII del primer char de B */
            uint32_t id = (uint32_t)vm_get_register(vm, inst.operand_b);
            const char* s = vm_text_cache_get(vm, id);
            uint64_t codigo = 0;
#ifndef JASBOOT_LANG_INTEGRATION
            (void)id;
#endif
            if (!s) {
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal) {
                    char buf[16];
                    if (jmn_obtener_texto(vm->mem_neuronal, id, buf, sizeof(buf)) >= 0)
                        s = buf;
                }
#endif
            }
            if (s && s[0] != '\0')
                codigo = (unsigned char)s[0];
            vm_set_register(vm, inst.operand_a, codigo);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_FLOTANTE_PREC: {
            uint64_t fv = vm_get_register(vm, inst.operand_b);
            int prec;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE)
                prec = (int)inst.operand_c;
            else
                prec = (int)vm_get_register(vm, inst.operand_c);
            if (prec < 0) prec = 0;
            if (prec > 20) prec = 20;
            union { uint64_t u64; float f32; } u = {0};
            u.u64 = (uint32_t)fv;
            float val = u.f32;
            char buf[96];
            snprintf(buf, sizeof(buf), "%.*f", prec, (double)val);
            uint32_t id_res = vm_hash_texto(buf);
            vm_text_cache_put(vm, id_res, buf);
            vm_set_register(vm, inst.operand_a, (uint64_t)id_res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_DESDE_NUMERO: {
            uint64_t reg_val = vm_get_register(vm, inst.operand_b);
            char buf[64];
            
            // Flag de tipo en operand_c (1=entero, 0=float)
            int is_int = 0;
            if (inst.flags & IR_INST_FLAG_C_IMMEDIATE) {
                is_int = (inst.operand_c == 1);
            } else {
                is_int = (vm_get_register(vm, inst.operand_c) == 1);
            }
            
            if (is_int) {
                snprintf(buf, sizeof(buf), "%lld", (long long)reg_val);
            } else {
                union { uint64_t u64; float f32; } u = {0};
                u.u64 = (uint32_t)reg_val;
                float val = u.f32;
                if (val == (float)((long long)val)) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)val);
                } else {
                    snprintf(buf, sizeof(buf), "%.2f", val);
                }
            }
            
            uint32_t id_res = 0;
            /* Bypasseamos integración neuronal para mayor estabilidad en compilador */
            /*
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                id_res = jmn_registrar_texto_dinamico(vm->mem_neuronal, buf);
            }
#endif
            */
            if (id_res == 0) {
                id_res = vm_hash_texto(buf);
                vm_text_cache_put(vm, id_res, buf);
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)id_res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_DESDE_CODIGO: { /* 0x9A: A <- string de 1 char con código ASCII B */
            uint64_t codigo = (inst.flags & IR_INST_FLAG_B_REGISTER)
                ? vm_get_register(vm, inst.operand_b) : (uint64_t)inst.operand_b;
            char buf[2] = { (char)(codigo & 0xFF), '\0' };
            uint32_t id_res = vm_hash_texto(buf);
            vm_text_cache_put(vm, id_res, buf);
            vm_set_register(vm, inst.operand_a, (uint64_t)id_res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_EXTRAER_DESPUES_REG: {
#ifdef JASBOOT_LANG_INTEGRATION
            {
                uint32_t id_frase = (uint32_t)vm->registers[inst.operand_b];
                uint32_t id_patron = (uint32_t)vm->registers[inst.operand_c];
                
                char frase[4096], patron[512];
                memset(frase, 0, 4096);
                memset(patron, 0, 512);

                int ok_f = vm_text_cache_get_copy(vm, id_frase, frase, 4096);
                if (!ok_f && vm->mem_neuronal) ok_f = (jmn_obtener_texto(vm->mem_neuronal, id_frase, frase, 4096) >= 0);
                int ok_p = vm_text_cache_get_copy(vm, id_patron, patron, 512);
                if (!ok_p && vm->mem_neuronal) ok_p = (jmn_obtener_texto(vm->mem_neuronal, id_patron, patron, 512) >= 0);

                if (ok_f && ok_p) {
                    char* pos = strstr(frase, patron);
                    if (pos) {
                        pos += strlen(patron);
                        while (*pos == ' ') pos++;
                        uint32_t id_res = vm_hash_texto(pos);
                        if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, id_res, pos);
                        vm_text_cache_put(vm, id_res, pos);
                        vm->registers[inst.operand_a] = (uint64_t)id_res;
                    } else {
                        vm->registers[inst.operand_a] = 5381;
                    }
                } else {
                    vm->registers[inst.operand_a] = 5381;
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }


        case OP_STR_SUBTEXTO: {
            uint32_t id_frase = (uint32_t)vm_get_register(vm, inst.operand_b);
            uint32_t start = (uint32_t)vm_get_register(vm, inst.operand_c);
            uint32_t len = (uint32_t)vm_get_register(vm, inst.operand_c + 1);
            const char* frase = NULL;
            size_t f_len = 0;
            char frase_stack[4096];
            VMTextCacheEntry* ent = vm_text_cache_find(vm, id_frase);
            if (ent && ent->text) {
                frase = ent->text;
                f_len = ent->text_len;
            }
#ifdef JASBOOT_LANG_INTEGRATION
            if (!frase && vm->mem_neuronal &&
                jmn_obtener_texto(vm->mem_neuronal, id_frase, frase_stack, sizeof(frase_stack)) >= 0) {
                frase = frase_stack;
                f_len = strlen(frase_stack);
            }
#endif
            if (frase && f_len > 0) {
                if (start < f_len) {
                    if ((size_t)start + (size_t)len > f_len)
                        len = (uint32_t)(f_len - start);
                    if (len >= 4096) len = 4095;
                    char salida[4096];
                    memcpy(salida, frase + start, len);
                    salida[len] = '\0';
                    uint32_t id_res = vm_hash_texto(salida);
#ifdef JASBOOT_LANG_INTEGRATION
                    if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, id_res, salida);
#endif
                    vm_text_cache_put(vm, id_res, salida);
                    vm_set_register(vm, inst.operand_a, (uint64_t)id_res);
                } else {
                    vm_set_register(vm, inst.operand_a, 5381);
                }
            } else {
                vm_set_register(vm, inst.operand_a, 5381);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_EXTRAER_ANTES_REG: {
#ifdef JASBOOT_LANG_INTEGRATION
            {
                uint32_t id_frase = (uint32_t)b_val;
                uint32_t id_patron = (uint32_t)c_val;

                char frase[4096], patron[512], salida[4096];
                memset(frase, 0, 4096);
                memset(patron, 0, 512);
                memset(salida, 0, 4096);

                int ok_f = vm_text_cache_get_copy(vm, id_frase, frase, 4096);
                if (!ok_f && vm->mem_neuronal) ok_f = (jmn_obtener_texto(vm->mem_neuronal, id_frase, frase, 4096) >= 0);
                int ok_p = vm_text_cache_get_copy(vm, id_patron, patron, 512);
                if (!ok_p && vm->mem_neuronal) ok_p = (jmn_obtener_texto(vm->mem_neuronal, id_patron, patron, 512) >= 0);

                if (ok_f && ok_p) {
                    char *pos = strstr(frase, patron);
                    if (pos) {
                        size_t len = (size_t)(pos - frase);
                        if (len < 4096) {
                            memcpy(salida, frase, len);
                            salida[len] = '\0';
                            uint32_t id_res = vm_hash_texto(salida);
                            if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, id_res, salida);
                            vm_text_cache_put(vm, id_res, salida);
                            vm->registers[inst.operand_a] = (uint64_t)id_res;
                        } else {
                            vm->registers[inst.operand_a] = 5381;
                        }
                    } else {
                        vm->registers[inst.operand_a] = 5381;
                    }
                } else {
                    vm->registers[inst.operand_a] = 5381;
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_REGISTRAR_LITERAL: {
            uint32_t off24 = (uint32_t)inst.operand_a | ((uint32_t)inst.operand_b << 8) | ((uint32_t)inst.operand_c << 16);
            if (vm->ir && vm->ir->data && off24 < vm->ir->header.data_size) {
                const char* literal = (const char*)(vm->ir->data + off24);
                uint32_t hash = vm_hash_texto(literal);
                vm_text_cache_put(vm, hash, literal);
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal) {
                    jmn_guardar_texto(vm->mem_neuronal, hash, literal);
                }
#endif
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_RECORDAR_TEXTO: {
            size_t offset = (size_t)inst.operand_a |
                            ((size_t)inst.operand_b << 8) |
                            ((size_t)inst.operand_c << 16);
#ifndef JASBOOT_LANG_INTEGRATION
            (void)offset;
#endif
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                const char* texto = (const char*)vm->ir->data + offset;
                uint32_t hash = vm_hash_texto(texto);
                
                jmn_guardar_texto(vm->mem_neuronal, hash, texto);
                vm_text_cache_put(vm, hash, texto);
                JMNValor v_uno = { .f = 1.0f };
                jmn_agregar_nodo(vm->mem_neuronal, hash, v_uno);
                vm_percepcion_push(vm, hash);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }




        case OP_MEM_BUSCAR_PESO_REG: {
            // A: Registro con ID (u32)
            // B|C: Dirección destino (u16)
            uint8_t reg_id = inst.operand_a;
            uint16_t dest_addr = (uint16_t)inst.operand_b | ((uint16_t)inst.operand_c << 8);

#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                // Leer ID desde registro
                uint64_t id_val = vm->registers[reg_id];
                uint32_t id = (uint32_t)id_val;
                
                uint64_t valor = 0;
                JMNNodo* nodo = jmn_obtener_nodo(vm->mem_neuronal, id);
                if (nodo) {
                    valor = (uint64_t)(nodo->peso.u);
                }
                
                if (dest_addr + sizeof(uint64_t) <= vm->memory_size) {
                    *(uint64_t*)(vm->memory + dest_addr) = valor;
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_APRENDER_PESO_REG: {
            uint8_t reg_id = inst.operand_a;
            uint8_t reg_peso = inst.operand_b;
            
            uint64_t id_val = vm->registers[reg_id];
            uint32_t id = (uint32_t)id_val;
            uint64_t peso_raw = vm->registers[reg_peso];
            float peso = (float)peso_raw;

#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNValor v_p; v_p.f = peso;
                jmn_aprender_nodo(vm->mem_neuronal, id, v_p);
            }
#endif
            vm_percepcion_push(vm, id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }



        case OP_LEER_U32_IND: {
            uint64_t addr = vm->registers[inst.operand_b];
            if (addr + 4 <= vm->memory_size) {
                uint32_t val = 0;
                memcpy(&val, vm->memory + addr, 4);
                vm->registers[inst.operand_a] = (uint64_t)val;
            } else {
                vm->registers[inst.operand_a] = 0;
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_LEER_TEXTO: {
            uint32_t id_ruta = (uint32_t)vm_get_register(vm, inst.operand_b);
            const char* ruta = vm_resolve_path(vm, id_ruta);
            if (ruta) {
                FILE* f = fopen(ruta, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long size = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    
                    char* buf = (char*)malloc(size + 1);
                    if (buf) {
                        fread(buf, 1, size, f);
                        buf[size] = '\0';
                        
                        uint32_t hash = vm_hash_texto(buf);
                        vm_text_cache_put(vm, hash, buf);
                        vm_set_register(vm, inst.operand_a, (uint64_t)hash);
                        free(buf);
                    } else {
                        vm_set_register(vm, inst.operand_a, 0);
                    }
                    fclose(f);
                } else {
                    vm_set_register(vm, inst.operand_a, 0);
                }
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
            
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_ESCRIBIR_TEXTO: {
            size_t offset = (size_t)inst.operand_a |
                            ((size_t)inst.operand_b << 8) |
                            ((size_t)inst.operand_c << 16);
#ifndef JASBOOT_LANG_INTEGRATION
            (void)offset;
#endif
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && vm->ir && vm->ir->data && offset + 8 <= vm->ir->header.data_size) {
                uint32_t path_offset = 0;
                uint32_t id = 0;
                if (vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset, &path_offset) == 0 &&
                    vm_leer_u32(vm->ir->data, vm->ir->header.data_size, offset + 4, &id) == 0) {
                    if (path_offset < vm->memory_size) {
                        const char* ruta = (const char*)(vm->memory + path_offset);
                        jmn_escribir_archivo(vm->mem_neuronal, ruta, id);
                    }
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_LEER_ARCHIVO_REG: {
            uint32_t id_ruta = (uint32_t)vm_get_register(vm, inst.operand_a);
            uint32_t id_dest = (uint32_t)vm_get_register(vm, inst.operand_b);
            const char* ruta = vm_text_cache_get(vm, id_ruta);
#ifdef JASBOOT_LANG_INTEGRATION
            if (ruta && vm->mem_neuronal) {
                jmn_leer_archivo(vm->mem_neuronal, ruta, id_dest);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_ESCRIBIR_ARCHIVO_REG: {
            uint32_t id_ruta = (uint32_t)vm_get_register(vm, inst.operand_a);
            uint32_t id_origen = (uint32_t)vm_get_register(vm, inst.operand_b);
            const char* ruta = vm_text_cache_get(vm, id_ruta);
#ifdef JASBOOT_LANG_INTEGRATION
            if (ruta && vm->mem_neuronal) {
                jmn_escribir_archivo(vm->mem_neuronal, ruta, id_origen);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
            



        case OP_MEM_OBTENER_RELACIONADOS: {
            uint32_t concepto_id = (uint32_t)b_val;
            uint32_t lista_id = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNMemoria* mem = vm->mem_neuronal;
                JMNNodo* nodo = jmn_obtener_nodo(mem, concepto_id);
                if (nodo) {
                    uint32_t count = 0;
                    JMNConexion* conns = jmn_obtener_conexiones(mem, nodo, &count);
                    
                    // Usar un ID de lista temporal diferente al ID del concepto
                    lista_id = concepto_id ^ 0xF0F0F0F0; 
                    jmn_crear_lista(mem, lista_id);
                    
                    for (uint32_t i = 0; i < count; i++) {
                        if (conns[i].fuerza.f > 0.01f || conns[i].key_id == 0) {
                            JMNValor v_d; v_d.u = conns[i].destino_id;
                            jmn_lista_agregar(mem, lista_id, v_d);
                        }
                    }
                    }
                }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)lista_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_IMPRIMIR_FLOTANTE: {
            uint64_t val = vm_get_register(vm, inst.operand_a);
            vm_escribir_flotante(val);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_OBTENER_FUERZA: {
            uint32_t id1 = (uint32_t)vm_get_register(vm, inst.operand_b);
            uint32_t id2 = (uint32_t)vm_get_register(vm, inst.operand_c);
            float fuerza = 0.0f;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                fuerza = jmn_obtener_fuerza_asociacion(vm->mem_neuronal, id1, id2);
            }
#endif
            // Convertir float a bits para registro (union cast)
            union { float f; uint64_t u; } cast;
            cast.f = fuerza;
            vm_set_register(vm, inst.operand_a, cast.u);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

            
        case OP_ACTIVAR_MODULO: {
            // Hot-Reload: Recargar el archivo IR actual desde el disco preservando estado
            if (vm->ir_path) {
                // Usar ir_file_create + ir_file_read + vm_load
                IRFile* new_ir = ir_file_create();
                if (new_ir && ir_file_read(new_ir, vm->ir_path) == 0) {
                    if (vm->ir) ir_file_destroy(vm->ir);
                    vm_load(vm, new_ir); // Reinicia PC a 0
                    fprintf(stderr, "[VM] Hot-Reload exitoso desde '%s'\n", vm->ir_path);
                } else {
                    if (new_ir) ir_file_destroy(new_ir);
                    fprintf(stderr, "[VM ERR] Falló Hot-Reload desde '%s'\n", vm->ir_path);
                }
            }
            // No incrementamos PC porque reiniciamos o fallamos
            break;
        }

        case OP_ESTABLECER_CONTEXTO: {
            size_t offset = (size_t)inst.operand_a |
                            ((size_t)inst.operand_b << 8) |
                            ((size_t)inst.operand_c << 16);
            if (vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                const char* ctx = (const char*)vm->ir->data + offset;
                if (vm->context) free(vm->context);
                vm->context = strdup(ctx);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_USA_CONCEPTO:
            vm->pc += IR_INSTRUCTION_SIZE;
            break;

        case OP_MEM_LISTA_CREAR: {
            uint32_t id = 0;
            if (inst.flags & IR_INST_FLAG_B_IMMEDIATE) {
                size_t offset = (size_t)inst.operand_b | ((size_t)inst.operand_c << 8);
                if (vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                    const char* name = (const char*)vm->ir->data + offset;
                    id = vm_hash_texto(name);
#ifdef JASBOOT_LANG_INTEGRATION
                    // ensure_jmn(vm); eliminado
                    if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, id, name);
#endif
                    vm_text_cache_put(vm, id, name);
                }
            } else {
                id = (uint32_t)vm_get_register(vm, inst.operand_b);
            }
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                jmn_crear_lista(vm->mem_colecciones, id);
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_ID: {
             size_t offset = (size_t)inst.operand_b | ((size_t)inst.operand_c << 8);
             uint32_t id = 0;
             if (vm->ir && vm->ir->data && offset < vm->ir->header.data_size) {
                 const char* name = (const char*)vm->ir->data + offset;
                 for (const char* p = name; *p; p++) id = id * 31 + (unsigned char)*p;
             }
             vm_set_register(vm, inst.operand_a, (uint64_t)id);
             vm->pc += IR_INSTRUCTION_SIZE;
             break;
        }


        case OP_MEM_LISTA_AGREGAR: {
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                JMNValor val;
                val.u = (uint32_t)b_val;
                jmn_lista_agregar(vm->mem_colecciones, (uint32_t)a_val, val);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_UNIR: {
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                // R_A = R_B + R_C (IDs de listas)
                uint32_t id_izq = (uint32_t)b_val;
                uint32_t id_der = (uint32_t)c_val;
                
                /*
                printf("[VM DBG] OP_MEM_LISTA_UNIR: inst.operand_b=%u, inst.operand_c=%u, flags=%02X\n", 
                       inst.operand_b, inst.operand_c, inst.flags);
                printf("[VM DBG]    VALS: b_val=%08X, c_val=%08X\n", (uint32_t)b_val, (uint32_t)c_val);
                printf("[VM DBG]    REGS: R%u=%08llX, R%u=%08llX\n", 
                       inst.operand_b, vm_get_register(vm, inst.operand_b),
                       inst.operand_c, vm_get_register(vm, inst.operand_c));
                */

                // Generar un ID nuevo para la lista destino
                uint32_t id_dest = (id_izq ^ id_der ^ 0x12345678) | 0x80000000;
                
                jmn_lista_unir(vm->mem_colecciones, id_izq, id_der, id_dest);
                vm_set_register(vm, inst.operand_a, (uint64_t)id_dest);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_OBTENER: {
            uint32_t list_id = (uint32_t)b_val;
            uint32_t idx = (uint32_t)c_val;
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                if (jmn_lista_indice_fuera_de_rango(vm->mem_colecciones, list_id, idx)) {
                    uint32_t tam = jmn_lista_tamano(vm->mem_colecciones, list_id);
                    char rango[112];
                    if (tam == 0)
                        snprintf(rango, sizeof rango, "la lista esta vacia (no hay ningun indice valido)");
                    else if (tam == 1)
                        snprintf(rango, sizeof rango, "solo el indice 0 es valido (hay 1 elemento)");
                    else
                        snprintf(rango, sizeof rango,
                                 "hay %u elementos; los indices validos van de 0 a %u",
                                 (unsigned)tam, (unsigned)(tam - 1));
                    char trymsg[320];
                    snprintf(trymsg, sizeof trymsg,
                             "indice de lista invalido: posicion %u; %s (mem_lista_obtener, lista_obtener o [])",
                             (unsigned)idx, rango);
                    if (vm_try_catch_or_abort(vm, trymsg)) return 0;
                    if (vm->current_line > 0)
                        fprintf(stderr,
                                "Error de ejecucion (VM) en la linea %d: indice de lista invalido: se pidio la posicion %u, pero %s.\n"
                                "         Origen habitual: mem_lista_obtener / lista_obtener o una expresion con corchetes [ ].\n",
                                vm->current_line, (unsigned)idx, rango);
                    else
                        fprintf(stderr,
                                "Error de ejecucion (VM): indice de lista invalido: se pidio la posicion %u, pero %s.\n"
                                "         Origen habitual: mem_lista_obtener / lista_obtener o una expresion con corchetes [ ].\n",
                                (unsigned)idx, rango);
                    vm->running = 0;
                    vm->exit_code = 1;
                    return 0;
                }
                JMNValor val = jmn_lista_obtener(vm->mem_colecciones, list_id, idx);
                vm_set_register(vm, inst.operand_a, (uint64_t)val.u);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_PONER: {
            uint32_t list_id = (uint32_t)a_val;
            uint32_t idx = (uint32_t)b_val;
            uint64_t reg_val = c_val;
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                JMNValor val; val.u = (uint32_t)reg_val;
                jmn_lista_poner(vm->mem_colecciones, list_id, idx, val);
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_LIMPIAR: {
            uint32_t list_id = (uint32_t)a_val;
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                jmn_vector_limpiar(vm->mem_colecciones, list_id);
                JMNNodo* nodo = jmn_obtener_nodo(vm->mem_colecciones, list_id);
                if (nodo) nodo->peso.f = 0.0f;
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_LIBERAR: {
            uint32_t list_id = (uint32_t)a_val;
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones)
                jmn_lista_liberar(vm->mem_colecciones, list_id);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_LISTA_TAMANO: {
            uint32_t list_id_val = (uint32_t)b_val;
            uint32_t tam = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            ensure_jmn_col(vm);
            if (vm->mem_colecciones) {
                tam = (uint32_t)jmn_lista_tamano(vm->mem_colecciones, list_id_val);
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)tam);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_REGISTRAR_PATRON: {
            uint32_t list_id_val = (uint32_t)a_val;
            uint32_t pattern_id = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNMemoria* mem_neu = vm->mem_neuronal;
                JMNMemoria* mem_src = NULL;
                uint32_t count = 0;
                if (vm->mem_colecciones) {
                    count = jmn_lista_tamano(vm->mem_colecciones, list_id_val);
                    if (count > 0) mem_src = vm->mem_colecciones;
                }
                if (!mem_src) {
                    count = jmn_lista_tamano(mem_neu, list_id_val);
                    if (count > 0) mem_src = mem_neu;
                }
                if (mem_src && count > 0) {
                    uint32_t* ids = (uint32_t*)malloc(sizeof(uint32_t) * count);
                    if (ids) {
                        pattern_id = 0x5ECA; 
                        for (uint32_t i = 0; i < count; i++) {
                            JMNValor val_l = jmn_lista_obtener(mem_src, list_id_val, i);
                            ids[i] = val_l.u;
                            pattern_id = pattern_id * 31 + ids[i];
                        }
                        if (!jmn_obtener_nodo(mem_neu, pattern_id)) {
                             JMNValor v_dos = { .f = 2.0f };
                             jmn_agregar_nodo(mem_neu, pattern_id, v_dos); 
                        }
                        for (uint32_t i = 0; i < count; i++) {
                            JMNValor v_uno = { .f = 1.0f };
                            jmn_agregar_conexion(mem_neu, pattern_id, ids[i], v_uno, JMN_RELACION_PATRON);
                            if (i < count - 1) {
                                jmn_agregar_conexion(mem_neu, ids[i], ids[i + 1], v_uno, JMN_RELACION_SECUENCIA);
                                uint32_t rel_sec = jmn_relacion_con_contexto(JMN_RELACION_SECUENCIA, pattern_id);
                                jmn_agregar_conexion(mem_neu, ids[i], ids[i+1], v_uno, rel_sec);
                            }
                            // Asegurar que el nodo exista en Neuro aunque esté en RAM
                            if (!jmn_obtener_nodo(mem_neu, ids[i])) {
                                JMNValor v_nulo = { .f = 0.0f };
                                jmn_agregar_nodo(mem_neu, ids[i], v_nulo);
                            }
                        }
                        free(ids);
                    }
                }
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)pattern_id);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_ASOCIAR_SECUENCIA: {
            uint32_t concept_id = (uint32_t)a_val;
            uint32_t list_id_val = (uint32_t)b_val;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && concept_id != 0) {
                JMNMemoria* mem_neu = vm->mem_neuronal;
                JMNMemoria* mem_src = NULL;
                uint32_t count = 0;
                
                if (vm->mem_colecciones) {
                    count = jmn_lista_tamano(vm->mem_colecciones, list_id_val);
                    if (count > 0) mem_src = vm->mem_colecciones;
                }
                if (!mem_src) {
                    count = jmn_lista_tamano(mem_neu, list_id_val);
                    if (count > 0) mem_src = mem_neu;
                }

                if (mem_src && count > 0) {
                    uint32_t* ids = (uint32_t*)malloc(sizeof(uint32_t) * count);
                    if (ids) {
                        for (uint32_t i = 0; i < count; i++) {
                            JMNValor v_val_l = jmn_lista_obtener(mem_src, list_id_val, i);
                            ids[i] = v_val_l.u;
                        }
                        for (uint32_t i = 0; i < count; i++) {
                            JMNValor v_uno = { .f = 1.0f };
                            jmn_agregar_conexion(mem_neu, concept_id, ids[i], v_uno, JMN_RELACION_PATRON);
                            if (i < count - 1) {
                                uint32_t rel_sec = jmn_relacion_con_contexto(JMN_RELACION_SECUENCIA, concept_id);
                                jmn_agregar_conexion(mem_neu, ids[i], ids[i+1], v_uno, rel_sec);
                            }
                        }
                        free(ids);
                    }
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_COMPARAR_PATRONES: {
            uint32_t id1 = (uint32_t)b_val;
            uint32_t id2 = (uint32_t)c_val;
            float similitud = 0.0f;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                JMNMemoria* mem = vm->mem_neuronal;
                uint32_t tam1 = jmn_lista_tamano(mem, id1);
                uint32_t tam2 = jmn_lista_tamano(mem, id2);
                if (tam1 > 0 && tam2 > 0) {
                    uint32_t matches = 0;
                    uint32_t min_tam = (tam1 < tam2) ? tam1 : tam2;
                    for (uint32_t i = 0; i < min_tam; i++) {
                        if (jmn_lista_obtener(mem, id1, i).u == jmn_lista_obtener(mem, id2, i).u) {
                            matches++;
                        }
                    }
                    similitud = (float)matches / (float)((tam1 > tam2) ? tam1 : tam2);
                }
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)(similitud * 100.0f));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_OBTENER_VALOR: {
            /* A = valor almacenado en clave B (recordar key con valor X); tipo ASOCIACION (1). Si no hay, A = B.
             * Varias aristas tipo 1 desde la misma clave: la primera en la lista puede ser basura (p. ej. id sin texto
             * por aprendizaje viejo). Se elige el primer destino con cadena en cache o JMN; si ninguno tiene texto, A = B. */
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint32_t key_id = (uint32_t)vm_get_register(vm, inst.operand_b);
                JMNBusquedaResultado res[32];
                int n = jmn_buscar_asociaciones(vm->mem_neuronal, key_id, 1, 0.01f, 1, res, 32);
                uint32_t chosen = key_id;
                if (n > 0) {
                    int picked = 0;
                    for (int i = 0; i < n; i++) {
                        uint32_t cand = res[i].id;
                        if (cand == 0 || cand == key_id) continue;
                        const char* ct = vm_text_cache_get(vm, cand);
                        if (ct && ct[0]) {
                            chosen = cand;
                            picked = 1;
                            break;
                        }
                        char buf_txt[512];
                        if (jmn_obtener_texto(vm->mem_neuronal, cand, buf_txt, sizeof(buf_txt)) >= 0 && buf_txt[0]) {
                            vm_text_cache_put(vm, cand, buf_txt);
                            chosen = cand;
                            picked = 1;
                            break;
                        }
                    }
                    if (!picked) chosen = key_id;
                }
                vm_set_register(vm, inst.operand_a, (uint64_t)chosen);
            } else {
                uint64_t key = vm_get_register(vm, inst.operand_b);
                vm_set_register(vm, inst.operand_a, key);
            }
#else
            vm_set_register(vm, inst.operand_a, vm_get_register(vm, inst.operand_b));
#endif
            vm_percepcion_push(vm, (uint32_t)vm_get_register(vm, inst.operand_a));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_BUSCAR_ASOCIADOS: {
            // A <- mejor concepto asociado a B (origen); C = tipo_relacion (0 = cualquiera). Umbral 0.1, profundidad 2.
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint64_t b_val = vm_get_register(vm, inst.operand_b);
                uint64_t c_val = vm_get_register(vm, inst.operand_c);
                uint32_t origen_id = (uint32_t)b_val;
                uint32_t tipo_relacion = (uint32_t)(c_val & 0xFFu);
                if (tipo_relacion > JMN_RELACION_MAX) tipo_relacion = 0;
                JMNBusquedaResultado resultados[16];
                float umbral = 0.1f;
                uint16_t profundidad = 2;
                vm_rastro_clear(vm);
                vm_rastro_push(vm, origen_id, 1.0f);
                int n = jmn_buscar_asociaciones(vm->mem_neuronal, origen_id, tipo_relacion, umbral, profundidad, resultados, 16);
                if (n > 0) {
                    vm_set_register(vm, inst.operand_a, (uint64_t)resultados[0].id);
                    vm_rastro_push(vm, resultados[0].id, resultados[0].fuerza);
                } else {
                    vm_set_register(vm, inst.operand_a, 0);
                }
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm_percepcion_push(vm, (uint32_t)vm_get_register(vm, inst.operand_a));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_DECAE_CONEXIONES: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint8_t fc = (inst.flags & IR_INST_FLAG_B_IMMEDIATE) ? inst.operand_b : 0;
                uint8_t uc = (inst.flags & IR_INST_FLAG_C_IMMEDIATE) ? inst.operand_c : 0;
                if (fc == 0) fc = 5;
                if (uc == 0) uc = 10;
                float factor = (float)fc / 100.0f;
                float umbral = (float)uc / 1000.0f;
                jmn_decaer_conexiones_global(vm->mem_neuronal, factor, 1, umbral);
                vm_set_register(vm, inst.operand_a, 1);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_PROPAGAR_ACTIVACION: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint64_t b_val = vm_get_register(vm, inst.operand_b);
                uint64_t c_val = vm_get_register(vm, inst.operand_c);
                uint32_t origen_id = (uint32_t)b_val;
                uint32_t tipo_relacion = (uint32_t)(c_val & 0xFFu);
                uint32_t K = (uint32_t)((c_val >> 8) & 0xFFu);
                uint32_t prof = (uint32_t)((c_val >> 16) & 0xFFu);
                if (tipo_relacion > JMN_RELACION_MAX) tipo_relacion = 0;
                if (K == 0 || K > 32) K = 8;
                if (prof == 0) prof = 3;
                JMNActivacionResultado resultados[32];
                vm_rastro_clear(vm);
                int n = jmn_propagar_activacion(vm->mem_neuronal, origen_id, 1.0f, 0.8f, 0.1f,
                    (uint16_t)prof, tipo_relacion, resultados, (uint16_t)K, vm_jmn_rastro_cb, 0, vm);
                if (n > 0) {
                    vm_set_register(vm, inst.operand_a, (uint64_t)resultados[0].id);
                } else {
                    vm_set_register(vm, inst.operand_a, 0);
                }
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm_percepcion_push(vm, (uint32_t)vm_get_register(vm, inst.operand_a));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_RESOLVER_CONFLICTOS: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint64_t b_val = vm_get_register(vm, inst.operand_b);
                uint64_t c_val = vm_get_register(vm, inst.operand_c);
                uint32_t origen_id = (uint32_t)b_val;
                uint32_t tipo_relacion = (uint32_t)(c_val & 0xFFu);
                if (tipo_relacion > JMN_RELACION_MAX) tipo_relacion = 0;
                JMNBusquedaResultado resultados[32];
                JMNConflictoResultado out;
                vm_rastro_clear(vm);
                vm_rastro_push(vm, origen_id, 1.0f);
                int n = jmn_buscar_asociaciones(vm->mem_neuronal, origen_id, tipo_relacion, 0.1f, 2, resultados, 32);
                for (int ri = 0; ri < n; ri++)
                    vm_rastro_push(vm, resultados[ri].id, resultados[ri].fuerza);
                if (n >= 2) {
                    (void)jmn_resolver_conflictos(vm->mem_neuronal, origen_id, tipo_relacion, 0.1f, 2,
                        resultados, (uint16_t)n, 0.4f, 0.2f, &out);
                    vm_set_register(vm, inst.operand_a, (uint64_t)out.id_ganador);
                } else if (n == 1) {
                    vm_set_register(vm, inst.operand_a, (uint64_t)resultados[0].id);
                } else {
                    vm_set_register(vm, inst.operand_a, 0);
                }
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm_percepcion_push(vm, (uint32_t)vm_get_register(vm, inst.operand_a));
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ELEGIR_POR_PESO_IDX:
        case OP_MEM_ELEGIR_POR_PESO_ID: {
#ifdef JASBOOT_LANG_INTEGRATION
            uint32_t ctx = (uint32_t)b_val;
            uint32_t list_id = (uint32_t)c_val;
            uint32_t best_i = 0, best_id = 0;
            int ok = 0;
            ensure_jmn_col(vm);
            vm_elegir_por_peso_best(vm, ctx, list_id, &best_i, &best_id, &ok);
            if (ok) {
                if (inst.opcode == OP_MEM_ELEGIR_POR_PESO_IDX)
                    vm_set_register(vm, inst.operand_a, (uint64_t)best_i);
                else
                    vm_set_register(vm, inst.operand_a, (uint64_t)best_id);
            } else {
                vm_set_register(vm, inst.operand_a, (uint64_t)(uint32_t)(-1));
            }
#else
            vm_set_register(vm, inst.operand_a, (uint64_t)(uint32_t)(-1));
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_ELEGIR_POR_PESO_SEMILLA: {
            uint32_t s = (uint32_t)vm_get_register(vm, inst.operand_b);
            vm->elegir_por_peso_seed = s;
            vm_set_register(vm, inst.operand_a, 1);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_MEM_BUSCAR_ASOCIADOS_LISTA: {
            // A <- lista (id) con hasta K conceptos asociados a B; C = tipo (8 low) | (K << 8). Búsqueda en mem_neuronal, lista en mem_colecciones.
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint64_t b_val = vm_get_register(vm, inst.operand_b);
                uint64_t c_val = vm_get_register(vm, inst.operand_c);
                uint32_t origen_id = (uint32_t)b_val;
                uint32_t tipo_relacion = (uint32_t)(c_val & 0xFFu);
                uint32_t K = (uint32_t)((c_val >> 8) & 0xFFu);
                if (tipo_relacion > JMN_RELACION_MAX) tipo_relacion = 0;
                if (K == 0 || K > 64) K = 16;
                JMNBusquedaResultado resultados[64];
                int n = jmn_buscar_asociaciones(vm->mem_neuronal, origen_id, tipo_relacion, 0.1f, 2, resultados, (uint16_t)K);
                uint32_t list_id = (origen_id ^ 0xA5A5A5A5u) | 0x80000000u;
                ensure_jmn_col(vm);
                if (vm->mem_colecciones) {
                    jmn_crear_lista(vm->mem_colecciones, list_id);
                    for (int i = 0; i < n; i++) {
                        JMNValor v; v.u = resultados[i].id;
                        jmn_lista_agregar(vm->mem_colecciones, list_id, v);
                    }
                }
                vm_set_register(vm, inst.operand_a, (uint64_t)list_id);
            } else {
                vm_set_register(vm, inst.operand_a, 0);
            }
#else
            vm_set_register(vm, inst.operand_a, 0);
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_NOP:
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
            
        // === NUEVOS OPCODES FASE 0 (solo los que no existen) ===
        
        case OP_STR_MINUSCULAS: {
            // A = dest, B = string_reg
            uint64_t b_val = vm_get_register(vm, inst.operand_b);
            const char* str = vm_text_cache_get(vm, (uint32_t)b_val);
            if (str) {
                char* lower = strdup(str);
                for (char* p = lower; *p; p++) {
                    *p = tolower(*p);
                }
                uint32_t hash = vm_hash_texto(lower);
                vm_text_cache_put(vm, hash, lower);
#ifdef JASBOOT_LANG_INTEGRATION
                if (vm->mem_neuronal) {
                    jmn_guardar_texto(vm->mem_neuronal, hash, lower);
                }
#endif
                vm_set_register(vm, inst.operand_a, (uint64_t)hash);
                free(lower);
            } else {
                vm_set_register(vm, inst.operand_a, 5381);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        
        case OP_FS_ABRIR: {
            // A = file_handle, B = path_reg, C = mode_reg (hash o offset en data)
            uint64_t b_val = vm_get_register(vm, inst.operand_b);
            uint64_t c_val = vm_get_register(vm, inst.operand_c);
            const char* path = vm_resolve_path(vm, (uint32_t)b_val);
            const char* mode = vm_text_cache_get(vm, (uint32_t)c_val);
            /* Si mode no está en cache, puede ser offset en data (ej. compilador jbc) */
            if (!mode && vm->ir && vm->ir->data && (uint32_t)c_val < vm->ir->header.data_size)
                mode = (const char*)(vm->ir->data + (uint32_t)c_val);
            if (path && mode) {
                FILE* file = fopen(path, mode);
                vm_set_register(vm, inst.operand_a, (uint64_t)(uintptr_t)file);
                vm->current_file = file;
                if (!file) perror("[FS_ABRIR] fopen");
            } else {
                if (!path) fprintf(stderr, "[FS_ABRIR] path NULL (b_val=0x%08X)\n", (unsigned)(uint32_t)b_val);
                if (!mode) fprintf(stderr, "[FS_ABRIR] mode NULL (c_val=0x%08X)\n", (unsigned)(uint32_t)c_val);
                vm_set_register(vm, inst.operand_a, 0);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        
        case OP_FS_ESCRIBIR: {
            // A = data_reg, B = handle_reg (opcional)
            uint64_t a_val = vm_get_register(vm, inst.operand_a);
            const char* data = vm_text_cache_get(vm, (uint32_t)a_val);
            FILE* f = (inst.flags & IR_INST_FLAG_B_REGISTER) ? (FILE*)(uintptr_t)vm_get_register(vm, inst.operand_b) : vm->current_file;
            
            if (data && f) {
                fprintf(f, "%s", data);
                fflush(f);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
        
        case OP_FS_LEER_LINEA: {
            // A = dest_reg, B = handle_reg (opcional)
            FILE* f = (inst.flags & IR_INST_FLAG_B_REGISTER) ? (FILE*)(uintptr_t)vm_get_register(vm, inst.operand_b) : vm->current_file;
            if (f) {
                char buffer[4096];
                if (fgets(buffer, sizeof(buffer), f)) {
                    size_t len = strlen(buffer);
                    if (len > 0 && buffer[len-1] == '\n') buffer[--len] = '\0';
                    if (len > 0 && buffer[len-1] == '\r') buffer[--len] = '\0';
                    
                    uint32_t hash = vm_hash_texto(buffer);
                    vm_text_cache_put(vm, hash, buffer);
#ifdef JASBOOT_LANG_INTEGRATION
                    if (vm->mem_neuronal) {
                        jmn_guardar_texto(vm->mem_neuronal, hash, buffer);
                    }
#endif
                    vm_set_register(vm, inst.operand_a, (uint64_t)hash);
                } else {
                    vm_set_register(vm, inst.operand_a, 5381); // Normalizado a "Nada/Fin/Error"
                }
            } else {
                vm_set_register(vm, inst.operand_a, 5381);
            }
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_STR_ASOCIAR_PESOS: {
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal) {
                uint64_t b_val = vm_get_register(vm, inst.operand_b);
                uint64_t c_val = vm_get_register(vm, inst.operand_c);
                uint32_t id_frase = (uint32_t)b_val;
                
                // --- FASE 2: Peso Configurable ---
                float peso_base = 1.0f;
                // Si se proporcionó un registro C válido con valor > 0
                if (c_val > 0) {
                    float p = (float)c_val;
                    if (p > 100.0f) p = 100.0f;
                    peso_base = p / 100.0f;
                    // printf("[VM_DEBUG] Asociando con peso custom: %.2f (Raw: %llu)\n", peso_base, c_val);
                }
                // ---------------------------------

                char frase[4096]; // Buffer un poco más grande
                if (jmn_obtener_texto(vm->mem_neuronal, id_frase, frase, sizeof(frase)) >= 0) {
                    // Tokenización
                    char* palabras[256];
                    uint32_t hashes[256];
                    int count = 0;
                    char* token = strtok(frase, " ,.;:!?()\"\n\r\t");
                    while (token && count < 256) {
                        palabras[count] = token;
                        hashes[count] = vm_hash_texto(token);
                        count++;
                        token = strtok(NULL, " ,.;:!?()\"\n\r\t");
                    }

                    // Asegurar existencia de todos los nodos primero (Optimización)
                    for (int i = 0; i < count; i++) {
                        if (!jmn_obtener_nodo(vm->mem_neuronal, hashes[i])) {
                            jmn_guardar_texto(vm->mem_neuronal, hashes[i], palabras[i]);
                            JMNValor v_uno = { .f = 1.0f };
                            jmn_agregar_nodo(vm->mem_neuronal, hashes[i], v_uno);
                        }
                    }

                    // Auditoría
                    const size_t audit_size = 65536; // Buffer más grande para frases largas
                    char* auditoria = (char*)malloc(audit_size);
                    if (auditoria) {
                        size_t pos = 0;
                        pos += snprintf(auditoria + pos, audit_size - pos, "--- AUDITORÍA DE ASOCIACIÓN DENSA ---\n");
                        pos += snprintf(auditoria + pos, audit_size - pos, "[ENTRADA]: ID=%u\n", id_frase);
                        pos += snprintf(auditoria + pos, audit_size - pos, "[TOKENS]: %d detectados\n", count);
                        pos += snprintf(auditoria + pos, audit_size - pos, "[PESO]: %.2f\n", peso_base);
                        pos += snprintf(auditoria + pos, audit_size - pos, "-------------------------------------\n");

                        int nuevos = 0, reforzados = 0;
                        for (int i = 0; i < count; i++) {
                            for (int j = i + 1; j < count; j++) {
                                if (hashes[i] == hashes[j]) continue;

                                float peso_ant = jmn_obtener_fuerza_asociacion(vm->mem_neuronal, hashes[i], hashes[j]);
                                JMNValor v_base; v_base.f = peso_base;
                                jmn_agregar_conexion(vm->mem_neuronal, hashes[i], hashes[j], v_base, 0);
                                jmn_agregar_conexion(vm->mem_neuronal, hashes[j], hashes[i], v_base, 0);
                                float peso_nue = jmn_obtener_fuerza_asociacion(vm->mem_neuronal, hashes[i], hashes[j]);

                                if (pos + 256 < audit_size) {
                                    int n = snprintf(auditoria + pos, audit_size - pos, 
                                                   "[%d] '%s' <-> '%s' | P: %.2f -> %.2f | %s\n",
                                                   (nuevos + reforzados + 1), palabras[i], palabras[j], 
                                                   peso_ant, peso_nue, (peso_ant == 0 ? "NUEVO" : "REFORZADO"));
                                    if (n > 0 && (size_t)n < (audit_size - pos)) pos += n;
                                }

                                if (peso_ant == 0) nuevos++; else reforzados++;
                            }
                        }

                        pos += snprintf(auditoria + pos, audit_size - pos, "-------------------------------------\n");
                        pos += snprintf(auditoria + pos, audit_size - pos, "[RESUMEN]: Nuevos: %d, Reforzados: %d\n", nuevos, reforzados);
                        
                        uint32_t id_audit = vm_hash_texto(auditoria);
                        jmn_guardar_texto(vm->mem_neuronal, id_audit, auditoria);
                        vm_text_cache_put(vm, id_audit, auditoria);
                        vm_set_register(vm, inst.operand_a, (uint64_t)id_audit);
                        free(auditoria);
                    }
                }
            }
#endif
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }
    
    case OP_FS_FIN_ARCHIVO: {
        // A = dest_reg, B = handle_reg (opcional)
        FILE* f = (inst.flags & IR_INST_FLAG_B_REGISTER) ? (FILE*)(uintptr_t)vm_get_register(vm, inst.operand_b) : vm->current_file;
        int eof = (f && feof(f)) ? 1 : 0;
        vm_set_register(vm, inst.operand_a, (uint64_t)eof);
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }
    case OP_FS_CERRAR: {
        // A = handle_reg (opcional)
        FILE* f = (inst.flags & IR_INST_FLAG_A_REGISTER) ? (FILE*)(uintptr_t)vm_get_register(vm, inst.operand_a) : vm->current_file;
        if (f) {
            fclose(f);
            if (f == vm->current_file) vm->current_file = NULL;
        }
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }

    
    case OP_FS_EXISTE: {
        // A = dest_reg, B = path_reg
        uint64_t b_val = vm_get_register(vm, inst.operand_b);
        const char* path = vm_text_cache_get(vm, (uint32_t)b_val);
        if (path) {
            FILE* file = fopen(path, "r");
            vm_set_register(vm, inst.operand_a, file ? 1 : 0);
            if (file) fclose(file);
        } else {
            vm_set_register(vm, inst.operand_a, 0);
        }
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }
    
    case OP_SYS_TIMESTAMP: {
        // A = dest_reg
        uint32_t timestamp = (uint32_t)time(NULL);
        vm_set_register(vm, inst.operand_a, (uint64_t)timestamp);
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }

    case OP_MEM_OBTENER_TODOS: {
        uint32_t lista_id = 0xA11C04CE; // "ALL CONCEPTS"
#ifdef JASBOOT_LANG_INTEGRATION
        if (vm->mem_neuronal) {
            JMNMemoria* mem = vm->mem_neuronal;
            jmn_crear_lista(mem, lista_id);
            jmn_iterar_nodos(mem, jmn_callback_recolectar_ids, mem);
        }
#endif
        vm_set_register(vm, inst.operand_a, (uint64_t)lista_id);
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }

    case OP_MEM_PENSAR_SIGUIENTE: {
        uint64_t b_val = vm_get_register(vm, inst.operand_b);
#ifdef JASBOOT_LANG_INTEGRATION
        if (vm->mem_neuronal) {
            JMNMemoria* mem = vm->mem_neuronal;
            JMNBusquedaResultado resultados[8];
            int n = jmn_buscar_asociaciones(mem, (uint32_t)b_val, JMN_RELACION_SECUENCIA, 0.1f, 1, resultados, 8);
            if (n <= 0) {
                if (vm->mem_colecciones) {
                    n = jmn_buscar_asociaciones(vm->mem_colecciones, (uint32_t)b_val, JMN_RELACION_SECUENCIA, 0.1f, 1, resultados, 8);
                }
                
                if (n > 0) {
                     vm_set_register(vm, inst.operand_a, (uint64_t)resultados[0].id);
                } else {
                    vm_set_register(vm, inst.operand_a, 0);
                }
            } else {
                  vm_set_register(vm, inst.operand_a, (uint64_t)resultados[0].id);
            }
        } else { vm_set_register(vm, inst.operand_a, 0); }
#else
        vm_set_register(vm, inst.operand_a, 0);
#endif
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }

    case OP_MEM_PENSAR_ANTERIOR:
#ifdef JASBOOT_LANG_INTEGRATION
        if (vm->mem_neuronal) {
             uint64_t b_val = vm_get_register(vm, inst.operand_b);
             JMNMemoria* mem = vm->mem_neuronal;
             JMNBusquedaPadreCtx ctx;
             ctx.target_id = (uint32_t)b_val;
             ctx.found_id = 0;
             ctx.mem = mem;
             jmn_iterar_nodos(mem, jmn_callback_buscar_padre_secuencia, &ctx);
             vm_set_register(vm, inst.operand_a, (uint64_t)ctx.found_id);
        } else { vm_set_register(vm, inst.operand_a, 0); }
#else
        vm_set_register(vm, inst.operand_a, 0);
#endif
        vm->pc += IR_INSTRUCTION_SIZE;
        break;

    case 0xC6: { // OP_MEM_CORREGIR_SECUENCIA
        // A = anterior, B = incorrecto, C = correcto
#ifdef JASBOOT_LANG_INTEGRATION
        if (vm->mem_neuronal) {
            uint64_t a_val = vm_get_register(vm, inst.operand_a);
            uint64_t b_val = vm_get_register(vm, inst.operand_b);
            uint64_t c_val = vm_get_register(vm, inst.operand_c);
            uint32_t id_ant = (uint32_t)a_val;
            uint32_t id_inc = (uint32_t)b_val;
            uint32_t id_cor = (uint32_t)c_val;
            
            // 1. Penalizar relación antigua
            JMNValor v_penal = { .f = -0.5f };
            jmn_agregar_conexion(vm->mem_neuronal, id_ant, id_inc, v_penal, JMN_RELACION_SECUENCIA);
            
            // 2. Reforzar relación correcta
            JMNValor v_cor = { .f = 1.0f };
            jmn_agregar_conexion(vm->mem_neuronal, id_ant, id_cor, v_cor, JMN_RELACION_SECUENCIA);
        }
#endif
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }

    case 0xC7: { // OP_MEM_ASOCIAR_RELACION
        // A = id1, B = id2, C = tipo (low 8 bits) + peso*1000 (bits 8-23); si bits 8-23 == 0 → peso 1.0
#ifdef JASBOOT_LANG_INTEGRATION
        if (vm->mem_neuronal) {
            uint64_t a_val = vm_get_register(vm, inst.operand_a);
            uint64_t b_val = vm_get_register(vm, inst.operand_b);
            uint64_t c_val = vm_get_register(vm, inst.operand_c);
            uint32_t id1 = (uint32_t)a_val;
            uint32_t id2 = (uint32_t)b_val;
            uint32_t tipo = (uint32_t)(c_val & 0xFFu);
            uint32_t peso_x1000 = (uint32_t)((c_val >> 8) & 0xFFFFu);
            float peso = (peso_x1000 != 0) ? ((float)peso_x1000 / 1000.0f) : 1.0f;

            if (tipo > 0 && tipo <= JMN_RELACION_MAX) {
                JMNValor v_peso = { .f = peso };
                jmn_agregar_conexion(vm->mem_neuronal, id1, id2, v_peso, tipo);
                if (tipo == JMN_RELACION_SIMILITUD || tipo == JMN_RELACION_OPOSICION) {
                    jmn_agregar_conexion(vm->mem_neuronal, id2, id1, v_peso, tipo);
                }
            }
        }
#endif
        vm->pc += IR_INSTRUCTION_SIZE;
        break;
    }
            case OP_FS_BORRAR: {
            uint32_t path_id = (uint32_t)vm_get_register(vm, inst.operand_a);
            char path[512];
            int res = -1;
            if (vm_text_cache_get_copy(vm, path_id, path, 512)) {
                if (!strstr(path, "..")) res = remove(path);
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_COPIAR: {
            uint32_t src_id = (uint32_t)vm_get_register(vm, inst.operand_a);
            uint32_t dst_id = (uint32_t)vm_get_register(vm, inst.operand_b);
            char src[512], dst[512];
            int res = -1;
            if (vm_text_cache_get_copy(vm, src_id, src, 512) && 
                vm_text_cache_get_copy(vm, dst_id, dst, 512)) {
                if (!strstr(src, "..") && !strstr(dst, "..")) {
                    FILE *f_src = fopen(src, "rb");
                    if (f_src) {
                        FILE *f_dst = fopen(dst, "wb");
                        if (f_dst) {
                            char buf[4096];
                            size_t n;
                            while ((n = fread(buf, 1, 4096, f_src)) > 0) fwrite(buf, 1, n, f_dst);
                            fclose(f_dst);
                            res = 0;
                        }
                        fclose(f_src);
                    }
                }
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_MOVER: {
            uint32_t src_id = (uint32_t)vm_get_register(vm, inst.operand_a);
            uint32_t dst_id = (uint32_t)vm_get_register(vm, inst.operand_b);
            char src[512], dst[512];
            int res = -1;
            if (vm_text_cache_get_copy(vm, src_id, src, 512) && 
                vm_text_cache_get_copy(vm, dst_id, dst, 512)) {
                 if (!strstr(src, "..") && !strstr(dst, "..")) res = rename(src, dst);
            }
            vm_set_register(vm, inst.operand_a, (uint64_t)res);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        case OP_FS_TAMANO: {
             uint32_t p_id = (uint32_t)vm_get_register(vm, inst.operand_b);
             char path[512];
             int64_t size = -1;
             if (vm_text_cache_get_copy(vm, p_id, path, 512)) {
                 FILE *f = fopen(path, "rb");
                 if (f) {
                     fseek(f, 0, SEEK_END);
                     size = ftell(f);
                     fclose(f);
                 }
             }
             vm_set_register(vm, inst.operand_a, (uint64_t)size);
             vm->pc += IR_INSTRUCTION_SIZE;
             break;
        }

        case OP_MEM_OBTENER_RELACION: {
            uint32_t id_origen = (uint32_t)b_val;
            uint32_t id_destino = (uint32_t)c_val;
            uint32_t tipo = 0;
#ifdef JASBOOT_LANG_INTEGRATION
            if (vm->mem_neuronal && id_origen != 0 && id_destino != 0) {
                 // tipo = jmn_obtener_tipo_relacion(vm->mem_neuronal, id_origen, id_destino);
                 tipo = 0; // Fallback
            }
#endif
            vm_set_register(vm, inst.operand_a, (uint64_t)tipo);
            vm->pc += IR_INSTRUCTION_SIZE;
            break;
        }

        default: {
            char opmsg[96];
            snprintf(opmsg, sizeof opmsg, "ERR: Opcode desconocido 0x%02X en PC=%zu", inst.opcode, (size_t)vm->pc);
            if (vm_try_catch_or_abort(vm, opmsg)) return 0;
            fprintf(stderr, "%s\n", opmsg);
            vm->running = 0;
            vm->exit_code = 1;
            return 0;
        }
    }
    
    return 0;
}

int vm_run_with_limit(VM* vm, uint64_t max_steps) {
    if (!vm || !vm->ir) return 1;
    vm->running = 1;
    vm->exit_code = 0;
    
    // printf("[VM DEBUG] Validating memory...\n");
    IRValidationInfo info = ir_validate_memory(vm->ir);
    if (info.result != IR_VALID_OK) {
        fprintf(stderr, "Error de validación: %s\n", info.message);
        vm->exit_code = 1;
        return 1;
    }
    
    uint64_t steps = 0;
    
    // Optimizacion "computed goto" (si soportado por GCC, de lo contrario un switch unrolled normal)
    uint64_t* regs = vm->registers;
    const uint8_t* code_base = vm->ir->code;
    size_t code_size = vm->ir->header.code_size;

    #if defined(__GNUC__) && !defined(__clang_analyzer__) && 0
    static void* dispatch_table[256] = { 0 };
    
    // Initialization flag for jump table to avoid full typing
    static int jump_init = 0;
    if (!jump_init) {
        for(int i = 0; i < 256; i++) dispatch_table[i] = &&op_default;
        dispatch_table[OP_HALT] = &&op_halt;
        dispatch_table[OP_MOVER] = &&op_mover;
        dispatch_table[OP_LEER] = &&op_leer;
        dispatch_table[OP_ESCRIBIR] = &&op_escribir;
        dispatch_table[OP_SUMAR] = &&op_sumar;
        dispatch_table[OP_RESTAR] = &&op_restar;
        dispatch_table[OP_MULTIPLICAR] = &&op_multiplicar;
        dispatch_table[OP_CMP_EQ] = &&op_cmp_eq;
        dispatch_table[OP_CMP_LT] = &&op_cmp_lt;
        dispatch_table[OP_SI] = &&op_si;
        dispatch_table[OP_IR] = &&op_ir;
        jump_init = 1;
    }
    
    #define DISPATCH() \
        if (vm->pc >= code_size) { vm->running = 0; goto vm_end; } \
        code_ptr = code_base + vm->pc; \
        opcode = code_ptr[0]; flags = code_ptr[1]; op_a = code_ptr[2]; op_b = code_ptr[3]; op_c = code_ptr[4]; \
        if (opcode != OP_DEBUG_LINE) { \
            a_val = (flags & IR_INST_FLAG_A_IMMEDIATE) ? op_a : regs[op_a]; \
            b_val = (flags & IR_INST_FLAG_B_IMMEDIATE) ? op_b : regs[op_b]; \
            c_val = (flags & IR_INST_FLAG_C_IMMEDIATE) ? op_c : regs[op_c]; \
        } \
        goto *dispatch_table[opcode];
        
    const uint8_t* code_ptr;
    uint8_t opcode, flags, op_a, op_b, op_c;
    uint64_t a_val, b_val, c_val;
    
    DISPATCH();
    
    op_sumar:
        regs[op_a] = b_val + c_val;
        vm->pc += IR_INSTRUCTION_SIZE;
        DISPATCH();
        
    op_restar:
        regs[op_a] = b_val - c_val;
        vm->pc += IR_INSTRUCTION_SIZE;
        DISPATCH();
        
    op_multiplicar:
        regs[op_a] = b_val * c_val;
        vm->pc += IR_INSTRUCTION_SIZE;
        DISPATCH();

    op_cmp_eq:
        regs[op_a] = (b_val == c_val) ? 1 : 0;
        vm->pc += IR_INSTRUCTION_SIZE;
        DISPATCH();
        
    op_cmp_lt:
        regs[op_a] = ((int64_t)b_val < (int64_t)c_val) ? 1 : 0;
        vm->pc += IR_INSTRUCTION_SIZE;
        DISPATCH();

    op_si:
        if (a_val != 0) {
            uint64_t addr = b_val;
            if (flags & IR_INST_FLAG_C_IMMEDIATE) addr |= ((uint64_t)op_c << 8);
            vm->pc = addr;
        } else {
            vm->pc += IR_INSTRUCTION_SIZE;
        }
        DISPATCH();

    op_mover:
        {
            uint64_t val = b_val;
            if ((flags & IR_INST_FLAG_B_IMMEDIATE) && (flags & IR_INST_FLAG_C_IMMEDIATE)) {
                 val |= (uint64_t)op_c << 8;
            }
            regs[op_a] = val;
            vm->pc += IR_INSTRUCTION_SIZE;
        }
        DISPATCH();

    op_ir:
        {
            uint64_t addr = a_val;
            if (flags & IR_INST_FLAG_B_IMMEDIATE) addr |= ((uint64_t)op_b << 8);
            if (flags & IR_INST_FLAG_C_IMMEDIATE) addr |= ((uint64_t)op_c << 16);
            vm->pc = addr;
        }
        DISPATCH();
        
    op_debug_line:
        // Ignorado en bucle optimizado por velocidad de ejecucion a menos que se re-habilite (vm->current_line)
        vm->pc += IR_INSTRUCTION_SIZE;
        DISPATCH();

    op_halt:
    op_leer:
    op_escribir:
    op_mover_u24:
    op_load_str_hash:
    op_get_fp:
    op_default:
        if (vm_step(vm) != 0) {
            if (vm->exit_code == 0) vm->exit_code = 1;
            return vm->exit_code;
        }
        DISPATCH();
        
    vm_end:
        ; // End loop
    #else
    while (vm->running) {
        if (vm->pc >= code_size) {
            vm->running = 0;
            break;
        }

        const uint8_t* code_ptr = code_base + vm->pc;
        uint8_t opcode = code_ptr[0];
        uint8_t flags = code_ptr[1];
        uint8_t op_a = code_ptr[2];
        uint8_t op_b = code_ptr[3];
        uint8_t op_c = code_ptr[4];

        uint64_t a_val, b_val, c_val;
        if (opcode != OP_DEBUG_LINE) {
            a_val = (flags & IR_INST_FLAG_A_IMMEDIATE) ? op_a : regs[op_a];
            b_val = (flags & IR_INST_FLAG_B_IMMEDIATE) ? op_b : regs[op_b];
            c_val = (flags & IR_INST_FLAG_C_IMMEDIATE) ? op_c : regs[op_c];
        }

        switch (opcode) {
            case OP_SUMAR:
                regs[op_a] = b_val + c_val;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_RESTAR:
                regs[op_a] = b_val - c_val;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_MULTIPLICAR:
                regs[op_a] = b_val * c_val;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_DIVIDIR:
                if (c_val == 0) {
                    char divmsg[160];
                    if (vm->current_line > 0)
                        snprintf(divmsg, sizeof divmsg,
                                 "Error de ejecucion (VM) en la linea %d: division por cero.", vm->current_line);
                    else
                        snprintf(divmsg, sizeof divmsg, "Error de ejecucion (VM): division por cero.");
                    if (vm_try_catch_or_abort(vm, divmsg))
                        break;
                    fprintf(stderr, "%s\n", divmsg);
                    vm->running = 0;
                    vm->exit_code = 1;
                    return vm->exit_code;
                }
                regs[op_a] = b_val / c_val;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_MODULO:
                if (c_val == 0) {
                    char modmsg[160];
                    if (vm->current_line > 0)
                        snprintf(modmsg, sizeof modmsg,
                                 "Error de ejecucion (VM) en la linea %d: modulo por cero.", vm->current_line);
                    else
                        snprintf(modmsg, sizeof modmsg, "Error de ejecucion (VM): modulo por cero.");
                    if (vm_try_catch_or_abort(vm, modmsg))
                        break;
                    fprintf(stderr, "%s\n", modmsg);
                    vm->running = 0;
                    vm->exit_code = 1;
                    return vm->exit_code;
                }
                regs[op_a] = b_val % c_val;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_CMP_EQ:
                regs[op_a] = (b_val == c_val) ? 1 : 0;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_CMP_LT:
                regs[op_a] = ((int64_t)b_val < (int64_t)c_val) ? 1 : 0;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            case OP_SI:
                if (a_val != 0) {
                    uint64_t addr = b_val;
                    if (flags & IR_INST_FLAG_C_IMMEDIATE) addr |= ((uint64_t)op_c << 8);
                    vm->pc = addr;
                } else {
                    vm->pc += IR_INSTRUCTION_SIZE;
                }
                break;
            case OP_MOVER: {
                uint64_t val = b_val;
                if ((flags & IR_INST_FLAG_B_IMMEDIATE) && (flags & IR_INST_FLAG_C_IMMEDIATE)) {
                     val |= (uint64_t)op_c << 8;
                }
                regs[op_a] = val;
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            }
            case OP_IR: {
                uint64_t addr = a_val;
                if (flags & IR_INST_FLAG_B_IMMEDIATE) addr |= ((uint64_t)op_b << 8);
                if (flags & IR_INST_FLAG_C_IMMEDIATE) addr |= ((uint64_t)op_c << 16);
                vm->pc = addr;
                break;
            }
            case OP_DEBUG_LINE:
                // vm->current_line = (int)(b_val | (c_val << 8));
                vm->pc += IR_INSTRUCTION_SIZE;
                break;
            default:
                if (vm_step(vm) != 0) {
                    if (vm->exit_code == 0) vm->exit_code = 1;
                    return vm->exit_code;
                }
                break;
        }
        steps++;
        if (max_steps > 0 && steps > max_steps) {
            fprintf(stderr, "Error: VM excedió el límite de pasos (%llu).\n", (unsigned long long)max_steps);
            vm->running = 0;
            vm->exit_code = 124;
            break;
        }
    }
    #endif
    
    return vm->exit_code;
}

int vm_run(VM* vm) {
    return vm_run_with_limit(vm, 0); // 0 = Sin límite forzoso
}
