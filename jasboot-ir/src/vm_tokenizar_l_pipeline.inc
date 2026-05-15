/* Pipeline L completo (tokenizar_L / claves_L). Incluido desde vm.c tras vm_hash_texto(). */
#include "vm_tokenizar_unicode_bits.h"
#include "vm_unicode_norm.h"

#define VM_TOKEN_L_MOD_REG   240u
#define VM_TOKEN_L_MIN_REG   241u
#define VM_TOKEN_L_STOPS_REG 242u
#define VM_TOKEN_L_LEMMA_REG 243u

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
#define TL_MOD_TOKENIZE_SYMBOL_WS VM_TL_MOD_TOKENIZE_SYMBOL_WS
#define TL_MOD_TOKENIZE_SM_WS VM_TL_MOD_TOKENIZE_SM_WS
#define TL_MOD_TOKENIZE_SK_WS VM_TL_MOD_TOKENIZE_SK_WS
#define TL_MOD_COMMA_ASCII_LIST VM_TL_MOD_COMMA_ASCII_LIST
#define TL_MOD_SEGMENT_GRAPHEME VM_TL_MOD_SEGMENT_GRAPHEME
#define TL_MOD_LEMMA            VM_TL_MOD_LEMMA
#define TL_MOD_MAP_EXISTING_ONLY VM_TL_MOD_MAP_EXISTING_ONLY
#define TL_MOD_4GRAM            VM_TL_MOD_4GRAM
#define TL_MOD_5GRAM            VM_TL_MOD_5GRAM

#define VM_TL_MAX_TOK 384
#define VM_TL_MAX_CH  256

static char g_tl_st[VM_TL_MAX_TOK][VM_TL_MAX_CH];
static char g_tl_sw_extra[128][48];
static int g_tl_sw_n;

static struct { char w[48]; char l[48]; } g_tl_lemmas[256];
static int g_tl_lemmas_n;

static void* vm_tl_memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen) {
    if (needlelen == 0) return (void*)haystack;
    if (haystacklen < needlelen) return NULL;
    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;
    for (size_t i = 0; i <= haystacklen - needlelen; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, needlelen) == 0) return (void*)(h + i);
    }
    return NULL;
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
    for (; src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void vm_tl_join_ngrams(char* out, size_t cap, int n, int start_idx) {
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        const char* s = g_tl_st[start_idx + i];
        if (i > 0 && o + 1 < cap) out[o++] = ' ';
        while (*s && o + 1 < cap) out[o++] = *s++;
    }
    out[o] = '\0';
}

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
                case 0x91: case 0xB1: rep = 'n'; break;
                case 0x9C: case 0xBC: rep = 'u'; break;
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

static int vm_tl_ascii_lower_eq(const char* s, const char* low_ascii) {
    for (; *low_ascii; s++, low_ascii++) {
        if (!*s) return 0;
        if ((char)tolower((unsigned char)*s) != *low_ascii) return 0;
    }
    return *s == 0;
}

