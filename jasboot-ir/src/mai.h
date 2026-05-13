#ifndef MAI_CORE_H
#define MAI_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

/**
 * MAI (Memoria Activa Independiente)
 * Fase plan: Córtex / Subconsciente, cola por prioridad, LRA configurable.
 */

#define MAI_NUM_WORKERS 2
#define MAI_MAX_ACTIVE_NEURONS 1000000
#define MAI_CONTEXT_BUFFER_SIZE 10
#define MAI_Q_CAP 8192

typedef struct MAISystem MAISystem;

typedef enum {
    MAI_MSG_ACTIVATION,
    MAI_MSG_INHIBITION,
    MAI_MSG_REFUERSO,
    MAI_MSG_SUEÑO
} MAIMessageType;

typedef struct {
    uint32_t origin_id;
    uint32_t target_id;
    float value;
    MAIMessageType type;
    uint8_t priority;
    uint8_t _pad[3];
} MAIMessage;

typedef struct {
    MAIMessage items[MAI_Q_CAP];
    uint32_t count;
} MAIPriorityQueue;

typedef struct {
    uint32_t id_hash;
    float energy;
    float potential;
    uint32_t last_update;
    uint8_t in_refractory;
    uint32_t refractory_end;
    float context[MAI_CONTEXT_BUFFER_SIZE];
    uint8_t context_idx;
    float decay_rate;
    float threshold;
    uint64_t access_tick;
} MAIActiveNeuron;

typedef struct {
    MAISystem* mai;
    int is_cortex;
} MaiWorkerArg;

struct MAISystem {
    MAIActiveNeuron* neurons;
    uint32_t count;
    uint32_t capacity;

    uint32_t* hash_table;
    uint32_t hash_size;

    MAIPriorityQueue cortex;
    MAIPriorityQueue sub;

    pthread_mutex_t mutex;
    pthread_cond_t cond_cortex;
    pthread_cond_t cond_sub;
    pthread_t workers[MAI_NUM_WORKERS];
    MaiWorkerArg worker_ctx[MAI_NUM_WORKERS];
    int running;

    void* jmn_base;

    size_t lra_limit_bytes;
    uint64_t global_tick;
    char lra_path[520];
    void* lra_fp;

    /** Inc under mutex cuando `mai_priq_push` rechaza un mensaje (cola llena y prioridad baja). */
    uint64_t enqueue_failures;
};

MAISystem* mai_init(uint32_t capacity, void* jmn_base);
void mai_destroy(MAISystem* mai);
void mai_set_jmn_base(MAISystem* mai, void* jmn);

/** 0 = encolado; -1 = cola llena (véase `enqueue_failures`). */
int mai_send_message(MAISystem* mai, uint32_t origin, uint32_t target, float value, MAIMessageType type);
int mai_send_message_ex(MAISystem* mai, uint32_t origin, uint32_t target, float value,
                        MAIMessageType type, uint8_t priority);

uint64_t mai_enqueue_failures(MAISystem* mai);

void mai_process_cycle(MAISystem* mai, uint32_t delta_ms);
void mai_scheduler_tick(MAISystem* mai);
MAIActiveNeuron* mai_get_or_create_neuron(MAISystem* mai, uint32_t id_hash);

#endif
