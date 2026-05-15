# -*- coding: utf-8 -*-
"""Genera test_tokenizar_L_seg_ws_unicode_300.jasb: 300 casos segmentacion UTF-8 (sep vacio)."""
import os

NBSP = "\u00a0"
EMSP = "\u2003"
ENSP = "\u2002"
THSP = "\u2009"
LS = "\u2028"  # Zl Line Separator
PS = "\u2029"  # Zp Paragraph Separator
FEFF = "\ufeff"


def esc_bt(s: str) -> str:
    return s.replace("\\", "\\\\").replace("`", "\\`")


def build_case(i: int) -> tuple[str, int, int]:
    """(literal_utf8, modo, lista_tamano_esperado)."""
    r = i % 7
    if r == 0:
        n = 2 + (i % 8)
        sep = NBSP * (1 + (i % 3))
        s = sep.join(["tok" + str(j % 10) for j in range(n)])
        return s, 3, n
    if r == 1:
        s = "alpha" + EMSP + "beta" + THSP + "gamma"
        return s, 3, 3
    if r == 2:
        s = "x" + "\t\n\r" + "y" + NBSP + "z"
        return s, 3, 3
    if r == 3:
        s = FEFF + "uno" + NBSP + "dos"
        return s, 3, 2
    if r == 4:
        s = "a" + LS + "b" + PS + "c"
        return s, 3, 3
    if r == 5:
        s = ENSP.join(["w", "x", "y"])
        return s, 1027, 3
    # r == 6: cadena larga repetida palabras ASCII + NBSP
    parts = ["palabra"] * (4 + (i % 12))
    s = NBSP.join(parts)
    return s, 3, len(parts)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, "test_tokenizar_L_seg_ws_unicode_300.jasb")
    lines = [
        "# Regresion: segmentacion con separador vacio = cortes Unicode (Zs/Zl/Zp + ASCII ws + FEFF).",
        "# Generado por _gen_tokenizar_L_seg_ws_unicode_300.py",
        "# Exito: CASOS=300 FALLIOS=0",
        "# Ejecutar: node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_seg_ws_unicode_300.jasb",
        "principal",
        " entero fallos = 0",
        " entero n_casos = 0",
    ]
    for i in range(300):
        s, modo, exp = build_case(i)
        lines.append(f" lista Lseg{i} = tokenizar_L(`{esc_bt(s)}`, \"\", {modo})")
        lines.append(f" si lista_tamano(Lseg{i}) != {exp} entonces")
        lines.append("  fallos = fallos + 1")
        lines.append(" fin_si")
        lines.append(" n_casos = n_casos + 1")
    lines.append(' imprimir "CASOS=" + str_desde_numero(n_casos) + " FALLIOS=" + str_desde_numero(fallos)')
    lines.append("fin_principal")
    text = "\n".join(lines) + "\n"
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("Wrote", out_path, "utf8_bytes", len(text.encode("utf-8")))


if __name__ == "__main__":
    main()
