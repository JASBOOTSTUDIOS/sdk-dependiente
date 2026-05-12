#include "mai.h"
#include "../../jasboot-jmn-core/src/memoria_neuronal/memoria_neuronal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAI_HASH_SIZE 1000003u
#define MAI_DEFAULT_DECAY 0.05f
#define MAI_DEFAULT_THRESHOLD 0.3f
static uint32_t mai_hash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x);
    return x % MAI_HASH_SIZE;
}

static size_t mai_estimated_bytes(const MAISystem* mai) {
    if (!mai) return 0;
    return (size_t)mai->capacity * sizeof(MAIActiveNeuron)
         + (size_t)mai->hash_size * sizeof(uint32_t)
         + (size_t)MAI_Q_CAP * sizeof(MAIMessage) * 2u;
}

static int mai_priq_push(MAIPriorityQueue* q, const MAIMessage* m) {
    if (q->count >= MAI_Q_CAP) {
        if (m->priority <= q->items[q->count - 1].priority)
            return -1;
        q->count--;
    }
    uint32_t i = 0;
    while (i < q->count && q->items[i].priority > m->priority)
        i++;
    memmove(&q->items[i + 1], &q->items[i], (size_t)(q->count - i) * sizeof(MAIMessage));
    q->items[i] = *m;
    q->count++;
    return 0;
}

static MAIMessage mai_priq_pop(MAIPriorityQueue* q) {
    MAIMessage m = q->items[0];
    if (q->count > 0) {
        memmove(q->items, q->items + 1, (size_t)(q->count - 1) * sizeof(MAIMessage));
        q->count--;
    }
    return m;
}

static void mai_touch(MAISystem* mai, MAIActiveNeuron* n) {
    n->access_tick = ++mai->global_tick;
    uint32_t ci = (uint32_t)(n->context_idx % MAI_CONTEXT_BUFFER_SIZE);
    n->context[ci] = n->potential;
    n->context_idx = (uint8_t)((n->context_idx + 1u) % MAI_CONTEXT_BUFFER_SIZE);
}

static void mai_append_lra_record(MAISystem* mai, const MAIActiveNeuron* n) {
    FILE* fp = (FILE*)mai->lra_fp;
    if (!fp && mai->lra_path[0]) {
        fp = fopen(mai->lra_path, "ab");
        mai->lra_fp = fp;
    }
    if (!fp) return;
    uint32_t magic = 0x4D414952u;
    fwrite(&magic, 4, 1, fp);
    fwrite(&n->id_hash, 4, 1, fp);
    fwrite(&n->energy, 4, 1, fp);
    fwrite(&n->potential, 4, 1, fp);
    fwrite(&n->access_tick, 8, 1, fp);
    fflush(fp);
}

static void mai_lra_soft_batch(MAISystem* mai, int batch) {
    if (!mai || mai->count == 0) return;
    int ev = 0;
    for (int b = 0; b < batch && ev < 48; b++) {
        uint32_t idx = (uint32_t)((mai->global_tick + (uint64_t)b * 7919u) % mai->count);
        MAIActiveNeuron* n = &mai->neurons[idx];
        if (n->id_hash == 0) continue;
        if (n->energy > 0.08f) continue;
        mai_append_lra_record(mai, n);
        memset(n->context, 0, sizeof(n->context));
        n->potential *= 0.5f;
        n->energy = 0.f;
        ev++;
    }
}

static void mai_maybe_lra(MAISystem* mai) {
    if (!mai) return;
    int force = getenv("MAI_LRA_FORCE_SOFT") != NULL;
    if (!force && mai_estimated_bytes(mai) <= mai->lra_limit_bytes)
        return;
    mai_lra_soft_batch(mai, 64);
}

