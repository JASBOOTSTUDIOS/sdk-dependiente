/**
 * n_grafo_core.c - Implementación de n_grafo (Fases 1-6)
 * Formato .ngf: cabecera + vocabulario + triples
 * Fase 6.1: Memory-mapped I/O (POSIX)
 * Fase 6.2: Triples ordenados por (S,P) al guardar
 */

#include "n_grafo.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#if !defined(_WIN32) && !defined(_WIN64)
#define NGF_USE_MMAP 1
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

/* Estructura del header en disco */
typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint8_t version;
    uint8_t flags;
    uint8_t reserved[2];
    uint32_t vocab_off;
    uint32_t vocab_len;
    uint32_t triples_off;
    uint32_t triples_len;
    uint32_t next_id;
} NGFHeader;

/* Entrada de vocabulario en memoria */
typedef struct {
    uint32_t id;
    uint32_t hash;
    uint16_t text_len;
    char* text;
} NGFVocabEntry;

#define NGF_HASH_BUCKETS 4096
#define NGF_INDEX_BUCKETS 8192
#define NGF_TEXT_BUF_MAX 4096

#define NGF_KEY_SP(s, p) (((uint64_t)(s) << 32) | (uint32_t)(p))
#define NGF_KEY_PO(p, o) (((uint64_t)(p) << 32) | (uint32_t)(o))
#define NGF_SP_S(k) ((uint32_t)((k) >> 32))
#define NGF_SP_P(k) ((uint32_t)(k))

/* Nodo de índice: clave -> lista de valores */
typedef struct NGFIndexNode NGFIndexNode;
struct NGFIndexNode {
    uint64_t key;
    uint32_t* vals_u32;   /* para sp y po */
    uint64_t* vals_u64;   /* para o_index (s,p) empaquetados */
    size_t n, cap;
    int is_u64;           /* 1 = vals_u64, 0 = vals_u32 */
    NGFIndexNode* next;
};

/* Triple */
typedef struct __attribute__((packed)) {
    uint32_t s;
    uint32_t p;
    uint32_t o;
    uint8_t peso;
} NGFTriple;

/* Índice hash: cada bucket apunta al primer vocab index en la cadena */
struct NGrafo {
    FILE* file;
    char* ruta;
    uint32_t next_id;
    NGFVocabEntry* vocab;
    size_t vocab_count;
    size_t vocab_cap;
    int32_t* vocab_hash_next;  /* siguiente en cadena de hash, -1 = fin */
    uint32_t* hash_buckets;    /* hash_buckets[h % NGF_HASH_BUCKETS] = vocab index */
    NGFTriple* triples;
    size_t triples_count;
    size_t triples_cap;
    int dirty;
    /* Fase 4: índices para búsqueda O(1) */
    NGFIndexNode** sp_index;   /* (S,P) -> [O] */
    NGFIndexNode** po_index;   /* (P,O) -> [S] */
    NGFIndexNode** o_index;    /* O -> [(S,P)] */
    /* Fase 6.1: memory-mapped I/O */
    void* mmap_base;           /* base del mapeo (NULL si no usa mmap) */
    size_t mmap_len;           /* longitud mapeada */
    /* Fase 7: caché y optimización */
    uint32_t* lru_hash;        /* LRU: hash de entradas cacheadas */
    size_t* lru_idx;           /* LRU: índice en vocab */
    size_t lru_size;           /* tamaño configurado del caché */
    size_t lru_n;              /* entradas actuales (0..lru_size) */
    uint8_t* bloom_bits;       /* Bloom filter para n_existe_concepto */
    size_t bloom_bits_len;     /* longitud en bytes */
};

/* Forward declarations para índices (Fase 4) */
static void n_grafo_indice_liberar(NGFIndexNode** buckets);
static void n_grafo_reconstruir_indices(NGrafo* g);
static void n_grafo_bloom_reconstruir(NGrafo* g);

#define NGF_BLOOM_K 3  /* número de hashes para Bloom */

/* FNV-1a 32-bit */
static uint32_t n_hash_fnv1a(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

#define NGF_BUCKET_EMPTY ((uint32_t)-1)

/* Reconstruir índice hash desde vocab (tras cargar o al iniciar) */
static void n_grafo_reconstruir_hash(NGrafo* g) {
    if (!g->hash_buckets) {
        g->hash_buckets = (uint32_t*)malloc(NGF_HASH_BUCKETS * sizeof(uint32_t));
        if (g->hash_buckets)
            for (uint32_t i = 0; i < NGF_HASH_BUCKETS; i++) g->hash_buckets[i] = NGF_BUCKET_EMPTY;
    } else {
        for (uint32_t i = 0; i < NGF_HASH_BUCKETS; i++) g->hash_buckets[i] = NGF_BUCKET_EMPTY;
    }
    if (!g->vocab_hash_next) {
        g->vocab_hash_next = (int32_t*)malloc((g->vocab_cap ? g->vocab_cap : 64) * sizeof(int32_t));
    }
    size_t need = g->vocab_count > 0 ? g->vocab_count : 64;
    if (!g->vocab_hash_next || need > (g->vocab_cap ? g->vocab_cap : 0)) {
        int32_t* tmp = (int32_t*)realloc(g->vocab_hash_next, need * sizeof(int32_t));
        if (tmp) { g->vocab_hash_next = tmp; g->vocab_cap = need; }
    }
    for (size_t i = 0; i < g->vocab_count && g->vocab_hash_next; i++) {
        uint32_t h = g->vocab[i].hash % NGF_HASH_BUCKETS;
        g->vocab_hash_next[i] = (int32_t)g->hash_buckets[h];
        g->hash_buckets[h] = (uint32_t)i;
    }
}

/* Normalización: minúsculas (ASCII), trim */
static void n_normalizar(char* out, const char* in, size_t max_len) {
    size_t i = 0;
    while (in[i] && i < max_len - 1) {
        out[i] = (char)tolower((unsigned char)in[i]);
        i++;
    }
    out[i] = '\0';
    /* Trim final */
    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t')) {
        out[--i] = '\0';
    }
}

