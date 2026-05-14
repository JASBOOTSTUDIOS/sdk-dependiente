<p align="center">
  <img src="../assets/jasboot-icon.png" alt="Jasboot — logo del lenguaje" width="120" height="120">
</p>

# jasboot IR (IR binario + VM)

**Representación intermedia binaria (`.jbo`) y máquina virtual** que ejecuta el bytecode generado por el compilador **`jbc`** del monorepo Jasboot.

---

## Objetivo

- Formato IR binario propio (instrucciones de tamaño fijo, registros virtuales).
- Carga y validación del `.jbo` (`reader_ir.c`, `ir_vm.c`, `ir_format.c`).
- **Ejecución** en `vm.c` (integración opcional con JMN cuando se compila con `JASBOOT_LANG_INTEGRATION`).

Flujo real en el proyecto:

```
.jasb (Jasboot)  →  jbc (jas-compiler-c)  →  .jbo  →  jasboot-ir-vm (este paquete)
```

El compilador Jasboot **no** vive en este directorio: está en `sdk-dependiente/jas-compiler-c/`.

---

## Arquitectura (fuentes principales)

| Área | Ficheros típicos |
|------|------------------|
| Formato IR | `src/ir_format.c`, `src/ir_format.h` |
| Lectura / validación | `src/reader_ir.c`, `src/ir_vm.c` |
| VM | `src/vm.c`, `src/vm.h`, `src/vm_main.c` |
| Optimizador (piezas IR) | `src/optimizer_ir.c` |
| MAI / analítica (VM) | `src/mai.c`, `src/vm_analitica_mlp.c` |
| JMN (enlazado al compilar la VM) | `../jasboot-jmn-core/src/memoria_neuronal/*` |

En el árbol también existen **herramientas y tests en C** (`ir_compiler.c`, `codegen_ir.c`, `ir_validator.c`, `ir_test.c`, …) que el **`Makefile`** puede enlazar como objetivos adicionales; el camino **Windows habitual del monorepo** usa `build_vm.bat` y solo construye la VM (ver siguiente sección).

---

## Documentación en este paquete

- [`docs/FORMATO_IR.md`](docs/FORMATO_IR.md) — estructura del IR binario  
- [`docs/OPCODES.md`](docs/OPCODES.md) — opcodes  
- [`docs/BACKEND_DIRECTO.md`](docs/BACKEND_DIRECTO.md), [`docs/JASB_SEC.md`](docs/JASB_SEC.md), [`docs/METADATA_IA.md`](docs/METADATA_IA.md) — temas relacionados  

Comportamiento de **JMN** desde el punto de vista del usuario del lenguaje: [`../docs/JMN_Y_MEMORIA_EN_JASBOOT.md`](../docs/JMN_Y_MEMORIA_EN_JASBOOT.md).

---

## Compilar la VM (Windows, flujo recomendado)

Desde esta carpeta:

```bat
build_vm.bat
```

Resultado típico en `bin/`:

- **`jasboot-ir-vm-trace.exe`** — VM enlazada con trazas / depuración según flags de compilación del script.  
- **`jasboot-ir-vm.exe`** — copia de la anterior (ver comentarios en `build_vm.bat`; ambos nombres suelen existir tras un build correcto).

El script enlaza **todas** las unidades de compilación de `jasboot-jmn-core/src/memoria_neuronal/*.c` y `platform_compat.c`. Si no encuentra JMN, define **`JASBOOT_JMN_ROOT`** apuntando a la raíz de `jasboot-jmn-core`, o coloca `sdk-dependiente/jasboot-jmn-core` como carpeta hermana de `jasboot-ir`.

---

## Compilar con `Makefile` (alternativa)

El `Makefile` puede generar, entre otros, `bin/jasboot-ir-vm.exe` y **`bin/jasboot-ir-compiler.exe`** (IR desde AST interno del paquete). Ese **compilador IR** no sustituye a **`jbc`** para programas `.jasb` del lenguaje Jasboot; el flujo oficial del lenguaje sigue siendo **`jbc` → `.jbo` → `jasboot-ir-vm`**.

---

## Ejecutar un programa

Tras compilar un `.jasb` con `jbc`:

```bat
bin\jasboot-ir-vm.exe ruta\programa.jbo
```

En el monorepo Jasboot suele usarse el script de la raíz: `node .vscode/run-jasb.cjs ruta\al\archivo.jasb` (compila y ejecuta con los `.exe` del SDK).

---

## Estado resumido

| Componente | Notas |
|-------------|--------|
| Formato `.jbo` + lector | En uso |
| VM `jasboot-ir-vm` | En uso (build principal vía `build_vm.bat`) |
| JMN en VM | En uso si el núcleo JMN está presente en el build |
| `jasboot-ir-compiler` / validador / `ir_test` | Código presente; construcción **opcional** vía `Makefile` o invocación manual, no parte de `build_vm.bat` |

---

## Relación con otros directorios del SDK

- **Compilador Jasboot (`jbc`):** `sdk-dependiente/jas-compiler-c/`.  
- **JMN:** `sdk-dependiente/jasboot-jmn-core/`.  

### Clonar solo este repo

1. Clona también **`jasboot-jmn-core`**.  
2. Define **`JASBOOT_JMN_ROOT`** con la ruta absoluta al directorio raíz de ese clon (el que contiene `src/`).  
3. Ejecuta **`build_vm.bat`** (Windows) o **`make`** con `JMN_PKG` apuntando al clon.  

En el monorepo, `jasboot-jmn-core` es hermano de `jasboot-ir` bajo `sdk-dependiente/` y el build lo detecta sin variables.

---

**Última actualización:** 2026-05-14
