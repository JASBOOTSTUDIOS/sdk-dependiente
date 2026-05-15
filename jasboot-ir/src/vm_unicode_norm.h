/* Unicode (utf8proc) para el pipeline L: formas NFC/NFD/NFKC/NFKD, casefold, BOM, strip, colapso WS. */
#ifndef VM_UNICODE_NORM_H
#define VM_UNICODE_NORM_H

#include "vm_tokenizar_unicode_bits.h"
#include <stddef.h>
#include <stdint.h>

/** BOM UTF-8 + normalización (una forma) + postproceso strip (marcas/CC). */
void vm_tl_unicode_apply(char* buf, size_t cap, uint32_t modo);

/** Solo formas NFC/NFD/NFKC/NFKD (+ casefold con bit 1). Prioridad: NFKC > NFKD > NFC > NFD. */
void vm_tl_unicode_normalize_buffer(char* buf, size_t cap, uint32_t modo);

/** Segunda pasada: STRIPMARK / STRIPCC (requiere cadena UTF-8 válida). */
void vm_tl_unicode_postprocess(char* buf, size_t cap, uint32_t modo);

/** Colapsa separadores Unicode (Zs, Zl, Zp) y ASCII isspace a un espacio ASCII. */
void vm_tl_collapse_ws_unicode(const char* in, char* out, size_t cap);

/**
 * Parte `texto[0 .. texto_len)` en tokens por separadores Unicode de espacio
 * (mismo criterio que `vm_tl_collapse_ws_unicode`: Zs/Zl/Zp, ASCII HT/LF/FF/CR/VT/SP, U+FEFF).
 * UTF-8 válido por codepoint; si un byte no inicia secuencia UTF-8 válida, se consume 1 byte
 * como parte del token actual (no se corta palabra).
 * @return 1 si terminó bien; 0 si `emit` devolvió 0 (p. ej. tope de tokens).
 */
typedef int (*vm_tl_ws_emit_fn)(void* udata, const char* src, size_t len);
int vm_tl_utf8_segment_by_whitespace(const char* texto, size_t texto_len, vm_tl_ws_emit_fn emit, void* udata);

/** Sustituye codepoints categoría P* por espacio ASCII in-place (compacta; UTF-8 válido). */
void vm_tl_punctuation_to_space_inplace(char* buf, size_t cap);

/** Copia `src[0..src_len)` a `out`, recorta bordes (espacio Unicode + comillas ASCII " ') y NUL-termina. */
int vm_tl_utf8_copy_trim_segment(const char* src, size_t src_len, char* out, size_t out_cap);

#endif