static int n_grafo_crear_vacio(NGrafo* g, const char* ruta) {
    g->file = fopen(ruta, "wb");
    if (!g->file) return -1;

    NGFHeader h;
    memset(&h, 0, sizeof(h));
    h.magic[0] = NGF_MAGIC_0;
    h.magic[1] = NGF_MAGIC_1;
    h.magic[2] = NGF_MAGIC_2;
    h.magic[3] = NGF_MAGIC_3;
    h.version = NGF_VERSION_1;
    h.vocab_off = NGF_HEADER_SIZE;
    h.vocab_len = 0;
    h.triples_off = NGF_HEADER_SIZE;
    h.triples_len = 0;
    h.next_id = 1;

    if (fwrite(&h, 1, sizeof(h), g->file) != sizeof(h)) {
        fclose(g->file);
        g->file = NULL;
        return -1;
    }
    fclose(g->file);
    g->file = NULL;
    return 0;
}

static int n_grafo_leer_header(NGrafo* g) {
    NGFHeader h;
    if (fseek(g->file, 0, SEEK_SET) != 0) return -1;
    if (fread(&h, 1, sizeof(h), g->file) != sizeof(h)) return -1;

    if (h.magic[0] != NGF_MAGIC_0 || h.magic[1] != NGF_MAGIC_1 ||
        h.magic[2] != NGF_MAGIC_2 || h.magic[3] != NGF_MAGIC_3) {
        return -1; /* Formato inválido */
    }
    if (h.version != NGF_VERSION_1) return -1;

    g->next_id = h.next_id;
    return 0;
}

static int n_grafo_cargar_vocab(NGrafo* g) {
    NGFHeader h;
    fseek(g->file, 0, SEEK_SET);
    fread(&h, 1, sizeof(h), g->file);

    if (h.vocab_len == 0) return 0;

    fseek(g->file, (long)h.vocab_off, SEEK_SET);
    size_t remain = h.vocab_len;

    while (remain >= 10) { /* id(4) + hash(4) + text_len(2) */
        uint32_t id, hash;
        uint16_t text_len;
        if (fread(&id, 4, 1, g->file) != 1) break;
        if (fread(&hash, 4, 1, g->file) != 1) break;
        if (fread(&text_len, 2, 1, g->file) != 1) break;
        remain -= 10;

        char* text = NULL;
        if (text_len > 0) {
            text = (char*)malloc(text_len + 1);
            if (text && fread(text, 1, text_len, g->file) == text_len) {
                text[text_len] = '\0';
                remain -= text_len;
            } else {
                free(text);
                text = NULL;
            }
        }

        if (g->vocab_count >= g->vocab_cap) {
            size_t new_cap = g->vocab_cap ? g->vocab_cap * 2 : 64;
            NGFVocabEntry* tmp = (NGFVocabEntry*)realloc(g->vocab, new_cap * sizeof(NGFVocabEntry));
            if (!tmp) { free(text); break; }
            g->vocab = tmp;
            g->vocab_cap = new_cap;
        }
        g->vocab[g->vocab_count].id = id;
        g->vocab[g->vocab_count].hash = hash;
        g->vocab[g->vocab_count].text_len = text_len;
        g->vocab[g->vocab_count].text = text;
        g->vocab_count++;
    }
    return 0;
}

/* Comparador para ordenar triples por (S, P) - Fase 6.2 */
static int n_grafo_triple_cmp_sp(const void* a, const void* b) {
    const NGFTriple* ta = (const NGFTriple*)a;
    const NGFTriple* tb = (const NGFTriple*)b;
    if (ta->s != tb->s) return (ta->s > tb->s) ? 1 : -1;
    if (ta->p != tb->p) return (ta->p > tb->p) ? 1 : -1;
    return (ta->o > tb->o) ? 1 : (ta->o < tb->o ? -1 : 0);
}

/* Pasar de triples en mmap a malloc (cuando vamos a modificar) */
static void n_grafo_desmapear_triples(NGrafo* g) {
#if defined(NGF_USE_MMAP)
    if (!g->mmap_base || !g->triples || g->triples_count == 0) return;
    size_t n = g->triples_count;
    NGFTriple* copy = (NGFTriple*)malloc(n * sizeof(NGFTriple));
    if (!copy) return;
    memcpy(copy, g->triples, n * sizeof(NGFTriple));
    munmap(g->mmap_base, g->mmap_len);
    g->mmap_base = NULL;
    g->mmap_len = 0;
    g->triples = copy;
    g->triples_cap = n;
#else
    (void)g;
#endif
}

static int n_grafo_cargar_triples(NGrafo* g) {
    NGFHeader h;
    fseek(g->file, 0, SEEK_SET);
    fread(&h, 1, sizeof(h), g->file);

    if (h.triples_len == 0) return 0;

    size_t n = h.triples_len / NGF_TRIPLE_SIZE;
    g->triples = (NGFTriple*)malloc(n * sizeof(NGFTriple));
    if (!g->triples) return -1;
    g->triples_cap = n;
    g->triples_count = 0;

    fseek(g->file, (long)h.triples_off, SEEK_SET);
    for (size_t i = 0; i < n; i++) {
        if (fread(&g->triples[i], 1, NGF_TRIPLE_SIZE, g->file) != NGF_TRIPLE_SIZE) break;
        g->triples_count++;
    }
    return 0;
}

