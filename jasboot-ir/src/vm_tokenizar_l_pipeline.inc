/* Pipeline L completo (tokenizar_L / claves_L). Incluido desde vm.c tras vm_hash_texto(). */

#include "vm_tokenizar_unicode_bits.h"
#include "vm_unicode_norm.h"

#define VM_TOKEN_L_MOD_REG   240u
#define VM_TOKEN_L_MIN_REG   241u
#define VM_TOKEN_L_STOPS_REG 242u

#define TL_MOD_LOWER        1u
#define TL_MOD_COLLAPSE     2u
#define TL_MOD_BIGRAM       4u
#define TL_MOD_MIN2         8u
#define TL_MOD_TRIGRAM      16u
#define TL_MOD_STEM         32u
#define TL_MOD_STRIP_PUNCT  64u
#define TL_MOD_NFKC_LATIN   128u
#define TL_MOD_STOPWORDS    256u
#define TL_MOD_CONTRACT     512u
#define TL_MOD_UNICODE_NFKC VM_TL_MOD_UNICODE_NFKC
#define TL_MOD_UNICODE_NFC  VM_TL_MOD_UNICODE_NFC
#define TL_MOD_UNICODE_NFD  VM_TL_MOD_UNICODE_NFD
#define TL_MOD_UNICODE_NFKD VM_TL_MOD_UNICODE_NFKD
#define TL_MOD_UNICODE_STRIPMARK VM_TL_MOD_UNICODE_STRIPMARK
#define TL_MOD_UNICODE_STRIPCC   VM_TL_MOD_UNICODE_STRIPCC
#define TL_MOD_STRIP_UTF8_BOM    VM_TL_MOD_STRIP_UTF8_BOM
#define TL_MOD_UNICODE_WS_FULL   VM_TL_MOD_UNICODE_WS_FULL
#define TL_MOD_TOKENIZE_PUNCT_WS VM_TL_MOD_TOKENIZE_PUNCT_WS

#define VM_TL_MAX_TOK 384
#define VM_TL_MAX_CH  256

static char g_tl_st[VM_TL_MAX_TOK][VM_TL_MAX_CH];
static char g_tl_sw_extra[128][48];
static int g_tl_sw_n;

static void vm_tl_trim_quotes_inplace(char* s) {
    char* p = s;
    while (*p && (isspace((unsigned char)*p) || *p == '"' || *p == '\'')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (isspace((unsigned char)s[n - 1]) || s[n - 1] == '"' || s[n - 1] == '\'')) {
        s[--n] = '\0';
    }
}

static void vm_tl_collapse_ws(const char* in, char* out, size_t cap) {
    size_t o = 0;
    int last_sp = 0;
    for (const char* p = in; *p && o + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (isspace(c)) {
            if (!last_sp && o > 0) {
                out[o++] = ' ';
                last_sp = 1;
            }
        } else {
            out[o++] = (char)c;
            last_sp = 0;
        }
    }
    out[o] = '\0';
    while (o > 0 && out[o - 1] == ' ') out[--o] = '\0';
    char* t = out;
    while (*t == ' ') t++;
    if (t != out) memmove(out, t, strlen(t) + 1);
}

