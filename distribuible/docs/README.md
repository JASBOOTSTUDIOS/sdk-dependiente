# Documentación del paquete distribuible

Esta carpeta acompaña al **layout de instalación** descrito en [`SDK_README.md`](SDK_README.md) (copia a `%ProgramFiles%\Jasboot\`, variables de entorno, etc.).

## Enlace con el monorepo Jasboot

- **SDK de desarrollo y fuentes:** directorio hermano [`../../README.md`](../../README.md) (raíz de `sdk-dependiente/` dentro del repo).
- **Compilador `jbc`:** [`../../jas-compiler-c/README.md`](../../jas-compiler-c/README.md).
- **VM e IR:** [`../../jasboot-ir/README.md`](../../jasboot-ir/README.md).
- **JMN:** [`../../jasboot-jmn-core/README.md`](../../jasboot-jmn-core/README.md).
- **JMN / memoria (notas verificadas):** [`../../docs/JMN_Y_MEMORIA_EN_JASBOOT.md`](../../docs/JMN_Y_MEMORIA_EN_JASBOOT.md).

Antes de publicar o instalar desde aquí, construye los binarios en `sdk-dependiente` y cópialos a `distribuible/bin/` y `distribuible/runtime/` según esperan `instalar_jasboot.bat` y `verify_installation.bat`.