#ifdef NGF_USE_MMAP
/* Cargar vocab y triples vía mmap: acceso bajo demanda */
static int n_grafo_cargar_con_mmap(NGrafo* g, int fd, const NGFHeader* h) {
    size_t file_len = (size_t)h->vocab_off + h->vocab_len + h->triples_len;
    if (file_len == 0) return 0;
    void* base = mmap(NULL, file_len, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) return -1;
    g->mmap_base = base;
    g->mmap_len = file_len;

    /* Parsear vocab desde mmap (copiamos text para añadir '\0') */
    if (h->vocab_len > 0) {
        const char* p = (const char*)base + h->vocab_off;
        size_t remain = h->vocab_len;
        while (remain >= 10) {
            uint32_t id, hash;
            uint16_t text_len;
            memcpy(&id, p, 4); p += 4;
            memcpy(&hash, p, 4); p += 4;
            memcpy(&text_len, p, 2); p += 2;
            remain -= 10;
            char* text = NULL;
            if (text_len > 0 && remain >= text_len) {
                text = (char*)malloc(text_len + 1);
                if (text) {
                    memcpy(text, p, text_len);
                    text[text_len] = '\0';
                    remain -= text_len;
                    p += text_len;
                }
            }
            if (g->vocab_count >= g->vocab_cap) {
                size_t new_cap = g->vocab_cap ? g->vocab_cap * 2 : 64;
                NGFVocabEntry* tmp = (NGFVocabEntry*)realloc(g->vocab, new_cap * sizeof(NGFVocabEntry));
                if (!tmp) { free(text); break; }
                g->vocab = tmp;
                g->vocab_cap = new_cap;
            }
            g->vocab[g->vocab_count].id = id;
            g->vocab[g->vocab_count].hash = hash;
            g->vocab[g->vocab_count].text_len = text_len;
            g->vocab[g->vocab_count].text = text;
            g->vocab_count++;
        }
    }

    /* Triples apuntan directamente al mmap (sin copia) */
    if (h->triples_len > 0) {
        g->triples = (NGFTriple*)((char*)base + h->triples_off);
        g->triples_count = h->triples_len / NGF_TRIPLE_SIZE;
        g->triples_cap = g->triples_count;
    }
    return 0;
}
#endif

static int n_grafo_guardar(NGrafo* g) {
    if (!g->dirty) return 0;

    g->file = fopen(g->ruta, "r+b");
    if (!g->file) g->file = fopen(g->ruta, "wb");
    if (!g->file) return -1;

    uint32_t vocab_len = 0;
    for (size_t i = 0; i < g->vocab_count; i++) {
        vocab_len += 4 + 4 + 2 + g->vocab[i].text_len;
    }

    uint32_t triples_len = (uint32_t)(g->triples_count * NGF_TRIPLE_SIZE);
    uint32_t vocab_off = NGF_HEADER_SIZE;
    uint32_t triples_off = vocab_off + vocab_len;

    NGFHeader h;
    memset(&h, 0, sizeof(h));
    h.magic[0] = NGF_MAGIC_0;
    h.magic[1] = NGF_MAGIC_1;
    h.magic[2] = NGF_MAGIC_2;
    h.magic[3] = NGF_MAGIC_3;
    h.version = NGF_VERSION_1;
    h.vocab_off = vocab_off;
    h.vocab_len = vocab_len;
    h.triples_off = triples_off;
    h.triples_len = triples_len;
    h.next_id = g->next_id;

    fseek(g->file, 0, SEEK_SET);
    fwrite(&h, 1, sizeof(h), g->file);

    for (size_t i = 0; i < g->vocab_count; i++) {
        fwrite(&g->vocab[i].id, 4, 1, g->file);
        fwrite(&g->vocab[i].hash, 4, 1, g->file);
        fwrite(&g->vocab[i].text_len, 2, 1, g->file);
        if (g->vocab[i].text_len > 0)
            fwrite(g->vocab[i].text, 1, g->vocab[i].text_len, g->file);
    }

    /* Fase 6.2: ordenar triples por (S,P) para búsqueda secuencial y compresión */
    n_grafo_desmapear_triples(g);  /* asegurar que podemos modificar */
    if (g->triples_count > 1 && g->triples)
        qsort(g->triples, g->triples_count, sizeof(NGFTriple), n_grafo_triple_cmp_sp);

    if (g->triples_count > 0 && g->triples)
        fwrite(g->triples, NGF_TRIPLE_SIZE, g->triples_count, g->file);
    fflush(g->file);
    fclose(g->file);
    g->file = NULL;
    g->dirty = 0;
    return 0;
}

NGrafo* n_abrir_grafo(const char* ruta) {
    if (!ruta || !ruta[0]) return NULL;

    NGrafo* g = (NGrafo*)calloc(1, sizeof(NGrafo));
    if (!g) return NULL;

    g->ruta = strdup(ruta);
    if (!g->ruta) { free(g); return NULL; }

    g->file = fopen(ruta, "rb");
    if (!g->file) {
        if (n_grafo_crear_vacio(g, ruta) != 0) {
            free(g->ruta);
            free(g);
            return NULL;
        }
        g->file = fopen(ruta, "r+b");
    }

    if (!g->file) {
        free(g->ruta);
        free(g);
        return NULL;
    }

    if (n_grafo_leer_header(g) != 0) {
        fclose(g->file);
        free(g->ruta);
        free(g);
        return NULL;
    }

#ifdef NGF_USE_MMAP
    {
        NGFHeader h;
        fseek(g->file, 0, SEEK_SET);
        fread(&h, 1, sizeof(h), g->file);
        if (h.vocab_len > 0 || h.triples_len > 0) {
            int fd = fileno(g->file);
            if (fd >= 0 && n_grafo_cargar_con_mmap(g, fd, &h) == 0) {
                fclose(g->file);
                g->file = NULL;
                n_grafo_reconstruir_hash(g);
                n_grafo_reconstruir_indices(g);
                n_grafo_bloom_reconstruir(g);
                return g;
            }
            /* mmap falló o no aplica: limpiar vocab parcial y continuar con carga tradicional */
            if (g->mmap_base) {
                munmap(g->mmap_base, g->mmap_len);
                g->mmap_base = NULL;
                g->mmap_len = 0;
            }
            if (g->vocab_count > 0) {
                for (size_t i = 0; i < g->vocab_count; i++) free(g->vocab[i].text);
                free(g->vocab);
                g->vocab = NULL;
                g->vocab_count = g->vocab_cap = 0;
            }
            g->triples = NULL;
            g->triples_count = g->triples_cap = 0;
        }
    }
#endif

    n_grafo_cargar_vocab(g);
    n_grafo_cargar_triples(g);
    fclose(g->file);
    g->file = NULL;

    n_grafo_reconstruir_hash(g);
    n_grafo_reconstruir_indices(g);
    n_grafo_bloom_reconstruir(g);  /* Fase 7.2: Bloom filter */
    return g;
}

