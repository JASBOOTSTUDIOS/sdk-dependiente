#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/** Inicializa consola (modo UTF-8 en Windows, etc.) */
void jasboot_init_console(void);

#ifdef __cplusplus
}
#endif

#endif
