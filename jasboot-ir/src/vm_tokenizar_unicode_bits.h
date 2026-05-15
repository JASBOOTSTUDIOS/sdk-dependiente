/* Bits y constantes compartidas: pipeline L (vm_tokenizar_l_pipeline.inc) y vm_unicode_norm.c */
#ifndef VM_TOKENIZAR_UNICODE_BITS_H
#define VM_TOKENIZAR_UNICODE_BITS_H

/** Tamaño del buffer de trabajo `work[]` y auxiliares alineados (NUL final). */
#define VM_TL_WORK_CAP 8192u

/** Unicode: formas canónicas (utf8proc). A lo sumo una a la vez; prioridad NFKC > NFKD > NFC > NFD. */
#define VM_TL_MOD_UNICODE_NFKC 1024u
#define VM_TL_MOD_UNICODE_NFC  2048u
#define VM_TL_MOD_UNICODE_NFD  4096u
#define VM_TL_MOD_UNICODE_NFKD 8192u

/** Tras normalizar: quitar marcas combinantes (acentos) y/o caracteres de control (utf8proc). */
#define VM_TL_MOD_UNICODE_STRIPMARK 16384u
#define VM_TL_MOD_UNICODE_STRIPCC   65536u

/** Quitar BOM UTF-8 (EF BB BF) al inicio del buffer antes de normalizar. */
#define VM_TL_MOD_STRIP_UTF8_BOM 32768u

/** Con bit **2** (colapsar): usar colapso de separadores Unicode (Zs/Zl/Zp + ASCII isspace) en UTF-8. */
#define VM_TL_MOD_UNICODE_WS_FULL 131072u

/**
 * Antes de segmentar: sustituir caracteres Unicode de **puntuación** (categorías P*) por espacio ASCII,
 * para fronteras de token tipo Neurixis / CSV natural (`,.;:!?` y equivalentes Unicode).
 * Suele combinarse con **colapsar (2)** y opcionalmente **131072** para colapsar runs de espacio.
 */
#define VM_TL_MOD_TOKENIZE_PUNCT_WS 262144u

#define VM_TL_UNICODE_NORM_FORM_MASK \
    (VM_TL_MOD_UNICODE_NFKC | VM_TL_MOD_UNICODE_NFC | VM_TL_MOD_UNICODE_NFD | VM_TL_MOD_UNICODE_NFKD)

/** Si alguno está activo, se ejecuta `vm_tl_unicode_apply` (BOM / forma / strip). */
#define VM_TL_UNICODE_APPLY_MASK \
    (VM_TL_UNICODE_NORM_FORM_MASK | VM_TL_MOD_UNICODE_STRIPMARK | VM_TL_MOD_UNICODE_STRIPCC \
     | VM_TL_MOD_STRIP_UTF8_BOM)

#endif
