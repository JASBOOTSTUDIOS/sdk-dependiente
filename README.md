# sdk-dependiente

Toolchain oficial del lenguaje **Jasboot** en **C**: compilador, representación intermedia (IR) binaria, máquina virtual y núcleo de memoria neuronal (JMN) que enlaza con la VM.

Este repositorio contiene el **SDK completo en un solo árbol** (sin submódulos Git). Cualquier programador puede clonarlo, compilar y obtener `jbc` y `jasboot-ir-vm` para trabajar con archivos fuente `.jasb`.

**Licencia:** el proyecto es **código abierto** bajo la [Licencia MIT](LICENSE) (permisiva: uso, modificación y distribución con mínimas condiciones).

---

## Cómo se enlaza todo hasta ejecutar un `.jasb`

El flujo es **dos pasos** en la práctica (tres si separas compilación y ejecución):

```
┌─────────────────┐     jbc (compilador)      ┌─────────────────┐     jasboot-ir-vm      ┌──────────────┐
│  programa.jasb  │  ───────────────────────►  │   programa.jbo   │  ──────────────────►  │  ejecución   │
│  (texto Jasboot)│     lexer/parser/codegen   │  (IR binario)    │     lee IR y opcodes  │  (consola…)  │
└─────────────────┘                            └─────────────────┘                       └──────────────┘
        │                                                │
        │                                                │
   jas-compiler-c                                   jasboot-ir
   (genera .jbo)                                    (vm.c, reader_ir.c, …)
```

1. **Entrada:** un archivo **`.jasb`** con el programa en lenguaje Jasboot (texto).
2. **Compilación:** el ejecutable **`jbc`** (carpeta `jas-compiler-c/`) lee el `.jasb`, analiza el código y emite un **`.jbo`**: IR binario con opcodes de tamaño fijo que la VM entiende.
3. **Ejecución:** el ejecutable **`jasboot-ir-vm`** (carpeta `jasboot-ir/`) carga el `.jbo`, interpreta las instrucciones y produce el comportamiento del programa (por ejemplo salida por consola, lectura de stdin, etc.).

**Importante:** `jbc` y la VM son programas **independientes** enlazados solo por el **formato `.jbo`**. No hace falta que estén en la misma carpeta si ambos están en el `PATH` o invocas rutas completas.

**Dependencia de build de la VM:** al **compilar** `jasboot-ir-vm`, el enlazador incluye el código de **`jasboot-jmn-core/`** (JMN y `platform_compat`). Eso no afecta al flujo “usuario final”: una vez construidos los `.exe`, ejecutar un `.jasb` compilado es solo `jbc` → `.jbo` → VM.

---

## Contenido del repositorio

| Directorio | Rol | Relación con el flujo `.jasb` |
|------------|-----|-------------------------------|
| [`jas-compiler-c/`](jas-compiler-c/) | Compilador **`jbc`**. | **Obligatorio** para pasar de `.jasb` a `.jbo`. Ver [README del compilador](jas-compiler-c/README.md). |
| [`jasboot-ir/`](jasboot-ir/) | Formato IR, lector y **VM** (`jasboot-ir-vm`). | **Obligatorio** para ejecutar `.jbo`. Script interno `build_vm.bat`. Ver [README de la VM](jasboot-ir/README.md). |
| [`jasboot-jmn-core/`](jasboot-jmn-core/) | Implementación JMN y compatibilidad de plataforma. | **Solo build de la VM** (fuentes que se compilan junto a `vm.c`). |
| [`jas-runtime/`](jas-runtime/) | Runtime auxiliar en C. | Piezas de soporte; el camino principal `.jasb` → `.jbo` → VM no lo requiere en el uso típico. |
| [`scripts/`](scripts/) | Scripts de construcción en Windows. | Orquestan compilación de VM + compilador y copian binarios a `bin/`. |
| [`bin/`](bin/) | Accesos directos (`jbc.cmd`, `jasboot-ir-vm.cmd`). | Apuntan a `jas-compiler-c\bin\jbc.exe` y `jasboot-ir\bin\jasboot-ir-vm.exe` (o copias en `bin/` tras `build-all`). |

Documentación técnica del IR dentro del SDK: [`jasboot-ir/docs/FORMATO_IR.md`](jasboot-ir/docs/FORMATO_IR.md), [`jasboot-ir/docs/OPCODES.md`](jasboot-ir/docs/OPCODES.md).

---

## Requisitos

- **Compilador C** con soporte **C11** (por ejemplo **gcc** en MinGW-w64, MSYS2 o entorno similar).
- **Windows** para los scripts `.bat` de `scripts/` (la VM y el compilador también se pueden construir con otros métodos descritos en cada subcarpeta, por ejemplo `Makefile` en `jasboot-ir/`).