static void vm_tl_contract_merge_adjacent_tokens(int* pn) {
    int n = *pn;
    if (n < 2) return;
    for (;;) {
        int merged = 0;
        for (int i = 0; i + 1 < n; i++) {
            if (vm_tl_ascii_lower_eq(g_tl_st[i], "de") && vm_tl_ascii_lower_eq(g_tl_st[i + 1], "el")) {
                vm_tl_strcpy_cap(g_tl_st[i], VM_TL_MAX_CH, "del");
            } else if (vm_tl_ascii_lower_eq(g_tl_st[i], "a") && vm_tl_ascii_lower_eq(g_tl_st[i + 1], "el")) {
                vm_tl_strcpy_cap(g_tl_st[i], VM_TL_MAX_CH, "al");
            } else if (vm_tl_ascii_lower_eq(g_tl_st[i], "por") && vm_tl_ascii_lower_eq(g_tl_st[i + 1], "que")) {
                vm_tl_strcpy_cap(g_tl_st[i], VM_TL_MAX_CH, "porque");
            } else if (vm_tl_ascii_lower_eq(g_tl_st[i], "para") && vm_tl_ascii_lower_eq(g_tl_st[i + 1], "que")) {
                vm_tl_strcpy_cap(g_tl_st[i], VM_TL_MAX_CH, "paraque");
            } else continue;
            for (int j = i + 1; j + 1 < n; j++)
                vm_tl_strcpy_cap(g_tl_st[j], VM_TL_MAX_CH, g_tl_st[j + 1]);
            n--; *pn = n; merged = 1; break;
        }
        if (!merged) break;
    }
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
    "a", "al", "algo", "alguna", "alguno", "algunos", "ante", "antes", "como", "con", "contra", "cual", "cuales", "cuan", "cuando", "de", "del", "desde", "donde", "e", "el", "ella", "ellas", "ellos", "en", "entre", "era", "eran", "es", "esa", "esas", "ese", "eso", "esos", "esta", "estas", "este", "esto", "estos", "estoy", "fue", "fueron", "ha", "han", "hasta", "hay", "he", "la", "las", "le", "les", "lo", "los", "mas", "me", "mi", "mis", "muy", "nada", "ni", "no", "nos", "nosotros", "nuestra", "nuestras", "nuestro", "nuestros", "o", "os", "otra", "otras", "otro", "otros", "para", "pero", "poco", "por", "porque", "que", "quien", "quienes", "se", "sea", "ser", "si", "sin", "sobre", "su", "sus", "suyo", "tambien", "tan", "te", "tengo", "ti", "toda", "todas", "todo", "todos", "tu", "tus", "un", "una", "unas", "uno", "unos", "usted", "ustedes", "vuestra", "vuestras", "vuestro", "vuestros", "y", "ya", "yo"
};

static int vm_tl_is_builtin_stop(const char* w) {
    for (size_t i = 0; i < sizeof(g_tl_stops_es) / sizeof(g_tl_stops_es[0]); i++) {
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
        char* end = t + strlen(t);
        while (end > t && isspace((unsigned char)end[-1])) *--end = '\0';
        if (!*t) continue;
        vm_tl_strcpy_cap(g_tl_sw_extra[g_tl_sw_n++], 48, t);
    }
}

static int vm_tl_is_extra_stop(const char* w) {
    for (int i = 0; i < g_tl_sw_n; i++) {
        if (strcmp(w, g_tl_sw_extra[i]) == 0) return 1;
    }
    return 0;
}

static void vm_tl_parse_lemma_csv(const char* csv) {
    g_tl_lemmas_n = 0;
    if (!csv || !csv[0]) return;
    char buf[4096];
    vm_tl_strcpy_cap(buf, sizeof buf, csv);
    for (char* t = strtok(buf, ","); t && g_tl_lemmas_n < 256; t = strtok(NULL, ",")) {
        char* eq = strchr(t, '=');
        if (!eq) continue;
        *eq = '\0';
        char* w = t;
        char* l = eq + 1;
        while (*w && isspace((unsigned char)*w)) w++;
        char* ew = w + strlen(w);
        while (ew > w && isspace((unsigned char)ew[-1])) *--ew = '\0';
        while (*l && isspace((unsigned char)*l)) l++;
        char* el = l + strlen(l);
        while (el > l && isspace((unsigned char)el[-1])) *--el = '\0';
        if (!*w || !*l) continue;
        vm_tl_strcpy_cap(g_tl_lemmas[g_tl_lemmas_n].w, sizeof g_tl_lemmas[0].w, w);
        vm_tl_strcpy_cap(g_tl_lemmas[g_tl_lemmas_n].l, sizeof g_tl_lemmas[0].l, l);
        g_tl_lemmas_n++;
    }
}

static void vm_tl_apply_lemmas(char* s) {
    for (int i = 0; i < g_tl_lemmas_n; i++) {
        if (strcmp(s, g_tl_lemmas[i].w) == 0) {
            vm_tl_strcpy_cap(s, VM_TL_MAX_CH, g_tl_lemmas[i].l);
            return;
        }
    }
}

static int vm_tl_push_tok(void* udata, const char* src, size_t len) {
    int* pn = (int*)udata;
    if (*pn >= VM_TL_MAX_TOK || len >= VM_TL_MAX_CH) return 0;
    memcpy(g_tl_st[*pn], src, len);
    g_tl_st[*pn][len] = '\0';
    (*pn)++;
    return 1;
}