static void vm_tl_strcpy_cap(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void vm_tl_join_space2(char* out, size_t cap, const char* a, const char* b) {
    size_t o = 0;
    while (*a && o + 1 < cap) out[o++] = *a++;
    if (o + 1 < cap) out[o++] = ' ';
    while (*b && o + 1 < cap) out[o++] = *b++;
    out[o] = '\0';
}

static void vm_tl_join_space3(char* out, size_t cap, const char* a, const char* b, const char* c) {
    size_t o = 0;
    while (*a && o + 1 < cap) out[o++] = *a++;
    if (o + 1 < cap) out[o++] = ' ';
    while (*b && o + 1 < cap) out[o++] = *b++;
    if (o + 1 < cap) out[o++] = ' ';
    while (*c && o + 1 < cap) out[o++] = *c++;
    out[o] = '\0';
}

/* NFKC-lite: UTF-8 C3 xx (Latin-1 supplement) -> ASCII vocal / n. */
static void vm_tl_fold_latin_utf8(char* buf) {
    char out[VM_TL_WORK_CAP];
    size_t o = 0;
    for (size_t i = 0; buf[i] && o + 1 < sizeof(out); ) {
        unsigned char c0 = (unsigned char)buf[i];
        unsigned char c1 = buf[i + 1] ? (unsigned char)buf[i + 1] : 0;
        if (c0 == 0xC3 && c1) {
            char rep = 0;
            switch (c1) {
                case 0x81: case 0xA1: rep = 'a'; break;
                case 0x89: case 0xA9: rep = 'e'; break;
                case 0x8D: case 0xAD: rep = 'i'; break;
                case 0x93: case 0xB3: rep = 'o'; break;
                case 0x9A: case 0xBA: rep = 'u'; break;
                case 0x91: case 0xB1: rep = 'n'; break; /* Ñ ñ -> n */
                case 0x9C: case 0xBC: rep = 'u'; break; /* Ü ü */
                default: break;
            }
            if (rep) {
                out[o++] = (char)tolower((unsigned char)rep);
                i += 2;
                continue;
            }
        }
        out[o++] = (char)c0;
        i++;
    }
    out[o] = '\0';
    vm_tl_strcpy_cap(buf, VM_TL_WORK_CAP, out);
}

static void vm_tl_contract_es(char* buf, size_t cap) {
    char tmp[VM_TL_WORK_CAP];
    struct { const char* from; const char* to; } reps[] = {
        { " de el ", " del " }, { " a el ", " al " }, { " por que ", " porque " }, { " para que ", " paraque " }
    };
    for (size_t r = 0; r < sizeof(reps) / sizeof(reps[0]); r++) {
        for (;;) {
            char* hit = strstr(buf, reps[r].from);
            if (!hit) break;
            size_t pre = (size_t)(hit - buf);
            size_t fl = strlen(reps[r].from);
            size_t tl = strlen(reps[r].to);
            if (pre + tl + strlen(hit + fl) + 1 > sizeof(tmp)) break;
            memcpy(tmp, buf, pre);
            memcpy(tmp + pre, reps[r].to, tl);
            size_t tail = strlen(hit + fl);
            memcpy(tmp + pre + tl, hit + fl, tail + 1u);
            vm_tl_strcpy_cap(buf, cap, tmp);
        }
    }
}

static void vm_tl_strip_punct_edges(char* s) {
    const char* punct = ".,;:!?\"'`";
    size_t n = strlen(s);
    while (n > 0 && strchr(punct, s[n - 1])) s[--n] = '\0';
    while (s[0] && strchr(punct, s[0])) memmove(s, s + 1, strlen(s));
}

static void vm_tl_stem_es_light(char* s) {
    size_t n = strlen(s);
    if (n < 6) return;
    const char* suf[] = { "mente", "aciones", "acion", "imiento", "adoras", "adores" };
    for (size_t i = 0; i < sizeof(suf) / sizeof(suf[0]); i++) {
        size_t sl = strlen(suf[i]);
        if (n > sl + 2 && strcmp(s + n - sl, suf[i]) == 0) {
            s[n - sl] = '\0';
            return;
        }
    }
    if (n > 3 && s[n - 1] == 's' && strchr("aeiou", (int)(unsigned char)s[n - 2])) {
        s[n - 1] = '\0';
    }
}

static const char* const g_tl_stops_es[] = {
    "a", "al", "algo", "alguna", "alguno", "algunos", "ante", "antes", "como", "con", "contra",
    "cual", "cuales", "cuan", "cuando", "de", "del", "desde", "donde", "e", "el", "ella", "ellas",
    "ellos", "en", "entre", "era", "eran", "es", "esa", "esas", "ese", "eso", "esos", "esta", "estas",
    "este", "esto", "estos", "estoy", "fue", "fueron", "ha", "han", "hasta", "hay", "he", "la", "las",
    "le", "les", "lo", "los", "mas", "me", "mi", "mis", "muy", "nada", "ni", "no", "nos", "nosotros",
    "nuestra", "nuestras", "nuestro", "nuestros", "o", "os", "otra", "otras", "otro", "otros", "para",
    "pero", "poco", "por", "porque", "que", "quien", "quienes", "se", "sea", "ser", "si", "sin", "sobre",
    "su", "sus", "suyo", "tambien", "tan", "te", "tengo", "ti", "toda", "todas", "todo", "todos", "tu",
    "tus", "u", "un", "una", "uno", "unos", "vosotros", "vuestra", "vuestras", "vuestro", "vuestros",
    "y", "ya", "yo"
};

static int vm_tl_is_builtin_stop(const char* w) {
    size_t n = sizeof(g_tl_stops_es) / sizeof(g_tl_stops_es[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(w, g_tl_stops_es[i]) == 0) return 1;
    }
    return 0;
}

static void vm_tl_parse_stops_csv(const char* csv) {
    g_tl_sw_n = 0;
    if (!csv || !csv[0]) return;
    char buf[2048];
    vm_tl_strcpy_cap(buf, sizeof buf, csv);
    for (char* t = strtok(buf, ","); t && g_tl_sw_n < 128; t = strtok(NULL, ",")) {
        while (*t && isspace((unsigned char)*t)) t++;
        char* e = t + strlen(t);
        while (e > t && isspace((unsigned char)e[-1])) *--e = '\0';
        if (!*t) continue;
        vm_tl_strcpy_cap(g_tl_sw_extra[g_tl_sw_n], sizeof g_tl_sw_extra[0], t);
        for (char* z = g_tl_sw_extra[g_tl_sw_n]; *z; z++) *z = (char)tolower((unsigned char)*z);
        g_tl_sw_n++;
    }
}

static int vm_tl_is_extra_stop(const char* w) {
    for (int i = 0; i < g_tl_sw_n; i++) {
        if (strcmp(w, g_tl_sw_extra[i]) == 0) return 1;
    }
    return 0;
}

static int vm_tl_push_tok(int* pn, const char* src, size_t len) {
    if (*pn >= VM_TL_MAX_TOK) return 0;
    if (len >= VM_TL_MAX_CH) len = VM_TL_MAX_CH - 1;
    memcpy(g_tl_st[*pn], src, len);
    g_tl_st[*pn][len] = '\0';
    (*pn)++;
    return 1;
}

struct vm_tl_seg_push_ctx { int* pn; };
static int vm_tl_seg_emit_push(void* udata, const char* src, size_t len) {
    struct vm_tl_seg_push_ctx* c = (struct vm_tl_seg_push_ctx*)udata;
    return vm_tl_push_tok(c->pn, src, len);
}

static void vm_tl_push_segment_trimmed_utf8(const char* q, const char* seg_end, int* pn) {
    char segbuf[VM_TL_WORK_CAP];
    size_t segbytes = (size_t)(seg_end > q ? (size_t)(seg_end - q) : 0u);
    if (segbytes == 0u) return;
    if (segbytes >= VM_TL_WORK_CAP) segbytes = VM_TL_WORK_CAP - 1u;
    if (!vm_tl_utf8_copy_trim_segment(q, segbytes, segbuf, sizeof segbuf)) return;
    if (segbuf[0] == '\0') return;
    (void)vm_tl_push_tok(pn, segbuf, strlen(segbuf));
}

static void vm_tl_segment_fill(const char* texto, size_t texto_end, const char* sep, size_t sep_len, int* pn) {
    *pn = 0;
    const char* texto_endp = texto + texto_end;
    if (sep_len == 0) {
        struct vm_tl_seg_push_ctx ctx = { pn };
        (void)vm_tl_utf8_segment_by_whitespace(texto, texto_end, vm_tl_seg_emit_push, &ctx);
        return;
    } else if (sep_len == 1) {
        unsigned char sep_ch = (unsigned char)sep[0];
        const char* p = texto;
        for (;;) {
            const char* found = p;
            while (found < texto_endp && (unsigned char)*found != sep_ch) found++;
            const char* seg_end = found;
            vm_tl_push_segment_trimmed_utf8(p, seg_end, pn);
            if (found >= texto_endp) break;
            p = found + 1;
        }
    } else {
        const char* p = texto;
        for (;;) {
            const char* found = strstr(p, sep);
            const char* seg_end = found ? found : texto_endp;
            vm_tl_push_segment_trimmed_utf8(p, seg_end, pn);
            if (!found) break;
            p = found + sep_len;
        }
    }
}

static void vm_run_tokenizar_L(VM* vm, uint8_t dest_reg, uint32_t id_txt, uint32_t id_sep) {
    uint32_t list_id = 0;
    uint64_t modo64 = vm_get_register(vm, VM_TOKEN_L_MOD_REG);
    uint32_t modo = (uint32_t)(modo64 & 0xFFFFFFFFu);
    if (modo == 0u) modo = TL_MOD_LOWER | TL_MOD_COLLAPSE;
    else if ((modo & (TL_MOD_LOWER | TL_MOD_COLLAPSE)) == 0u)
        modo |= TL_MOD_LOWER | TL_MOD_COLLAPSE;

    uint64_t min_reg = vm_get_register(vm, VM_TOKEN_L_MIN_REG);
    uint32_t min_len = (min_reg > 0u) ? (uint32_t)min_reg : (((modo & TL_MOD_MIN2) != 0u) ? 2u : 1u);
    if (min_len > 64u) min_len = 64u;

    uint32_t stops_id = (uint32_t)vm_get_register(vm, VM_TOKEN_L_STOPS_REG);
    const char* stops_csv = NULL;
    if (stops_id) stops_csv = vm_text_cache_get(vm, stops_id);
    g_tl_sw_n = 0;
    if ((modo & TL_MOD_STOPWORDS) != 0u && stops_csv && stops_csv[0])
        vm_tl_parse_stops_csv(stops_csv);

    const char* texto_raw = vm_text_cache_get(vm, id_txt);
    const char* sep = vm_text_cache_get(vm, id_sep);
    char work[VM_TL_WORK_CAP];
    work[0] = '\0';
    if (texto_raw) {
        vm_tl_strcpy_cap(work, sizeof work, texto_raw);
        if ((modo & VM_TL_UNICODE_APPLY_MASK) != 0u)
            vm_tl_unicode_apply(work, sizeof work, modo);
        if ((modo & TL_MOD_NFKC_LATIN) != 0u && ((modo & VM_TL_UNICODE_NORM_FORM_MASK) == 0u))
            vm_tl_fold_latin_utf8(work);
        if ((modo & TL_MOD_CONTRACT) != 0u)
            vm_tl_contract_es(work, sizeof(work));
        if ((modo & TL_MOD_COLLAPSE) != 0u) {
            char collapsed[VM_TL_WORK_CAP];
            if ((modo & TL_MOD_UNICODE_WS_FULL) != 0u)
                vm_tl_collapse_ws_unicode(work, collapsed, sizeof collapsed);
            else
                vm_tl_collapse_ws(work, collapsed, sizeof collapsed);
            vm_tl_strcpy_cap(work, sizeof work, collapsed);
        }
        vm_tl_trim_quotes_inplace(work);
        if ((modo & TL_MOD_TOKENIZE_PUNCT_WS) != 0u) {
            vm_tl_punctuation_to_space_inplace(work, sizeof work);
            if ((modo & TL_MOD_COLLAPSE) != 0u) {
                char collapsed_p[VM_TL_WORK_CAP];
                if ((modo & TL_MOD_UNICODE_WS_FULL) != 0u)
                    vm_tl_collapse_ws_unicode(work, collapsed_p, sizeof collapsed_p);
                else
                    vm_tl_collapse_ws(work, collapsed_p, sizeof collapsed_p);
                vm_tl_strcpy_cap(work, sizeof work, collapsed_p);
            }
        }
    }
    const char* texto = work;
    size_t sep_len = sep ? strlen(sep) : 0;
    size_t texto_end = strlen(texto);

    int n_tok = 0;
    vm_tl_segment_fill(texto, texto_end, sep, sep_len, &n_tok);

    unsigned char keep[VM_TL_MAX_TOK];
    for (int i = 0; i < n_tok; i++) {
        vm_tl_trim_quotes_inplace(g_tl_st[i]);
        if ((modo & TL_MOD_STRIP_PUNCT) != 0u)
            vm_tl_strip_punct_edges(g_tl_st[i]);
        /* tolower byte a byte solo si no hay normalización Unicode (rompería UTF-8 multibyte). */
        if ((modo & TL_MOD_LOWER) != 0u
            && ((modo & VM_TL_UNICODE_NORM_FORM_MASK) == 0u)) {
            for (char* z = g_tl_st[i]; *z; z++) *z = (char)tolower((unsigned char)*z);
        }
        if ((modo & TL_MOD_STEM) != 0u)
            vm_tl_stem_es_light(g_tl_st[i]);
        int sl = (int)strlen(g_tl_st[i]);
        int is_stop = 0;
        if ((modo & TL_MOD_STOPWORDS) != 0u)
            is_stop = vm_tl_is_builtin_stop(g_tl_st[i]) || vm_tl_is_extra_stop(g_tl_st[i]);
        keep[i] = (unsigned char)((sl >= (int)min_len && !is_stop) ? 1 : 0);
    }

#ifdef JASBOOT_LANG_INTEGRATION
    if (texto_raw)
        ensure_jmn_col(vm);
    if (texto_raw && vm->mem_colecciones) {
        list_id = (uint32_t)(vm_hash_texto(texto_raw) ^ vm_hash_texto(sep ? sep : "") ^ 0x544B4E4Cu);
        jmn_crear_lista(vm->mem_colecciones, list_id);

        for (int i = 0; i < n_tok; i++) {
            if (!keep[i]) continue;
            const char* tn = g_tl_st[i];
            uint32_t token_id = vm_hash_texto(tn);
            vm_text_cache_put(vm, token_id, tn);
            JMNValor v_val;
            v_val.u = token_id;
            jmn_lista_agregar(vm->mem_colecciones, list_id, v_val);
            if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, token_id, tn);
        }

        if ((modo & TL_MOD_BIGRAM) != 0u) {
            char bi[1024];
            for (int i = 0; i + 1 < n_tok; i++) {
                if (!keep[i] || !keep[i + 1]) continue;
                vm_tl_join_space2(bi, sizeof bi, g_tl_st[i], g_tl_st[i + 1]);
                if ((modo & TL_MOD_LOWER) != 0u
                    && ((modo & VM_TL_UNICODE_NORM_FORM_MASK) == 0u)) {
                    for (char* z = bi; *z; z++) *z = (char)tolower((unsigned char)*z);
                }
                if (strlen(bi) < (size_t)min_len) continue;
                uint32_t bid = vm_hash_texto(bi);
                vm_text_cache_put(vm, bid, bi);
                JMNValor v2;
                v2.u = bid;
                jmn_lista_agregar(vm->mem_colecciones, list_id, v2);
                if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, bid, bi);
            }
        }

        if ((modo & TL_MOD_TRIGRAM) != 0u) {
            char tri[1536];
            for (int i = 0; i + 2 < n_tok; i++) {
                if (!keep[i] || !keep[i + 1] || !keep[i + 2]) continue;
                vm_tl_join_space3(tri, sizeof tri, g_tl_st[i], g_tl_st[i + 1], g_tl_st[i + 2]);
                if ((modo & TL_MOD_LOWER) != 0u
                    && ((modo & VM_TL_UNICODE_NORM_FORM_MASK) == 0u)) {
                    for (char* z = tri; *z; z++) *z = (char)tolower((unsigned char)*z);
                }
                if (strlen(tri) < (size_t)min_len) continue;
                uint32_t tid = vm_hash_texto(tri);
                vm_text_cache_put(vm, tid, tri);
                JMNValor v3;
                v3.u = tid;
                jmn_lista_agregar(vm->mem_colecciones, list_id, v3);
                if (vm->mem_neuronal) jmn_guardar_texto(vm->mem_neuronal, tid, tri);
            }
        }

        uint32_t token_count = (uint32_t)jmn_lista_tamano(vm->mem_colecciones, list_id);
        vm_list_size_cache_set(vm, list_id, token_count);
    }
#endif
    vm_set_register(vm, dest_reg, (uint64_t)list_id);
}