void n_cerrar_grafo(NGrafo* g) {
    if (!g) return;
    n_grafo_guardar(g);
    n_grafo_indice_liberar(g->sp_index);
    n_grafo_indice_liberar(g->po_index);
    n_grafo_indice_liberar(g->o_index);
    for (size_t i = 0; i < g->vocab_count; i++)
        free(g->vocab[i].text);
    free(g->vocab);
    free(g->vocab_hash_next);
    free(g->hash_buckets);
    free(g->lru_hash);
    free(g->lru_idx);
    free(g->bloom_bits);
#if defined(NGF_USE_MMAP)
    if (g->mmap_base) {
        munmap(g->mmap_base, g->mmap_len);
    } else
#endif
    if (g->triples) {
        free(g->triples);
    }
    free(g->ruta);
    free(g);
}

int n_grafo_valido(const NGrafo* g) {
    return g != NULL;
}

/* Bloom filter: bits para h1, h1+h2, h1+2*h2 (mod num_bits) */
static void n_grafo_bloom_add(NGrafo* g, uint32_t hash_val) {
    if (!g->bloom_bits || g->bloom_bits_len == 0) return;
    size_t m = g->bloom_bits_len * 8;
    uint32_t h1 = hash_val;
    uint32_t h2 = (hash_val * 0x85ebca6b) ^ (hash_val >> 16);
    for (int i = 0; i < NGF_BLOOM_K; i++) {
        size_t bit = (h1 + (uint32_t)i * h2) % m;
        g->bloom_bits[bit / 8] |= (uint8_t)(1 << (bit % 8));
    }
}

static int n_grafo_bloom_maybe(const NGrafo* g, uint32_t hash_val) {
    if (!g->bloom_bits || g->bloom_bits_len == 0) return 1; /* sin bloom: siempre "maybe" */
    size_t m = g->bloom_bits_len * 8;
    uint32_t h1 = hash_val;
    uint32_t h2 = (hash_val * 0x85ebca6b) ^ (hash_val >> 16);
    for (int i = 0; i < NGF_BLOOM_K; i++) {
        size_t bit = (h1 + (uint32_t)i * h2) % m;
        if (!(g->bloom_bits[bit / 8] & (uint8_t)(1 << (bit % 8))))
            return 0; /* definitivamente no está */
    }
    return 1; /* quizá está */
}

static void n_grafo_bloom_reconstruir(NGrafo* g) {
    if (!g->bloom_bits || g->bloom_bits_len == 0) return;
    memset(g->bloom_bits, 0, g->bloom_bits_len);
    for (size_t i = 0; i < g->vocab_count; i++)
        n_grafo_bloom_add(g, g->vocab[i].hash);
}

/* LRU: mover entrada a frente (índice pos en lru_hash/lru_idx) */
static void n_grafo_lru_tocar(NGrafo* g, size_t pos) {
    if (pos == 0 || !g->lru_hash || !g->lru_idx) return;
    uint32_t th = g->lru_hash[pos];
    size_t ti = g->lru_idx[pos];
    memmove(&g->lru_hash[1], &g->lru_hash[0], pos * sizeof(uint32_t));
    memmove(&g->lru_idx[1], &g->lru_idx[0], pos * sizeof(size_t));
    g->lru_hash[0] = th;
    g->lru_idx[0] = ti;
}

/* LRU: buscar en caché, devolver vocab_idx o (size_t)-1 */
static size_t n_grafo_lru_buscar(NGrafo* g, uint32_t hash_val, const char* texto_norm) {
    if (!g->lru_hash || !g->lru_idx || g->lru_n == 0) return (size_t)-1;
    for (size_t i = 0; i < g->lru_n; i++) {
        if (g->lru_hash[i] == hash_val) {
            size_t idx = g->lru_idx[i];
            if (idx < g->vocab_count && g->vocab[idx].text &&
                strcmp(g->vocab[idx].text, texto_norm) == 0) {
                n_grafo_lru_tocar(g, i);
                return idx;
            }
        }
    }
    return (size_t)-1;
}

/* LRU: añadir (hash, idx) al frente, evictar si lleno */
static void n_grafo_lru_anadir(NGrafo* g, uint32_t hash_val, size_t vocab_idx) {
    if (!g->lru_hash || !g->lru_idx || g->lru_size == 0) return;
    if (g->lru_n < g->lru_size) {
        memmove(&g->lru_hash[1], &g->lru_hash[0], g->lru_n * sizeof(uint32_t));
        memmove(&g->lru_idx[1], &g->lru_idx[0], g->lru_n * sizeof(size_t));
        g->lru_n++;
    }
    g->lru_hash[0] = hash_val;
    g->lru_idx[0] = vocab_idx;
}

/* Buscar en índice hash por texto normalizado. Devuelve índice en vocab o (size_t)-1 */
static size_t n_grafo_buscar_por_texto(NGrafo* g, const char* texto_norm, uint32_t hash_val) {
    if (!g || !g->hash_buckets || !g->vocab_hash_next) return (size_t)-1;
    /* Consultar LRU primero */
    size_t lru_idx = n_grafo_lru_buscar(g, hash_val, texto_norm);
    if (lru_idx != (size_t)-1) return lru_idx;
    /* Búsqueda normal en hash */
    uint32_t h = hash_val % NGF_HASH_BUCKETS;
    size_t idx = g->hash_buckets[h];
    while (idx != NGF_BUCKET_EMPTY && idx < g->vocab_count) {
        if (g->vocab[idx].hash == hash_val && g->vocab[idx].text &&
            strcmp(g->vocab[idx].text, texto_norm) == 0) {
            n_grafo_lru_anadir(g, hash_val, idx);
            return idx;
        }
        idx = (size_t)g->vocab_hash_next[idx];
    }
    return (size_t)-1;
}

