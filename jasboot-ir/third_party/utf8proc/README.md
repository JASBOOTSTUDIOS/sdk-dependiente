# utf8proc (vendido en el árbol)

- **Versión:** 2.11.3  
- **Origen:** https://github.com/JuliaStrings/utf8proc  
- **Uso en Jasboot:** normalización Unicode (NFC/NFKC/NFD/NFKD, casefold, strip) y **segmentación por espacio Unicode** (`vm_tl_utf8_segment_by_whitespace`) en `vm_unicode_norm.c` para `tokenizar_L` / `claves_L`.  
- **Licencia:** `LICENSE.md` (MIT/expat + términos de datos Unicode).

Solo se compila `utf8proc.c` (incluye `utf8proc_data.c`).
