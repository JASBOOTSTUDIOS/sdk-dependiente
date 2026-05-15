#include "vm_unicode_norm.h"
#define UTF8PROC_STATIC
#include "../third_party/utf8proc/utf8proc.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VM_TL_MOD_LOWER 1u

static void vm_tl_strip_utf8_bom(char* buf) {
    if (!buf || !buf[0]) return;
    if ((unsigned char)buf[0] == 0xEFu && (unsigned char)buf[1] == 0xBBu && (unsigned char)buf[2] == 0xBFu) {
        size_t n = strlen(buf + 3);
        memmove(buf, buf + 3, n + 1u);
    }
}

static int vm_tl_cp_is_space_like(utf8proc_int32_t cp) {
    if (cp <= 0) return 0;
    if (cp <= 0x20) {
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' || cp == '\v') return 1;
        return 0;
    }
    if (cp == 0xFEFF) return 1;
    utf8proc_category_t cat = utf8proc_category(cp);
    return (cat == UTF8PROC_CATEGORY_ZS || cat == UTF8PROC_CATEGORY_ZL || cat == UTF8PROC_CATEGORY_ZP);
}

void vm_tl_collapse_ws_unicode(const char* in, char* out, size_t cap) {
    if (!in || !out || cap < 2) {
        if (out && cap) out[0] = '\0';
        return;
    }
    size_t o = 0;
    int pending = 0;
    utf8proc_ssize_t pos = 0;
    for (;;) {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t adv = utf8proc_iterate((const utf8proc_uint8_t*)in + pos, -1, &cp);
        if (adv <= 0) break;
        if (vm_tl_cp_is_space_like(cp)) {
            pending = 1;
        } else {
            if (pending && o > 0 && o + 1 < cap) out[o++] = ' ';
            pending = 0;
            utf8proc_ssize_t w = utf8proc_encode_char(cp, (utf8proc_uint8_t*)out + o);
            if (w <= 0 || (size_t)w >= cap - o) break;
            o += (size_t)w;
        }
        pos += adv;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = '\0';
    char* t = out;
    while (*t == ' ') t++;
    if (t != out) memmove(out, t, strlen(t) + 1u);
}

int vm_tl_utf8_segment_by_whitespace(const char* texto, size_t texto_len, vm_tl_ws_emit_fn emit, void* udata) {
    if (!emit) return 0;
    if (!texto || texto_len == 0) return 1;
    utf8proc_ssize_t pos = 0;
    utf8proc_ssize_t run_start = 0;
    while ((size_t)pos < texto_len) {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t adv = utf8proc_iterate(
            (const utf8proc_uint8_t*)texto + pos,
            (utf8proc_ssize_t)(texto_len - (size_t)pos),
            &cp);
        if (adv <= 0) {
            /* UTF-8 inválido o truncado: un byte hacia el run actual (no dividir). */
            if ((size_t)pos >= texto_len) break;
            pos += 1;
            continue;
        }
        if (vm_tl_cp_is_space_like(cp)) {
            size_t b0 = (size_t)run_start;
            size_t b1 = (size_t)pos;
            if (b1 > b0) {
                if (!emit(udata, texto + b0, b1 - b0)) return 0;
            }
            run_start = pos + adv;
        }
        pos += adv;
    }
    size_t b0 = (size_t)run_start;
    if (b0 < texto_len) {
        if (!emit(udata, texto + b0, texto_len - b0)) return 0;
    }
    return 1;
}

static int vm_tl_cp_is_punct_cat(utf8proc_int32_t cp) {
    utf8proc_category_t c = utf8proc_category(cp);
    return (c == UTF8PROC_CATEGORY_PD || c == UTF8PROC_CATEGORY_PE || c == UTF8PROC_CATEGORY_PF
            || c == UTF8PROC_CATEGORY_PI || c == UTF8PROC_CATEGORY_PO || c == UTF8PROC_CATEGORY_PS
            || c == UTF8PROC_CATEGORY_PC);
}

void vm_tl_punctuation_to_space_inplace(char* buf, size_t cap) {
    if (!buf || cap < 2) return;
    char tmp[8192];
    if (cap > sizeof tmp) cap = sizeof tmp;
    size_t w = 0;
    int pending_space = 0;
    utf8proc_ssize_t pos = 0;
    for (;;) {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t adv = utf8proc_iterate((const utf8proc_uint8_t*)buf + pos, -1, &cp);
        if (adv <= 0) break;
        if (vm_tl_cp_is_punct_cat(cp)) {
            pending_space = 1;
        } else {
            if (pending_space) {
                if (w == 0 || (w > 0 && tmp[w - 1] != ' ')) {
                    if (w + 1 < cap) tmp[w++] = ' ';
                }
                pending_space = 0;
            }
            if ((size_t)adv >= cap - w) break;
            memcpy(tmp + w, buf + pos, (size_t)adv);
            w += (size_t)adv;
        }
        pos += adv;
    }
    if (pending_space && w > 0 && tmp[w - 1] != ' ' && w + 1 < cap) tmp[w++] = ' ';
    tmp[w] = '\0';
    size_t i = 0;
    for (; tmp[i] && i + 1 < cap; i++) buf[i] = tmp[i];
    buf[i] = '\0';
}

void vm_tl_utf8_trim_edges_inplace(char* s) {
    if (!s || !s[0]) return;
    for (;;) {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t adv = utf8proc_iterate((const utf8proc_uint8_t*)s, -1, &cp);
        if (adv <= 0) break;
        if (vm_tl_cp_is_space_like(cp)) {
            memmove(s, s + adv, strlen(s + adv) + 1u);
            continue;
        }
        if (adv == 1 && (s[0] == '"' || s[0] == '\'')) {
            memmove(s, s + 1, strlen(s + 1) + 1u);
            continue;
        }
        break;
    }
    for (;;) {
        size_t L = strlen(s);
        if (L == 0) break;
        const char* end = s + L;
        const char* u = end - 1;
        while (u > s && ((unsigned char)*u & 0xC0u) == 0x80u) u--;
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t adv = utf8proc_iterate((const utf8proc_uint8_t*)u, (utf8proc_ssize_t)(end - u), &cp);
        if (adv <= 0 || (size_t)((const char*)u - s) + (size_t)adv != L) {
            s[L - 1] = '\0';
            continue;
        }
        if (vm_tl_cp_is_space_like(cp)) {
            s[(size_t)(u - s)] = '\0';
            continue;
        }
        if (adv == 1 && (*u == '"' || *u == '\'')) {
            s[(size_t)(u - s)] = '\0';
            continue;
        }
        break;
    }
}

int vm_tl_utf8_copy_trim_segment(const char* src, size_t src_len, char* out, size_t out_cap) {
    if (!src || !out || out_cap < 2) return 0;
    size_t n = src_len;
    if (n >= out_cap) n = out_cap - 1u;
    memcpy(out, src, n);
    out[n] = '\0';
    vm_tl_utf8_trim_edges_inplace(out);
    return 1;
}

void vm_tl_unicode_normalize_buffer(char* buf, size_t cap, uint32_t modo) {
    if (!buf || cap == 0) return;
    const int want_nfkc = (modo & VM_TL_MOD_UNICODE_NFKC) != 0;
    const int want_nfkd = (modo & VM_TL_MOD_UNICODE_NFKD) != 0;
    const int want_nfc = (modo & VM_TL_MOD_UNICODE_NFC) != 0;
    const int want_nfd = (modo & VM_TL_MOD_UNICODE_NFD) != 0;
    if (!want_nfkc && !want_nfkd && !want_nfc && !want_nfd) return;

    const utf8proc_uint8_t* in = (const utf8proc_uint8_t*)buf;
    utf8proc_uint8_t* out = NULL;
    const int lower = (modo & VM_TL_MOD_LOWER) != 0;

    if (want_nfkc) {
        out = lower ? utf8proc_NFKC_Casefold(in) : utf8proc_NFKC(in);
    } else if (want_nfkd) {
        if (lower) {
            utf8proc_uint8_t* mapped = NULL;
            const utf8proc_option_t opt = (utf8proc_option_t)(
                UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT | UTF8PROC_CASEFOLD);
            if (utf8proc_map(in, -1, &mapped, opt) < 0 || !mapped) return;
            out = mapped;
        } else
            out = utf8proc_NFKD(in);
    } else if (want_nfc) {
        if (lower) {
            utf8proc_uint8_t* mapped = NULL;
            const utf8proc_option_t opt = (utf8proc_option_t)(
                UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD);
            if (utf8proc_map(in, -1, &mapped, opt) < 0 || !mapped) return;
            out = mapped;
        } else
            out = utf8proc_NFC(in);
    } else {
        /* NFD */
        if (lower) {
            utf8proc_uint8_t* mapped = NULL;
            const utf8proc_option_t opt = (utf8proc_option_t)(
                UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_CASEFOLD);
            if (utf8proc_map(in, -1, &mapped, opt) < 0 || !mapped) return;
            out = mapped;
        } else
            out = utf8proc_NFD(in);
    }

    if (!out) return;
    size_t i = 0;
    for (; out[i] && i + 1 < cap; i++)
        buf[i] = (char)out[i];
    buf[i] = '\0';
    free(out);
}

void vm_tl_unicode_postprocess(char* buf, size_t cap, uint32_t modo) {
    if (!buf || cap == 0) return;
    const int sm = (modo & VM_TL_MOD_UNICODE_STRIPMARK) != 0;
    const int scc = (modo & VM_TL_MOD_UNICODE_STRIPCC) != 0;
    if (!sm && !scc) return;

    utf8proc_uint8_t* mapped = NULL;
    utf8proc_option_t opt = (utf8proc_option_t)(UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    if (sm) opt = (utf8proc_option_t)(opt | UTF8PROC_STRIPMARK);
    if (scc) opt = (utf8proc_option_t)(opt | UTF8PROC_STRIPCC);
    if (utf8proc_map((const utf8proc_uint8_t*)buf, -1, &mapped, opt) < 0 || !mapped) return;
    size_t i = 0;
    for (; mapped[i] && i + 1 < cap; i++)
        buf[i] = (char)mapped[i];
    buf[i] = '\0';
    free(mapped);
}

void vm_tl_unicode_apply(char* buf, size_t cap, uint32_t modo) {
    if (!buf || cap == 0) return;
    if ((modo & VM_TL_MOD_STRIP_UTF8_BOM) != 0) vm_tl_strip_utf8_bom(buf);
    if ((modo & VM_TL_UNICODE_NORM_FORM_MASK) != 0) vm_tl_unicode_normalize_buffer(buf, cap, modo);
    if ((modo & (VM_TL_MOD_UNICODE_STRIPMARK | VM_TL_MOD_UNICODE_STRIPCC)) != 0)
        vm_tl_unicode_postprocess(buf, cap, modo);
}
