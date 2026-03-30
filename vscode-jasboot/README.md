<p align="center">
  <img src="img/jasboot-icon.png" alt="Jasboot — logo del lenguaje" width="140" height="140">
</p>

# Jasboot (.jasb)

Resaltado de sintaxis (TextMate) alineado con el compilador **jbc**: comentarios `#`, `//`, `/* */`, cadenas `"` con escapes (`\n`, colores `\rojo`, etc.), **interpolación `${ ... }`** con llaves anidadas, conceptos `'...'`, números (decimal, hex `0x`, flotante), operadores (`=>`, `==`, `<<`, …), **213 palabras clave** agrupadas (control, tipos, E/S, biblioteca), `verdadero`/`falso`, y palabras inglesas prohibidas resaltadas como error.

**Regenerar la gramática** tras cambios en `sdk-dependiente/jas-compiler-c/src/keywords.c`:

```bash
npm run grammar
```

Incluye colores por defecto para archivos `.jasb` (`configurationDefaults`, estilo oscuro tipo One Dark). Puedes anularlos en tus ajustes de usuario bajo `[jasboot]`. También: icono del lenguaje y tema de iconos de explorador **Jasboot (.jasb)**.

## Icono del lenguaje y del explorador

- **Extensión:** el mismo logo aparece en la vista de extensiones (campo `icon` del manifiesto).
- **Pestañas y explorador:** VS Code/Cursor usa el icono del lenguaje cuando el tema de iconos lo permite (iconos de modo lenguaje).
- **Tema de iconos “Jasboot (.jasb)”:** para forzar este logo en **todos** los `.jasb` del explorador, elige  
  **Preferences: File Icon Theme** → **Jasboot (.jasb)**.

## Generar el paquete con npm

En esta carpeta (`editor/vscode-jasboot`):

```bash
npm install
npm run package
```

Se crea **`jasboot-<version>.vsix`** (p. ej. `0.0.4`) según `package.json`.

Instalar el VSIX: **Extensions** → menú `⋯` → **Install from VSIX…**.

## Instalación desde carpeta (desarrollo)

1. `Ctrl+Shift+P` → **Developer: Install Extension from Location…**
2. Carpeta: `editor/vscode-jasboot`
3. Recarga la ventana si te lo pide.

**Nota:** el resaltado colorea y ayuda con comentarios/cierres; no sustituye un formateador completo tipo Prettier.