MAIActiveNeuron* mai_get_or_create_neuron(MAISystem* mai, uint32_t id_hash) {
    if (!mai || id_hash == 0) return NULL;
    uint32_t h = mai_hash(id_hash);

    pthread_mutex_lock(&mai->mutex);

    uint32_t idx = h;
    uint32_t first_empty = 0xFFFFFFFFu;
    for (;;) {
        uint32_t cell = mai->hash_table[idx];
        if (cell == 0xFFFFFFFFu) {
            if (first_empty == 0xFFFFFFFFu) first_empty = idx;
            break;
        }
        uint32_t n_idx = cell;
        if (mai->neurons[n_idx].id_hash == id_hash) {
            mai_touch(mai, &mai->neurons[n_idx]);
            MAIActiveNeuron* out = &mai->neurons[n_idx];
            pthread_mutex_unlock(&mai->mutex);
            return out;
        }
        idx = (idx + 1u) % MAI_HASH_SIZE;
        if (idx == h) {
            pthread_mutex_unlock(&mai->mutex);
            return NULL;
        }
    }

    if (mai->count >= mai->capacity) {
        pthread_mutex_unlock(&mai->mutex);
        return NULL;
    }

    uint32_t n_idx = mai->count++;
    MAIActiveNeuron* n = &mai->neurons[n_idx];
    memset(n, 0, sizeof(*n));
    n->id_hash = id_hash;
    n->decay_rate = MAI_DEFAULT_DECAY;
    n->threshold = MAI_DEFAULT_THRESHOLD;
    n->in_refractory = 0;
    mai_touch(mai, n);

    uint32_t ins = first_empty;
    if (ins == 0xFFFFFFFFu) {
        ins = h;
        uint32_t guard = 0;
        while (mai->hash_table[ins] != 0xFFFFFFFFu && guard < mai->hash_size) {
            ins = (ins + 1u) % MAI_HASH_SIZE;
            guard++;
        }
        if (guard >= mai->hash_size) {
            mai->count--;
            pthread_mutex_unlock(&mai->mutex);
            return NULL;
        }
    }
    mai->hash_table[ins] = n_idx;

    pthread_mutex_unlock(&mai->mutex);
    return n;
}

static void mai_activate_locked(MAISystem* mai, MAIActiveNeuron* n, float value) {
    if (!n->in_refractory) {
        n->potential += value;
        if (n->potential > n->threshold) {
            n->energy = 1.0f;
            n->potential = 0.0f;
            n->in_refractory = 1;
        }
    }
    mai_touch(mai, n);
}

static void mai_activate_neuron(MAISystem* mai, uint32_t target_id, float value) {
    MAIActiveNeuron* n = mai_get_or_create_neuron(mai, target_id);
    if (!n) return;
    pthread_mutex_lock(&mai->mutex);
    mai_activate_locked(mai, n, value);
    pthread_mutex_unlock(&mai->mutex);
}

static void mai_process_sub_message(MAISystem* mai, const MAIMessage* msg) {
    switch (msg->type) {
    case MAI_MSG_REFUERSO:
        mai_activate_neuron(mai, msg->target_id, msg->value * 0.35f);
        break;
    case MAI_MSG_SUEÑO: {
        pthread_mutex_lock(&mai->mutex);
        for (uint32_t i = 0; i < mai->count; i++) {
            mai->neurons[i].potential *= 0.997f;
            mai->neurons[i].energy *= 0.995f;
        }
        pthread_mutex_unlock(&mai->mutex);
        break;
    }
    default:
        break;
    }
}

static void* mai_worker_main(void* arg) {
    MaiWorkerArg* wa = (MaiWorkerArg*)arg;
    MAISystem* mai = wa->mai;
    int is_cortex = wa->is_cortex;

    while (mai->running) {
        pthread_mutex_lock(&mai->mutex);
        MAIPriorityQueue* q = is_cortex ? &mai->cortex : &mai->sub;
        pthread_cond_t* cond = is_cortex ? &mai->cond_cortex : &mai->cond_sub;

        while (q->count == 0 && mai->running)
            pthread_cond_wait(cond, &mai->mutex);

        if (!mai->running) {
            pthread_mutex_unlock(&mai->mutex);
            break;
        }

        MAIMessage msg = mai_priq_pop(q);
        pthread_mutex_unlock(&mai->mutex);

        if (is_cortex) {
            switch (msg.type) {
            case MAI_MSG_ACTIVATION:
                mai_activate_neuron(mai, msg.target_id, msg.value);
                break;
            case MAI_MSG_INHIBITION:
                mai_activate_neuron(mai, msg.target_id, -msg.value);
                break;
            default:
                break;
            }
        } else {
            mai_process_sub_message(mai, &msg);
        }
    }
    return NULL;
}