uint32_t n_obtener_id(NGrafo* g, const char* texto) {
    if (!g || !texto) return NGF_ID_NULO;
    char norm[NGF_TEXT_BUF_MAX];
    n_normalizar(norm, texto, sizeof(norm));
    size_t len = strlen(norm);
    if (len == 0) return NGF_ID_NULO;
    uint32_t hash_val = n_hash_fnv1a(norm, len);
    size_t idx = n_grafo_buscar_por_texto(g, norm, hash_val);
    if (idx != (size_t)-1)
        return g->vocab[idx].id;
    /* Crear nuevo concepto */
    if (g->vocab_count >= g->vocab_cap || !g->vocab) {
        size_t new_cap = (g->vocab_cap ? g->vocab_cap * 2 : 64);
        NGFVocabEntry* tv = (NGFVocabEntry*)realloc(g->vocab, new_cap * sizeof(NGFVocabEntry));
        int32_t* tn = (int32_t*)realloc(g->vocab_hash_next, new_cap * sizeof(int32_t));
        if (!tv || !tn) return NGF_ID_NULO;
        g->vocab = tv;
        g->vocab_hash_next = tn;
        g->vocab_cap = new_cap;
    }
    size_t i = g->vocab_count;
    g->vocab[i].id = g->next_id++;
    g->vocab[i].hash = hash_val;
    g->vocab[i].text_len = (uint16_t)(len > 65535 ? 65535 : len);
    g->vocab[i].text = strdup(norm);
    if (!g->vocab[i].text) return NGF_ID_NULO;
    g->vocab_count++;
    n_grafo_bloom_add(g, hash_val);  /* Fase 7.2: Bloom filter */
    /* Insertar en hash */
    if (g->hash_buckets) {
        uint32_t h = hash_val % NGF_HASH_BUCKETS;
        g->vocab_hash_next[i] = (int32_t)g->hash_buckets[h];
        g->hash_buckets[h] = (uint32_t)i;
    }
    g->dirty = 1;
    return g->vocab[i].id;
}

int n_obtener_texto(const NGrafo* g, uint32_t id, char* buf, size_t buf_size) {
    if (!g || !buf || buf_size == 0) return 0;
    buf[0] = '\0';
    if (id == NGF_ID_NULO) return 0;
    for (size_t i = 0; i < g->vocab_count; i++) {
        if (g->vocab[i].id == id && g->vocab[i].text) {
            size_t n = (size_t)g->vocab[i].text_len;
            if (n >= buf_size) n = buf_size - 1;
            memcpy(buf, g->vocab[i].text, n);
            buf[n] = '\0';
            return 1;
        }
    }
    return 0;
}

int n_existe_concepto(const NGrafo* g, const char* texto) {
    if (!g || !texto) return 0;
    char norm[NGF_TEXT_BUF_MAX];
    n_normalizar(norm, texto, sizeof(norm));
    size_t len = strlen(norm);
    if (len == 0) return 0;
    uint32_t hash_val = n_hash_fnv1a(norm, len);
    /* Bloom filter: si definitivamente no está, evitar I/O y búsqueda completa */
    if (!n_grafo_bloom_maybe(g, hash_val)) return 0;
    return n_grafo_buscar_por_texto((NGrafo*)g, norm, hash_val) != (size_t)-1 ? 1 : 0;
}

/* --- Fase 4: Índices hash para búsqueda O(1) --- */

static uint32_t n_hash_u64(uint64_t k) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 8; i++) {
        h ^= (uint8_t)(k >> (i * 8));
        h *= 16777619u;
    }
    return h;
}

static NGFIndexNode* n_index_find(NGFIndexNode** buckets, uint64_t key) {
    uint32_t h = n_hash_u64(key) % NGF_INDEX_BUCKETS;
    NGFIndexNode* n = buckets[h];
    while (n) {
        if (n->key == key) return n;
        n = n->next;
    }
    return NULL;
}

static NGFIndexNode* n_index_get_or_create(NGrafo* g, NGFIndexNode*** buckets_ptr, uint64_t key, int use_u64) {
    if (!*buckets_ptr) {
        *buckets_ptr = (NGFIndexNode**)calloc(NGF_INDEX_BUCKETS, sizeof(NGFIndexNode*));
        if (!*buckets_ptr) return NULL;
    }
    uint32_t h = n_hash_u64(key) % NGF_INDEX_BUCKETS;
    NGFIndexNode* n = (*buckets_ptr)[h];
    while (n) {
        if (n->key == key) return n;
        n = n->next;
    }
    n = (NGFIndexNode*)calloc(1, sizeof(NGFIndexNode));
    if (!n) return NULL;
    n->key = key;
    n->is_u64 = use_u64;
    n->next = (*buckets_ptr)[h];
    (*buckets_ptr)[h] = n;
    return n;
}

static int n_index_append_u32(NGFIndexNode* n, uint32_t v) {
    if (n->n >= n->cap) {
        size_t new_cap = n->cap ? n->cap * 2 : 4;
        uint32_t* t = (uint32_t*)realloc(n->vals_u32, new_cap * sizeof(uint32_t));
        if (!t) return 0;
        n->vals_u32 = t;
        n->cap = new_cap;
    }
    n->vals_u32[n->n++] = v;
    return 1;
}

static int n_index_append_u64(NGFIndexNode* n, uint64_t v) {
    if (n->n >= n->cap) {
        size_t new_cap = n->cap ? n->cap * 2 : 4;
        uint64_t* t = (uint64_t*)realloc(n->vals_u64, new_cap * sizeof(uint64_t));
        if (!t) return 0;
        n->vals_u64 = t;
        n->cap = new_cap;
    }
    n->vals_u64[n->n++] = v;
    return 1;
}

static void n_index_remove_u32(NGFIndexNode* n, uint32_t v) {
    for (size_t i = 0; i < n->n; i++) {
        if (n->vals_u32[i] == v) {
            n->vals_u32[i] = n->vals_u32[n->n - 1];
            n->n--;
            return;
        }
    }
}

static void n_index_remove_u64_val(NGFIndexNode* n, uint64_t v) {
    for (size_t i = 0; i < n->n; i++) {
        if (n->vals_u64[i] == v) {
            n->vals_u64[i] = n->vals_u64[n->n - 1];
            n->n--;
            return;
        }
    }
}

