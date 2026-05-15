# -*- coding: utf-8 -*-
"""Genera test_tokenizar_L_paso2_completo_300.jasb: paso 2 (P*, So, Sm, Sk, coma lista, grafema) + contracciones S3."""
import os

NBSP = "\u00a0"
EMOJI = "\U0001F600"  # So

MODO_BASE = 394243
MODO_COMMA_LIST = 4588547  # MODO_BASE | 4194304
MODO_SM = 1442819  # MODO_BASE | 1048576
MODO_GRAPHEME = 8520707  # 1027 | 131072 | 8388608
MODO_CONTRACT = 515  # 512 | 2 | 1


def esc_bt(s: str) -> str:
    return s.replace("\\", "\\\\").replace("`", "\\`")


def build_case(i: int) -> tuple[str, int, int]:
    """(literal_utf8, modo, lista_tamano_esperado)."""
    r = i % 17
    q = i // 17

    if r == 0:
        n = 3 + (q % 6)
        s = ",".join(["tok" + str((j + q) % 10) for j in range(n)])
        return s, MODO_BASE, n
    if r == 1:
        return "3.14", MODO_BASE, 1
    if r == 2:
        return "12,34", MODO_BASE, 1
    if r == 3:
        return "a" + EMOJI + "b", 918531, 2
    if r == 4:
        return "a" + EMOJI + "b", MODO_BASE, 1
    if r == 5:
        return "hello" + EMOJI + "world", 918531, 2
    if r == 6:
        s = "w" + NBSP + ",x" + str(q % 3)
        return s, MODO_BASE, 2
    if r == 7:
        return "a@mail", MODO_BASE, 1
    if r == 8:
        return "a@1", MODO_BASE, 2
    if r == 9:
        return "x,y", 657923, 1
    if r == 10:
        return "hola!!mundo" + str(q % 4), MODO_BASE, 2
    if r == 11:
        return "1,2", MODO_COMMA_LIST, 2
    if r == 12:
        return "3,14", MODO_COMMA_LIST, 2
    if r == 13:
        return "a+b", MODO_SM, 2
    if r == 14:
        return "hola mundo", MODO_GRAPHEME, 2
    if r == 15:
        return "de\tel", MODO_CONTRACT, 1
    # r == 16
    return "1,2", MODO_BASE, 1


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, "test_tokenizar_L_paso2_completo_300.jasb")
    lines = [
        "# Regresion: paso 2 (P*, So, Sm, opcional Sk; coma lista ASCII; grafema) + S3 contracciones.",
        "# Generado por _gen_tokenizar_L_paso2_300.py",
        "# Exito: CASOS=300 FALLIOS=0",
        "# Ejecutar: node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_paso2_completo_300.jasb",
        "principal",
        " entero fallos = 0",
        " entero n_casos = 0",
    ]
    for i in range(300):
        s, modo, exp = build_case(i)
        lines.append(f" lista Lp2_{i} = tokenizar_L(`{esc_bt(s)}`, \"\", {modo})")
        lines.append(f" si lista_tamano(Lp2_{i}) != {exp} entonces")
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