MAISystem* mai_init(uint32_t capacity, void* jmn_base) {
    MAISystem* mai = (MAISystem*)calloc(1, sizeof(MAISystem));
    if (!mai) return NULL;

    mai->capacity = capacity ? capacity : 1024u;
    mai->neurons = (MAIActiveNeuron*)calloc(mai->capacity, sizeof(MAIActiveNeuron));
    mai->jmn_base = jmn_base;
    mai->hash_size = MAI_HASH_SIZE;
    mai->hash_table = (uint32_t*)malloc(mai->hash_size * sizeof(uint32_t));
    if (!mai->hash_table || !mai->neurons) {
        free(mai->neurons);
        free(mai->hash_table);
        free(mai);
        return NULL;
    }
    for (uint32_t i = 0; i < mai->hash_size; i++)
        mai->hash_table[i] = 0xFFFFFFFFu;

    const char* lim = getenv("MAI_LRA_LIMIT_MB");
    if (lim && lim[0])
        mai->lra_limit_bytes = (size_t)atoi(lim) * 1024u * 1024u;
    else
        mai->lra_limit_bytes = 2048ull * 1024ull * 1024ull;

#if defined(_WIN32) || defined(_WIN64)
    {
        char tmp[MAX_PATH];
        DWORD n = GetTempPathA(sizeof(tmp), tmp);
        if (n == 0 || n >= sizeof(tmp))
            snprintf(mai->lra_path, sizeof(mai->lra_path), "mai_lra_%p.bin", (void*)mai);
        else
            snprintf(mai->lra_path, sizeof(mai->lra_path), "%sjmn_mai_lra_%p.bin", tmp, (void*)mai);
    }
#else
    snprintf(mai->lra_path, sizeof(mai->lra_path), "/tmp/jmn_mai_lra_%p.bin", (void*)mai);
#endif

    pthread_mutex_init(&mai->mutex, NULL);
    pthread_cond_init(&mai->cond_cortex, NULL);
    pthread_cond_init(&mai->cond_sub, NULL);

    mai->running = 1;
    for (int i = 0; i < MAI_NUM_WORKERS; i++) {
        mai->worker_ctx[i].mai = mai;
        mai->worker_ctx[i].is_cortex = (i == 0);
        pthread_create(&mai->workers[i], NULL, mai_worker_main, &mai->worker_ctx[i]);
    }

    printf("[MAI] Córtex+Subconsciente (%d workers), colas prioridad %u, LRA límite ~%zu MiB.\n",
           MAI_NUM_WORKERS, (unsigned)MAI_Q_CAP,
           (size_t)(mai->lra_limit_bytes / (1024u * 1024u)));
    return mai;
}

void mai_set_jmn_base(MAISystem* mai, void* jmn) {
    if (!mai) return;
    pthread_mutex_lock(&mai->mutex);
    mai->jmn_base = jmn;
    pthread_mutex_unlock(&mai->mutex);
}

void mai_destroy(MAISystem* mai) {
    if (!mai) return;
    mai->running = 0;
    pthread_cond_broadcast(&mai->cond_cortex);
    pthread_cond_broadcast(&mai->cond_sub);
    for (int i = 0; i < MAI_NUM_WORKERS; i++)
        pthread_join(mai->workers[i], NULL);

    if (mai->lra_fp) {
        fclose((FILE*)mai->lra_fp);
        mai->lra_fp = NULL;
    }
    free(mai->neurons);
    free(mai->hash_table);
    pthread_mutex_destroy(&mai->mutex);
    pthread_cond_destroy(&mai->cond_cortex);
    pthread_cond_destroy(&mai->cond_sub);
    free(mai);
}

static int mai_reflect_env_on(void) {
    const char* e = getenv("MAI_REFLECT");
    return e && e[0] && strcmp(e, "0") != 0;
}

