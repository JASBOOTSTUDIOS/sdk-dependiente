<p align="center">
  <img src="img/jasboot-icon.png" alt="Jasboot — logo del lenguaje" width="140" height="140">
</p>

# Jasboot (.jasb, .jd)

Resaltado de sintaxis (TextMate) alineado con el compilador **jbc** y con la capa declarativa de **Estructa JD**: comentarios `#`, `//`, `/* */`, cadenas `"` y cadenas con backticks `` `...` `` con escapes (`\n`, colores `\rojo`, etc.), **interpolación `${ ... }`** con llaves anidadas, conceptos `'...'`, números (decimal, hex `0x`, flotante), operadores (`=>`, `==`, `<<`, …), palabras clave y **APIs de sistema** (unión de `keywords.c` + `sistema_llamadas.c`: texto, listas, `n_*`, trig, FFI, …), `verdadero`/`falso`, palabras inglesas prohibidas resaltadas como error y nodos declarativos como `app`, `vista`, `tema`, `componente`, `rutas`, `columna`, `fila`, `tarjeta`, `texto`, `titulo`, `subtitulo`, `boton_ruta`, `boton_alerta`, `boton_secundario` y `jasb`.

**Regenerar la gramática** tras cambios en `jas-compiler-c/src/keywords.c` o `jas-compiler-c/src/sistema_llamadas.c`:

```bash
npm run grammar
```

Incluye colores por defecto para archivos `.jasb` y `.jd` (`configurationDefaults`, estilo oscuro tipo One Dark). Puedes anularlos en tus ajustes de usuario bajo `[jasboot]`. La extensión ya no instala un tema de iconos global para no modificar carpetas ni otros tipos de archivo del editor.

## Icono del lenguaje

- **Extensión:** el mismo logo aparece en la vista de extensiones (campo `icon` del manifiesto).
- **Pestañas y archivos del lenguaje:** VS Code/Cursor puede usar el icono del lenguaje cuando el tema activo lo permite.
- **Sin cambios globales:** la extensión no aporta ya un tema de iconos completo, así que no debe reemplazar iconos de carpetas ni de otros tipos de archivo.

## Generar el paquete con npm

En esta carpeta (`editor/vscode-jasboot`):

```bash
npm install
npm run package
```

Se crea **`jasboot-<version>.vsix`** según `package.json`.

Instalar el VSIX: **Extensions** → menú `⋯` → **Install from VSIX…**.

## Instalación desde carpeta (desarrollo)

1. `Ctrl+Shift+P` → **Developer: Install Extension from Location…**
2. Carpeta: `editor/vscode-jasboot`
3. Recarga la ventana si te lo pide.

**Nota:** el resaltado colorea y ayuda con comentarios/cierres; no sustituye un formateador completo tipo Prettier.
