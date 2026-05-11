#ifndef JASBOOT_RT_JMN_VM_H
#define JASBOOT_RT_JMN_VM_H

#include "jasboot_rt.h"

struct JMNMemoria;

/* Memoria neuronal activa (NULL si cerrada o no abierta). */
struct JMNMemoria *jb_jmn_rt_mem(void);

void jb_jmn_vm_startup(void);
void jb_jmn_vm_shutdown(void);

#endif
