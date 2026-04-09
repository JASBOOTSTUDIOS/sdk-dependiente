<p align="center">
  <img src="../assets/jasboot-icon.png" alt="Jasboot — logo del lenguaje" width="120" height="120">
</p>

# jasboot-jmn-core

Biblioteca C **JMN** (memoria neuronal) y utilidades mínimas de consola usadas por la VM **jasboot-ir**.

Forma parte del **SDK estable** del lenguaje bajo `sdk-dependiente/`.

## Contenido

- `src/memoria_neuronal/` — implementación JMN (`memoria_neuronal.h` y fuentes).
- `src/platform_compat.c` / `platform_compat.h` — p. ej. UTF-8 en consola Windows (`jasboot_init_console`).

## Uso en el monorepo Jasboot

La VM (`sdk-dependiente/jasboot-ir`) resuelve este paquete así:

1. Variable de entorno **`JASBOOT_JMN_ROOT`**: ruta absoluta al **directorio raíz de este paquete** (el que contiene `src/`).
2. Si no está definida: **`sdk-dependiente/jasboot-jmn-core`** (hermano de `jasboot-ir` en el mismo `sdk-dependiente`).

## Repo independiente en GitHub

Puedes publicar solo esta carpeta como repositorio propio; al clonar junto a `jasboot-ir`, define:

```text
JASBOOT_JMN_ROOT=C:\ruta\a\jasboot-jmn-core
```

En Linux/macOS:

```bash
export JASBOOT_JMN_ROOT=/ruta/a/jasboot-jmn-core
```

Luego ejecuta `build_vm.bat` (Windows) o `make` en `jasboot-ir` con `JMN_PKG` apuntando al clon.

## Licencia

La del proyecto Jasboot padre (añade aquí `LICENSE` si separas el repo).