---

## Primera compilación del SDK (Windows)

Desde la **raíz de este repositorio** (donde están `jas-compiler-c/`, `jasboot-ir/`, `scripts/`, etc.):

```bat
scripts\build-all.bat
```

Ese script, en orden:

1. Compila la **VM** (`jasboot-ir\build_vm.bat`), que localiza **`jasboot-jmn-core`** como carpeta hermana bajo el mismo SDK.
2. Copia **`jasboot-ir-vm.exe`** a **`bin/`** del SDK (si el paso anterior lo generó).
3. Compila **`jbc`** (`jas-compiler-c\build.bat`) y deja **`jbc.exe`** en `jas-compiler-c\bin\` y una copia en **`bin/`** del SDK.

Alternativas:

| Script | Cuándo usarlo |
|--------|----------------|
| `scripts\build-vm.bat` | Solo reconstruir la VM. |
| `scripts\build-compiler.bat` | Solo reconstruir `jbc`. |
| `scripts\clean-build.bat` | Borrar artefactos generados (objetos, `.exe` en rutas de salida; no elimina los `.cmd` versionados en `bin/`). |

---

## Ejecutar un programa `.jasb` después del build

**Opción A — Rutas explícitas (clara para depurar):**

```bat
jas-compiler-c\bin\jbc.exe mi_programa.jasb -o mi_programa.jbo
jasboot-ir\bin\jasboot-ir-vm.exe --continuo mi_programa.jbo
```

**Opción B — Usar la carpeta `bin/` del SDK (recomendada si ejecutaste `build-all`):**

Añade al `PATH` la raíz del SDK y `jas-compiler-c\bin`, o ejecuta desde la raíz:

```bat
set "PATH=%CD%\bin;%CD%\jas-compiler-c\bin;%PATH%"
bin\jbc.cmd mi_programa.jasb -o mi_programa.jbo
bin\jasboot-ir-vm.cmd --continuo mi_programa.jbo
```

**Opción C — Compilar y ejecutar en un paso** (si `jbc` encuentra la VM en el `PATH` o en las rutas que implementa `main.c`):

```bat
jas-compiler-c\bin\jbc.exe mi_programa.jasb -e
```

Los **`.exe`** generados **no** se suben a Git (`.gitignore`); los **`.cmd`** de `bin/` sí están pensados para versionarse y delegar en los ejecutables una vez construidos.

---

## Compilación manual (sin `scripts/`)

- **VM:** `jasboot-ir\build_vm.bat` (Windows) o flujo `Makefile` dentro de `jasboot-ir/` según tu entorno.
- **Compilador:** `jas-compiler-c\build.bat` desde esa carpeta.

Si solo clonas el SDK, no hace falta editar rutas del monorepo Jasboot: `build.bat` del compilador ya copia a `..\bin` del SDK y, si usas `scripts\build-compiler.bat`, se define `JASBOOT_SDK_ROOT` para copiar también a `bin\` del SDK.

---

## Integración con el monorepo Jasboot (opcional)

Si este SDK vive dentro de un repositorio mayor (por ejemplo `jasboot/sdk-dependiente/`) y quieres un **`jbc.exe` “launcher”** en la carpeta `bin/` del monorepo (que reenvía al `jbc` real dentro del SDK), define antes de compilar:

```bat
set "JASBOOT_MONOREPO_BIN=C:\ruta\al\monorepo\bin\"
cd sdk-dependiente\jas-compiler-c
build.bat
```

La barra final en `JASBOOT_MONOREPO_BIN` es importante. El launcher (`jbc_launcher.c`) resuelve el compilador real probando rutas relativas típicas del SDK plano y del monorepo.

Si las fuentes JMN no están en la ubicación por defecto, puedes definir **`JASBOOT_JMN_ROOT`** apuntando a la carpeta que contiene `src/memoria_neuronal/` (véase `jasboot-ir\build_vm.bat`).

---

## Cómo contribuir

### Flujo con ramas (recomendado)

No integres cambios directamente en **`main`** desde tu clon sin acuerdo del equipo. El flujo habitual es:

1. Actualiza `main`: `git checkout main` y `git pull origin main`.
2. Crea una rama descriptiva: `git checkout -b tipo/descripcion-corta` (ejemplos: `fix/vm-stdin`, `docs/readme-ejemplos`, `feat/listas-codegen`).
3. Haz commits con mensajes claros en español.
4. Sube la rama al remoto: `git push -u origin tipo/descripcion-corta`.
5. En GitHub, abre un **pull request** desde tu rama hacia **`main`** en [JASBOOTSTUDIOS/sdk-dependiente](https://github.com/JASBOOTSTUDIOS/sdk-dependiente) y espera revisión o fusión.

Así `main` permanece estable y cada cambio queda trazado en una rama y en el PR.

### Dónde tocar el código

1. **Compilador (sintaxis, tipos, codegen, mensajes de error):** `jas-compiler-c/src/` — tras cambios, ejecutar `scripts\build-compiler.bat` y probar con `.jasb` de ejemplo en `jas-compiler-c/tests/` o en tu propio archivo.
2. **VM (opcodes, I/O, comportamiento en ejecución):** `jasboot-ir/src/` — principalmente `vm.c`, `reader_ir.c`, `ir_format.h`; reconstruir con `scripts\build-vm.bat` o `build-all.bat`.
3. **JMN enlazado a la VM:** `jasboot-jmn-core/src/memoria_neuronal/` — cambios suelen exigir recompilar la VM.
4. **Contrato `.jasb` ↔ `.jbo`:** cualquier nuevo opcode o cambio de formato debe reflejarse en **compilador y VM** de forma coherente; documentar en `jasboot-ir/docs/` cuando afecte al IR.

Convenciones prácticas:

- Mantener **mensajes de error y comentarios** en **español** si el resto del módulo ya lo está, para consistencia con el proyecto.
- No subir artefactos de compilación ni binarios: ver la sección siguiente y el archivo **`.gitignore`** en la raíz del SDK.
- Probar al menos: compilar el SDK y ejecutar un `.jasb` mínimo (por ejemplo uno de `jas-compiler-c/tests/`).

---

## Qué no se sube a GitHub

La política está centralizada en **`.gitignore`** en la raíz de `sdk-dependiente` (y en `.gitignore` dentro de cada paquete por si se trabaja con un clon de una sola carpeta). En resumen **no** deben aparecer en el remoto:

| Categoría | Ejemplos |
|-----------|----------|
| Ejecutables y librerías generadas | `*.exe`, `*.dll`, `*.lib`, `*.a`, `*.pdb`, `*.ilk` |
| Objetos de compilación | `*.o`, `*.obj` |
| Salida del toolchain Jasboot | `*.jbo` |
| Carpetas de build | `build/`, `obj/`, `dist/`, `jasboot-ir/bin/`, `jas-compiler-c/bin/` |
| Logs y volcados | `*.log`, `vm_debug.log`, `build_*.txt` de la VM |
| Cachés de IDE y sistema | `.vs/`, `.idea/`, `.vscode/` (opcional local), `Thumbs.db`, `.DS_Store` |
| Node / extensiones empaquetadas | `node_modules/`, `*.vsix` |
| Python auxiliar | `__pycache__/`, `*.pyc`, `venv/` |
| Copias de seguridad | `*.bak`, `*.tmp`, `*.orig` |
| Ficheros JMN enormes (límite GitHub 100 MB) | `jasboot-ir/main.jmn` — no está en el remoto; añádelo en local si lo necesitas para pruebas JMN |

**Sí** deben versionarse, entre otros: fuentes `.c`/`.h`, `Makefile`/`build.bat`, scripts en **`scripts/`**, envoltorios **`bin/*.cmd`** y **`bin/*.bat`**, pruebas `.jasb`, y documentación `.md`.

Si ya compilaste antes de clonar reglas nuevas, puedes limpiar restos locales con `scripts\clean-build.bat` y borrar a mano archivos sueltos (por ejemplo un `.exe` en la raíz de `jasboot-ir/`).

---

## Licencia

Este SDK se distribuye bajo la **Licencia MIT**. El texto completo está en el archivo [`LICENSE`](LICENSE) en la raíz del repositorio. La extensión `vscode-jasboot/` reutiliza la misma licencia para mantener un criterio único en el árbol.

---

## Rama de trabajo

La rama **`main`** es la línea base integrada en el remoto. El desarrollo día a día debe hacerse en **ramas de características o correcciones** y fusionarse en `main` vía pull request (ver *Flujo con ramas* arriba).

---

## Resumen en una frase

**Clona el repo → `scripts\build-all.bat` → usa `jbc` para generar `.jbo` desde `.jasb` → usa `jasboot-ir-vm` para ejecutar el `.jbo`.** Los directorios `jas-compiler-c`, `jasboot-ir` y `jasboot-jmn-core` son las piezas que se enlazan en ese orden conceptual; `scripts/` y `bin/` organizan el flujo de trabajo en Windows.
