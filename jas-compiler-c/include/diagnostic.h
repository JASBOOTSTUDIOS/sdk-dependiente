/* Formato de diagnosticos: mensaje + linea de codigo + marcador ^ */

#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

/* Adjunta debajo del titulo (headline) la linea line1 (1-based) y una marca ^ en col1 (1-based).
   Si source es NULL o line1/col1 invalidos, devuelve strdup(headline).
   El resultado debe liberarse con free(). */
char *diag_attach_snippet(const char *source, int line1, int col1, const char *headline);

/* Dos marcas ^ en las columnas col1 y col2 (1-based). Si line1 == line2, una sola linea de codigo
   y una fila con ambos acentos. Si line1 != line2, dos lineas de codigo y una marca por linea. */
char *diag_attach_snippet_two(const char *source, int line1, int col1, int line2, int col2, const char *headline);

#endif
