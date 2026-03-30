<p align="center">
  <img src="../assets/jasboot-icon.png" alt="Jasboot — logo del lenguaje" width="120" height="120">
</p>

# jasboot-compiler (jbc)

Compilador **C** del lenguaje Jasboot: traduce `.jasb` a **IR binario** (`.jbo`) ejecutable por la VM [`jasboot-ir`](../jasboot-ir).

## Requisitos

- **gcc** (o compatible) con soporte **C11**.
- Windows: ejecutar `build.bat` desde esta carpeta.

## Compilar

```bat
build.bat
```

Genera `jbc.exe` en la raíz del monorepo Jasboot (`../../bin/jbc.exe`) y copia una copia a `../bin/jbc.exe` si existe esa carpeta.

En un **clon solo de este repo**, edita `build.bat` para fijar la ruta de salida del ejecutable (por ejemplo `.\bin\jbc.exe`).

## Uso

```bat
jbc.exe programa.jasb -e
```

`-e` compila y ejecuta con la VM si está en el PATH o junto al toolchain (ver documentación del repo `jasboot-ir`).

## Estructura

| Ruta | Rol |
|------|-----|
| `include/` | Cabeceras públicas del compilador |
| `src/` | Lexer, parser, codegen, `main.c` |
| `tests/` | Programas `.jasb` de prueba |

## Toolchain estable en `sdk-dependiente`

- **JMN + consola (VM):** [`jasboot-jmn-core`](../jasboot-jmn-core) — usado al compilar la VM, no por este `build.bat`.
- **VM / IR:** [`jasboot-ir`](../jasboot-ir).

Este compilador incluye su propia copia de fuentes bajo `src/memoria_neuronal/` cuando el codegen las necesita; no depende de `jasboot-jmn-core` para construir `jbc`.

## Repositorio independiente

Este directorio está pensado para publicarse como repositorio Git propio (nombre sugerido: **`jasboot-compiler`**). Para empaquetar una copia limpia desde el monorepo Jasboot, usa `scripts/preparar-repos-github.ps1` en la raíz del monorepo.