static void n_grafo_indice_agregar(NGrafo* g, uint32_t s, uint32_t p, uint32_t o) {
    uint64_t ksp = NGF_KEY_SP(s, p);
    uint64_t kpo = NGF_KEY_PO(p, o);
    NGFIndexNode* nsp = n_index_get_or_create(g, &g->sp_index, ksp, 0);
    NGFIndexNode* npo = n_index_get_or_create(g, &g->po_index, kpo, 0);
    NGFIndexNode* no = n_index_get_or_create(g, &g->o_index, (uint64_t)o, 1);
    if (nsp) n_index_append_u32(nsp, o);
    if (npo) n_index_append_u32(npo, s);
    if (no) n_index_append_u64(no, ksp);
}

static void n_grafo_indice_quitar(NGrafo* g, uint32_t s, uint32_t p, uint32_t o) {
    uint64_t ksp = NGF_KEY_SP(s, p);
    uint64_t kpo = NGF_KEY_PO(p, o);
    NGFIndexNode* nsp = g->sp_index ? n_index_find(g->sp_index, ksp) : NULL;
    NGFIndexNode* npo = g->po_index ? n_index_find(g->po_index, kpo) : NULL;
    NGFIndexNode* no = g->o_index ? n_index_find(g->o_index, (uint64_t)o) : NULL;
    if (nsp) n_index_remove_u32(nsp, o);
    if (npo) n_index_remove_u32(npo, s);
    if (no) n_index_remove_u64_val(no, ksp);
}

static void n_grafo_indice_liberar(NGFIndexNode** buckets) {
    if (!buckets) return;
    for (uint32_t i = 0; i < NGF_INDEX_BUCKETS; i++) {
        NGFIndexNode* n = buckets[i];
        while (n) {
            NGFIndexNode* next = n->next;
            free(n->vals_u32);
            free(n->vals_u64);
            free(n);
            n = next;
        }
    }
    free(buckets);
}

static void n_grafo_reconstruir_indices(NGrafo* g) {
    n_grafo_indice_liberar(g->sp_index);
    n_grafo_indice_liberar(g->po_index);
    n_grafo_indice_liberar(g->o_index);
    g->sp_index = g->po_index = g->o_index = NULL;
    for (size_t i = 0; i < g->triples_count; i++) {
        n_grafo_indice_agregar(g, g->triples[i].s, g->triples[i].p, g->triples[i].o);
    }
}

/* --- Fase 3: Operaciones sobre triples --- */

static int n_grafo_triple_agregar(NGrafo* g, uint32_t s, uint32_t p, uint32_t o, uint8_t peso) {
    if (!g || s == NGF_ID_NULO || p == NGF_ID_NULO || o == NGF_ID_NULO) return 0;
    /* Si los triples están en mmap, copiar a malloc antes de modificar */
    n_grafo_desmapear_triples(g);

    if (g->triples_count >= g->triples_cap || !g->triples) {
        size_t new_cap = (g->triples_cap ? g->triples_cap * 2 : 64);
        NGFTriple* tmp = (NGFTriple*)realloc(g->triples, new_cap * sizeof(NGFTriple));
        if (!tmp) return 0;
        g->triples = tmp;
        g->triples_cap = new_cap;
    }
    g->triples[g->triples_count].s = s;
    g->triples[g->triples_count].p = p;
    g->triples[g->triples_count].o = o;
    g->triples[g->triples_count].peso = peso;
    g->triples_count++;
    g->dirty = 1;
    n_grafo_indice_agregar(g, s, p, o);
    return 1;
}

int n_recordar(NGrafo* g, const char* sujeto, const char* predicado, const char* objeto) {
    if (!g || !sujeto || !predicado || !objeto) return 0;
    uint32_t s = n_obtener_id(g, sujeto);
    uint32_t p = n_obtener_id(g, predicado);
    uint32_t o = n_obtener_id(g, objeto);
    return n_grafo_triple_agregar(g, s, p, o, 255);
}

int n_recordar_peso(NGrafo* g, const char* sujeto, const char* predicado, const char* objeto, uint8_t peso) {
    if (!g || !sujeto || !predicado || !objeto) return 0;
    uint32_t s = n_obtener_id(g, sujeto);
    uint32_t p = n_obtener_id(g, predicado);
    uint32_t o = n_obtener_id(g, objeto);
    return n_grafo_triple_agregar(g, s, p, o, peso);
}

static uint32_t n_grafo_resolver_id(const NGrafo* g, const char* texto) {
    if (!texto) return NGF_ID_NULO;
    char norm[NGF_TEXT_BUF_MAX];
    n_normalizar(norm, texto, sizeof(norm));
    if (norm[0] == '\0') return NGF_ID_NULO;
    uint32_t hash_val = n_hash_fnv1a(norm, strlen(norm));
    size_t idx = n_grafo_buscar_por_texto((NGrafo*)g, norm, hash_val);
    return (idx != (size_t)-1) ? g->vocab[idx].id : NGF_ID_NULO;
}

/* Búsqueda (S,P) -> primer O por IDs (para n_heredar) */
static uint32_t n_grafo_buscar_objeto_ids(const NGrafo* g, uint32_t s_id, uint32_t p_id) {
    if (!g || s_id == NGF_ID_NULO || p_id == NGF_ID_NULO) return NGF_ID_NULO;
    if (g->sp_index) {
        NGFIndexNode* n = n_index_find(g->sp_index, NGF_KEY_SP(s_id, p_id));
        if (n && n->n > 0) return n->vals_u32[0];
        return NGF_ID_NULO;
    }
    for (size_t i = 0; i < g->triples_count; i++) {
        if (g->triples[i].s == s_id && g->triples[i].p == p_id)
            return g->triples[i].o;
    }
    return NGF_ID_NULO;
}

uint32_t n_buscar_objeto(const NGrafo* g, const char* sujeto, const char* predicado) {
    if (!g) return NGF_ID_NULO;
    uint32_t s = n_grafo_resolver_id(g, sujeto);
    uint32_t p = n_grafo_resolver_id(g, predicado);
    if (s == NGF_ID_NULO || p == NGF_ID_NULO) return NGF_ID_NULO;
    if (g->sp_index) {
        NGFIndexNode* n = n_index_find(g->sp_index, NGF_KEY_SP(s, p));
        if (n && n->n > 0) return n->vals_u32[0];
        return NGF_ID_NULO;
    }
    for (size_t i = 0; i < g->triples_count; i++) {
        if (g->triples[i].s == s && g->triples[i].p == p)
            return g->triples[i].o;
    }
    return NGF_ID_NULO;
}