static void vm_tl_segment_fill(const char* start, const char* end, const char* sep, size_t sep_len, uint32_t modo, int* pn) {
    *pn = 0;
    if (start >= end) return;
    if (sep_len == 0) {
        if ((modo & VM_TL_MOD_SEGMENT_GRAPHEME) != 0u)
            vm_tl_utf8_segment_by_whitespace_graphemes(start, (size_t)(end - start), vm_tl_push_tok, pn);
        else
            vm_tl_utf8_segment_by_whitespace(start, (size_t)(end - start), vm_tl_push_tok, pn);
        return;
    }
    const char* p = start;
    while (p < end) {
        const char* next = (const char*)vm_tl_memmem(p, (size_t)(end - p), sep, sep_len);
        if (!next) {
            vm_tl_push_tok(pn, p, (size_t)(end - p));
            break;
        }
        if (next > p) vm_tl_push_tok(pn, p, (size_t)(next - p));
        p = next + sep_len;
    }
}

void vm_run_tokenizar_L(VM* vm, uint8_t dest_reg, uint32_t id_txt, uint32_t id_sep) {
    static uint32_t s_list_counter = 0;
    if (s_list_counter < 0x0FFFFFFFu) s_list_counter++;
    uint32_t list_id = 0xF0000000u + s_list_counter;

    JMNMemoria* m_target = (JMNMemoria*)(vm->mem_neuronal ? vm->mem_neuronal : vm->mem_colecciones);
    if (!m_target) {
        ensure_jmn_col(vm);
        m_target = (JMNMemoria*)vm->mem_colecciones;
    }
    if (m_target) {
        jmn_crear_lista(m_target, list_id);
    }

    uint32_t modo = (uint32_t)vm_get_register(vm, VM_TOKEN_L_MOD_REG);
    if ((modo & (TL_MOD_LOWER | TL_MOD_COLLAPSE)) == 0u) {
        modo |= TL_MOD_LOWER | TL_MOD_COLLAPSE;
    }

    uint64_t min_reg = vm_get_register(vm, VM_TOKEN_L_MIN_REG);
    uint32_t min_len = (min_reg > 0u) ? (uint32_t)min_reg : (((modo & TL_MOD_MIN2) != 0u) ? 2u : 1u);
    if (min_len > 64u) min_len = 64u;

    uint32_t stops_id = (uint32_t)vm_get_register(vm, VM_TOKEN_L_STOPS_REG);
    const char* stops_csv = NULL;
    if (stops_id) stops_csv = vm_text_cache_get(vm, stops_id);
    g_tl_sw_n = 0;
    if ((modo & TL_MOD_STOPWORDS) != 0u && stops_csv && stops_csv[0])
        vm_tl_parse_stops_csv(stops_csv);

    uint32_t lemma_id = (uint32_t)vm_get_register(vm, VM_TOKEN_L_LEMMA_REG);
    const char* lemma_csv = NULL;
    if (lemma_id) lemma_csv = vm_text_cache_get(vm, lemma_id);
    g_tl_lemmas_n = 0;
    if ((modo & TL_MOD_LEMMA) != 0u && lemma_csv && lemma_csv[0])
        vm_tl_parse_lemma_csv(lemma_csv);

    const char* texto_raw = vm_text_cache_get(vm, id_txt);
    if (texto_raw && texto_raw[0]) {
        char work[VM_TL_WORK_CAP];
        vm_tl_strcpy_cap(work, sizeof work, texto_raw);

        if ((modo & VM_TL_UNICODE_APPLY_MASK) != 0u)
            vm_tl_unicode_apply(work, sizeof work, modo);

        if ((modo & TL_MOD_NFKC_LATIN) != 0u)
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

        if ((modo & (TL_MOD_TOKENIZE_PUNCT_WS | TL_MOD_TOKENIZE_SYMBOL_WS | TL_MOD_TOKENIZE_SM_WS | TL_MOD_TOKENIZE_SK_WS)) != 0u) {
            vm_tl_punctuation_to_space_inplace(work, sizeof work, modo);
            char collapsed_p[VM_TL_WORK_CAP];
            if ((modo & TL_MOD_UNICODE_WS_FULL) != 0u)
                vm_tl_collapse_ws_unicode(work, collapsed_p, sizeof collapsed_p);
            else
                vm_tl_collapse_ws(work, collapsed_p, sizeof collapsed_p);
            vm_tl_strcpy_cap(work, sizeof work, collapsed_p);
        }

        const char* sep = vm_text_cache_get(vm, id_sep);
        size_t sep_len = sep ? strlen(sep) : 0;
        const char* texto = work;
        const char* texto_end = work + strlen(work);
        int n_tok = 0;
        vm_tl_segment_fill(texto, texto_end, sep, sep_len, modo, &n_tok);

        if ((modo & TL_MOD_CONTRACT) != 0u)
            vm_tl_contract_merge_adjacent_tokens(&n_tok);

        unsigned char keep[VM_TL_MAX_TOK];
        for (int i = 0; i < n_tok; i++) {
            vm_tl_utf8_trim_edges_inplace(g_tl_st[i]);
            if ((modo & TL_MOD_STRIP_PUNCT) != 0u)
                vm_tl_strip_punct_edges(g_tl_st[i]);
            if ((modo & TL_MOD_LOWER) != 0u) {
                if ((modo & VM_TL_UNICODE_NORM_FORM_MASK) == 0u) {
                    for (char* z = g_tl_st[i]; *z; z++) *z = (char)tolower((unsigned char)*z);
                }
            }
            if ((modo & TL_MOD_STEM) != 0u)
                vm_tl_stem_es_light(g_tl_st[i]);
            if ((modo & TL_MOD_LEMMA) != 0u)
                vm_tl_apply_lemmas(g_tl_st[i]);

            int sl = (int)strlen(g_tl_st[i]);
            int is_stop = 0;
            if ((modo & TL_MOD_STOPWORDS) != 0u) {
                is_stop = vm_tl_is_builtin_stop(g_tl_st[i]) || vm_tl_is_extra_stop(g_tl_st[i]);
            }
            keep[i] = (unsigned char)((sl >= (int)min_len && !is_stop) ? 1 : 0);
        }

        for (int i = 0; i < n_tok; i++) {
            if (!keep[i]) continue;
            const char* tn = g_tl_st[i];
            uint32_t token_id = vm_hash_texto(tn);
            
            if ((modo & TL_MOD_MAP_EXISTING_ONLY) != 0u) {
                if (!jmn_existe_texto((JMNMemoria*)vm->mem_neuronal, token_id)) continue;
            }

            vm_text_cache_put(vm, token_id, tn);
            JMNValor v_val;
            v_val.u = token_id;
            jmn_lista_agregar(m_target, list_id, v_val);
            if (vm->mem_neuronal) jmn_guardar_texto((JMNMemoria*)vm->mem_neuronal, token_id, tn);
        }

        int ngram_orders[] = { 2, 3, 4, 5 };
        uint32_t ngram_bits[] = { TL_MOD_BIGRAM, TL_MOD_TRIGRAM, TL_MOD_4GRAM, TL_MOD_5GRAM };
        
        for (int k = 0; k < 4; k++) {
            if ((modo & ngram_bits[k]) == 0u) continue;
            int order = ngram_orders[k];
            char gram[2048];
            for (int i = 0; i + order <= n_tok; i++) {
                int all_keep = 1;
                for (int j = 0; j < order; j++) if (!keep[i + j]) { all_keep = 0; break; }
                if (!all_keep) continue;
                
                vm_tl_join_ngrams(gram, sizeof gram, order, i);
                if ((modo & TL_MOD_LOWER) != 0u && ((modo & VM_TL_UNICODE_NORM_FORM_MASK) == 0u)) {
                    for (char* z = gram; *z; z++) *z = (char)tolower((unsigned char)*z);
                }
                if (strlen(gram) < (size_t)min_len) continue;
                
                uint32_t gid = vm_hash_texto(gram);
                if ((modo & TL_MOD_MAP_EXISTING_ONLY) != 0u) {
                    if (!jmn_existe_texto((JMNMemoria*)vm->mem_neuronal, gid)) continue;
                }
                
                vm_text_cache_put(vm, gid, gram);
                JMNValor v_ngram;
                v_ngram.u = gid;
                jmn_lista_agregar(m_target, list_id, v_ngram);
                if (vm->mem_neuronal) jmn_guardar_texto((JMNMemoria*)vm->mem_neuronal, gid, gram);
            }
        }

        uint32_t token_count = (uint32_t)jmn_lista_tamano(m_target, list_id);
        vm_list_size_cache_set(vm, list_id, token_count);
    }
    vm_set_register(vm, dest_reg, (uint64_t)list_id);
}
