#include "platform_compat.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

void jasboot_init_console(void) {
#if defined(_WIN32) || defined(_WIN64)
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}