size_t n_buscar_objetos(const NGrafo* g, const char* sujeto, const char* predicado, uint32_t* ids, size_t max_count) {
    if (!g) return 0;
    uint32_t s = n_grafo_resolver_id(g, sujeto);
    uint32_t p = n_grafo_resolver_id(g, predicado);
    if (s == NGF_ID_NULO || p == NGF_ID_NULO) return 0;
    if (g->sp_index) {
        NGFIndexNode* n = n_index_find(g->sp_index, NGF_KEY_SP(s, p));
        if (!n) return 0;
        size_t copy = n->n < max_count ? n->n : max_count;
        if (ids) for (size_t i = 0; i < copy; i++) ids[i] = n->vals_u32[i];
        return copy;
    }
    size_t n = 0;
    for (size_t i = 0; i < g->triples_count && n < max_count; i++) {
        if (g->triples[i].s == s && g->triples[i].p == p) {
            if (ids) ids[n] = g->triples[i].o;
            n++;
        }
    }
    return n;
}

uint32_t n_buscar_sujeto(const NGrafo* g, const char* predicado, const char* objeto) {
    if (!g) return NGF_ID_NULO;
    uint32_t p = n_grafo_resolver_id(g, predicado);
    uint32_t o = n_grafo_resolver_id(g, objeto);
    if (p == NGF_ID_NULO || o == NGF_ID_NULO) return NGF_ID_NULO;
    if (g->po_index) {
        NGFIndexNode* n = n_index_find(g->po_index, NGF_KEY_PO(p, o));
        if (n && n->n > 0) return n->vals_u32[0];
        return NGF_ID_NULO;
    }
    for (size_t i = 0; i < g->triples_count; i++) {
        if (g->triples[i].p == p && g->triples[i].o == o)
            return g->triples[i].s;
    }
    return NGF_ID_NULO;
}

size_t n_buscar_sujetos(const NGrafo* g, const char* predicado, const char* objeto, uint32_t* ids, size_t max_count) {
    if (!g) return 0;
    uint32_t p = n_grafo_resolver_id(g, predicado);
    uint32_t o = n_grafo_resolver_id(g, objeto);
    if (p == NGF_ID_NULO || o == NGF_ID_NULO) return 0;
    if (g->po_index) {
        NGFIndexNode* n = n_index_find(g->po_index, NGF_KEY_PO(p, o));
        if (!n) return 0;
        size_t copy = n->n < max_count ? n->n : max_count;
        if (ids) for (size_t i = 0; i < copy; i++) ids[i] = n->vals_u32[i];
        return copy;
    }
    size_t n = 0;
    for (size_t i = 0; i < g->triples_count && n < max_count; i++) {
        if (g->triples[i].p == p && g->triples[i].o == o) {
            if (ids) ids[n] = g->triples[i].s;
            n++;
        }
    }
    return n;
}

size_t n_buscar_donde_aparece(const NGrafo* g, const char* objeto, uint32_t* sujetos, uint32_t* predicados, size_t max_count) {
    if (!g) return 0;
    uint32_t o = n_grafo_resolver_id(g, objeto);
    if (o == NGF_ID_NULO) return 0;
    if (g->o_index) {
        NGFIndexNode* n = n_index_find(g->o_index, (uint64_t)o);
        if (!n || !n->vals_u64) return 0;
        size_t copy = n->n < max_count ? n->n : max_count;
        for (size_t i = 0; i < copy; i++) {
            uint64_t ksp = n->vals_u64[i];
            uint32_t s = (uint32_t)(ksp >> 32);
            uint32_t p = (uint32_t)(ksp & 0xFFFFFFFFu);
            if (sujetos) sujetos[i] = s;
            if (predicados) predicados[i] = p;
        }
        return copy;
    }
    size_t cnt = 0;
    for (size_t i = 0; i < g->triples_count && cnt < max_count; i++) {
        if (g->triples[i].o == o) {
            if (sujetos) sujetos[cnt] = g->triples[i].s;
            if (predicados) predicados[cnt] = g->triples[i].p;
            cnt++;
        }
    }
    return cnt;
}

size_t n_buscar_predicados(const NGrafo* g, const char* sujeto, uint32_t* preds, size_t max_count) {
    if (!g) return 0;
    uint32_t s = n_grafo_resolver_id(g, sujeto);
    if (s == NGF_ID_NULO) return 0;
    size_t n = 0;
    for (size_t i = 0; i < g->triples_count && n < max_count; i++) {
        if (g->triples[i].s == s) {
            int duplicado = 0;
            if (preds) {
                for (size_t j = 0; j < n; j++) {
                    if (preds[j] == g->triples[i].p) { duplicado = 1; break; }
                }
            }
            if (!duplicado) {
                if (preds) preds[n] = g->triples[i].p;
                n++;
            }
        }
    }
    return n;
}

int n_olvidar_triple(NGrafo* g, const char* sujeto, const char* predicado, const char* objeto) {
    if (!g) return 0;
    n_grafo_desmapear_triples(g);  /* poder modificar triples si vienen de mmap */
    uint32_t s = n_grafo_resolver_id(g, sujeto);
    uint32_t p = n_grafo_resolver_id(g, predicado);
    uint32_t o = n_grafo_resolver_id(g, objeto);
    if (s == NGF_ID_NULO || p == NGF_ID_NULO || o == NGF_ID_NULO) return 0;
    for (size_t i = 0; i < g->triples_count; i++) {
        if (g->triples[i].s == s && g->triples[i].p == p && g->triples[i].o == o) {
            n_grafo_indice_quitar(g, s, p, o);
            g->triples[i] = g->triples[g->triples_count - 1];
            g->triples_count--;
            g->dirty = 1;
            return 1;
        }
    }
    return 0;
}

/* --- Fase 5: API de alto nivel (comodidades) --- */