static void mai_reflect_from_jmn(MAISystem* mai) {
    static uint32_t throttle;
    if (++throttle % 3u != 0u) return;

    JMNBusquedaResultado buf[32];
    uint32_t seeds[12];
    int ns = 0;
    void* jmn_ptr = NULL;

    pthread_mutex_lock(&mai->mutex);
    jmn_ptr = mai->jmn_base;
    if (jmn_ptr && mai->count > 0) {
        uint32_t base = (uint32_t)(mai->global_tick % 7919u + 1u);
        for (uint32_t t = 0; t < mai->count && ns < 12; t++) {
            uint32_t k = (base + t * 1103515245u) % mai->count;
            MAIActiveNeuron* n = &mai->neurons[k];
            if (n->id_hash != 0 && n->energy >= 0.28f)
                seeds[ns++] = n->id_hash;
        }
    }
    pthread_mutex_unlock(&mai->mutex);

    if (!jmn_ptr || ns == 0) return;

    JMNMemoria* mem = (JMNMemoria*)jmn_ptr;
    int budget = 28;
    for (int s = 0; s < ns && budget > 0; s++) {
        int nr = jmn_buscar_asociaciones(mem, seeds[s], 0u, 0.07f, 1u, buf, 32);
        for (int i = 0; i < nr && budget > 0; i++) {
            if (buf[i].id == 0 || buf[i].id == seeds[s]) continue;
            float amp = buf[i].fuerza * 0.055f;
            if (amp < 0.015f) amp = 0.015f;
            mai_send_message_ex(mai, seeds[s], buf[i].id, amp, MAI_MSG_ACTIVATION, 58);
            budget--;
        }
    }
}

void mai_process_cycle(MAISystem* mai, uint32_t delta_ms) {
    if (!mai) return;
    uint32_t d = delta_ms ? delta_ms : 1u;

    pthread_mutex_lock(&mai->mutex);
    mai->global_tick += d;

    if (mai->count > 0) {
        static uint32_t last_idx = 0;
        uint32_t batch = 1000u;
        if (batch > mai->count) batch = mai->count;

        for (uint32_t i = 0; i < batch; i++) {
            uint32_t idx = (last_idx + i) % mai->count;
            MAIActiveNeuron* n = &mai->neurons[idx];
            if (n->energy > 0.0f) {
                n->energy -= n->decay_rate * (d / 100.0f);
                if (n->energy < 0.0f) n->energy = 0.0f;
            }
            if (n->in_refractory) {
                if (n->last_update > 200) {
                    n->in_refractory = 0;
                    n->last_update = 0;
                } else {
                    n->last_update += d;
                }
            }
        }
        last_idx = (last_idx + batch) % mai->count;
    }

    mai_maybe_lra(mai);
    pthread_mutex_unlock(&mai->mutex);

    if (mai_reflect_env_on())
        mai_reflect_from_jmn(mai);
}

void mai_scheduler_tick(MAISystem* mai) {
    if (!mai) return;
    static uint32_t tick;
    tick++;
    if ((tick % 250u) != 0u) return;
    mai_send_message_ex(mai, 0, 0, 0.02f, MAI_MSG_SUEÑO, 4);
}

void mai_send_message_ex(MAISystem* mai, uint32_t origin, uint32_t target, float value,
                         MAIMessageType type, uint8_t priority) {
    if (!mai) return;

    MAIMessage m;
    memset(&m, 0, sizeof(m));
    m.origin_id = origin;
    m.target_id = target;
    m.value = value;
    m.type = type;
    m.priority = priority;

    int route_sub = (type == MAI_MSG_REFUERSO || type == MAI_MSG_SUEÑO);
    MAIPriorityQueue* q = route_sub ? &mai->sub : &mai->cortex;
    pthread_cond_t* cond = route_sub ? &mai->cond_sub : &mai->cond_cortex;

    pthread_mutex_lock(&mai->mutex);
    mai_priq_push(q, &m);
    pthread_cond_signal(cond);
    pthread_mutex_unlock(&mai->mutex);
}

void mai_send_message(MAISystem* mai, uint32_t origin, uint32_t target, float value, MAIMessageType type) {
    uint8_t pri = 96;
    if (type == MAI_MSG_ACTIVATION) pri = 128;
    mai_send_message_ex(mai, origin, target, value, type, pri);
}