int n_recordar_triple_texto(NGrafo* g, const char* s_texto, const char* p_texto, const char* o_texto) {
    return n_recordar(g, s_texto, p_texto, o_texto);
}

int n_buscar_objeto_texto(const NGrafo* g, const char* sujeto, const char* predicado, char* buf, size_t buf_size) {
    if (!g || !buf || buf_size == 0) return 0;
    buf[0] = '\0';
    uint32_t o = n_buscar_objeto(g, sujeto, predicado);
    if (o == NGF_ID_NULO) return 0;
    return n_obtener_texto(g, o, buf, buf_size);
}

/* Fase 9: n_heredar - búsqueda transitiva vía n_es_un */
#define N_HEREDAR_MAX 64
static int n_heredar_ya_visitado(uint32_t* visitado, size_t n, uint32_t id) {
    for (size_t i = 0; i < n; i++) if (visitado[i] == id) return 1;
    return 0;
}

uint32_t n_heredar(const NGrafo* g, const char* sujeto, const char* predicado) {
    if (!g || !sujeto || !predicado) return NGF_ID_NULO;
    uint32_t s_id = n_grafo_resolver_id(g, sujeto);
    uint32_t p_id = n_grafo_resolver_id(g, predicado);
    if (s_id == NGF_ID_NULO || p_id == NGF_ID_NULO) return NGF_ID_NULO;

    uint32_t id_n_es_un = n_grafo_resolver_id(g, "n_es_un");

    uint32_t cola[N_HEREDAR_MAX];
    uint32_t visitado[N_HEREDAR_MAX];
    size_t cola_in = 0, cola_out = 0, n_visitados = 0;

    cola[cola_in++] = s_id;

    while (cola_out < cola_in && cola_in <= N_HEREDAR_MAX) {
        uint32_t actual = cola[cola_out++];

        if (n_heredar_ya_visitado(visitado, n_visitados, actual)) continue;
        if (n_visitados >= N_HEREDAR_MAX) continue;
        visitado[n_visitados++] = actual;

        /* Directo: (actual, P, O) */
        uint32_t o = n_grafo_buscar_objeto_ids(g, actual, p_id);
        if (o != NGF_ID_NULO) return o;

        /* Transitiva: (actual, n_es_un, T) */
        if (id_n_es_un != NGF_ID_NULO) {
            uint32_t tipos[16];
            size_t nt = 0;
            if (g->sp_index) {
                NGFIndexNode* n = n_index_find(g->sp_index, NGF_KEY_SP(actual, id_n_es_un));
                if (n)
                    for (size_t i = 0; i < n->n && nt < 16; i++)
                        tipos[nt++] = n->vals_u32[i];
            } else {
                for (size_t i = 0; i < g->triples_count && nt < 16; i++) {
                    if (g->triples[i].s == actual && g->triples[i].p == id_n_es_un)
                        tipos[nt++] = g->triples[i].o;
                }
            }
            for (size_t i = 0; i < nt && cola_in < N_HEREDAR_MAX; i++) {
                if (!n_heredar_ya_visitado(visitado, n_visitados, tipos[i]))
                    cola[cola_in++] = tipos[i];
            }
        }
    }
    return NGF_ID_NULO;
}

int n_heredar_texto(const NGrafo* g, const char* sujeto, const char* predicado, char* buf, size_t buf_size) {
    if (!g || !buf || buf_size == 0) return 0;
    buf[0] = '\0';
    uint32_t o = n_heredar(g, sujeto, predicado);
    if (o == NGF_ID_NULO) return 0;
    return n_obtener_texto(g, o, buf, buf_size);
}

size_t n_lista_triples(const NGrafo* g, const char* sujeto, uint32_t* predicados, uint32_t* objetos, size_t max_count) {
    if (!g) return 0;
    uint32_t s = n_grafo_resolver_id(g, sujeto);
    if (s == NGF_ID_NULO) return 0;
    size_t n = 0;
    for (size_t i = 0; i < g->triples_count && n < max_count; i++) {
        if (g->triples[i].s == s) {
            if (predicados) predicados[n] = g->triples[i].p;
            if (objetos) objetos[n] = g->triples[i].o;
            n++;
        }
    }
    return n;
}

int n_para_cada_concepto(const NGrafo* g, int (*cb)(uint32_t id, const char* texto, void* user), void* user) {
    if (!g || !cb) return 0;
    for (size_t i = 0; i < g->vocab_count; i++) {
        if (g->vocab[i].text && cb(g->vocab[i].id, g->vocab[i].text, user) != 0)
            return 1;
    }
    return 0;
}

size_t n_tamano_grafo(const NGrafo* g, size_t* conceptos_out) {
    if (!g) return 0;
    if (conceptos_out) *conceptos_out = g->vocab_count;
    return g->triples_count;
}

/* --- Fase 7: Caché y optimización --- */

void n_configurar_cache_lru(NGrafo* g, size_t tamano) {
    if (!g) return;
    free(g->lru_hash);
    free(g->lru_idx);
    g->lru_hash = NULL;
    g->lru_idx = NULL;
    g->lru_n = 0;
    g->lru_size = 0;
    if (tamano > 0) {
        g->lru_hash = (uint32_t*)calloc(tamano, sizeof(uint32_t));
        g->lru_idx = (size_t*)calloc(tamano, sizeof(size_t));
        if (g->lru_hash && g->lru_idx)
            g->lru_size = tamano;
        else {
            free(g->lru_hash);
            free(g->lru_idx);
            g->lru_hash = NULL;
            g->lru_idx = NULL;
        }
    }
}

void n_configurar_bloom(NGrafo* g, size_t num_bits) {
    if (!g) return;
    free(g->bloom_bits);
    g->bloom_bits = NULL;
    g->bloom_bits_len = 0;
    if (num_bits > 0) {
        size_t num_bytes = (num_bits + 7) / 8;
        g->bloom_bits = (uint8_t*)calloc(1, num_bytes);
        if (g->bloom_bits) {
            g->bloom_bits_len = num_bytes;
            n_grafo_bloom_reconstruir(g);
        }
    }
}
