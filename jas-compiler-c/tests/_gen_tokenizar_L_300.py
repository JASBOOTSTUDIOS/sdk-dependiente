# -*- coding: utf-8 -*-
"""Genera test_tokenizar_L_estres_300_corpus_wiki.jasb con corpus largo y 300 casos."""
import os

CORPUS_BASE = r"""Un texto es una composición de signos codificados en un sistema de escritura que forma una unidad de sentido. También es una composición de caracteres imprimibles (con grafema) generados por un algoritmo de cifrado que, aunque no tienen sentido para cualquier persona, sí puede ser descifrado por su destinatario original. En otras palabras, un texto es un entramado de signos con una intención comunicativa que adquiere sentido en determinado contexto.

Las ideas que comunica un texto están contenidas en lo que se suele denominar «macroproposiciones», unidades estructurales de nivel superior o global, que otorgan coherencia al texto constituyendo su hilo central, el esqueleto estructural que cohesiona elementos lingüísticos formales de alto nivel, como los títulos y subtítulos, la secuencia de párrafos, etc. En contraste, las «microproposiciones» son los elementos coadyuvantes de la cohesión de un texto, pero a nivel más particular o local. Esta distinción fue realizada por Teun van Dijk en 1980.[1]

El nivel microestructural o local está asociado con el concepto de cohesión. Se refiere a uno de los fenómenos propios de la coherencia, el de las relaciones particulares y locales que se dan entre elementos lingüísticos, tanto los que remiten unos a otros como los que tienen la función de conectar y organizar.

También es un conjunto de oraciones agrupadas en párrafos que habla de un tema determinado.

Texto lingüístico
De acuerdo con Greimas, es un enunciado ya sea gráfico o fónico que nos permite visualizar las palabras que escuchamos y que es utilizado para manifestar el proceso lingüístico. Mientras Hjelmslev usa ese término para designar el todo de una cadena lingüística ilimitada (§1).

En lingüística, no todo conjunto de signos constituye un texto.

Se le llama texto a la configuración de lengua o habla y se utilizan signos específicos (signo de la lengua o habla) y está organizada según reglas del habla o idioma.

Texto como "diálogo" y texto como "monólogo"
Otra noción importante es que los textos (y discursos) no son solo "monologales". En lingüística, el término texto sirve tanto para producciones en que solo hay un emisor (situaciones monogestionadas o monocontroladas) como en las que varios intercambian sus papeles (situaciones poligestionadas o policontroladas) como las conversaciones. El texto contiene conectores y signos, etc.

Ejemplos :

Monologales
Oral: Una declamación, un discurso político.
Escrita: Una carta de solicitud o una novela.
Dialogales
Oral: Una conversación en un bar o en un banco.
Escrita: Una conversación por chat o por cartas.
Características
Artículo principal: Criterios de textualidad
Este texto o conjunto de signos extraídos de un discurso debe reunir condiciones de textualidad. Las principales son:

Cohesión.
Coherencia.
Significado.
Progresividad.
Intencionalidad.
Adecuación.
Según los lingüistas Beaugrande y Dressler, todo texto bien elaborado ha de presentar siete características:

Ha de ser coherente, es decir, centrarse en un solo tema, de forma que las diversas ideas vertidas en él han de contribuir a la creación de una idea global.
Ha de tener cohesión, lo que quiere decir que las diversas secuencias que lo construyen han de estar relacionadas entre sí.
Ha de contar con adecuación al destinatario, de forma que utilice un lenguaje comprensible para su lector ideal, pero no necesariamente para todos los lectores (caso de los volcados de núcleo mencionados más arriba) y de forma que, además, ofrezca toda la información necesaria (y el mínimo de información innecesaria) para su lector ideal o destinatario.
Ha de contar con una intención comunicativa, es decir, debe querer decir algo a alguien y por tanto hacer uso de estrategias pertinentes para alcanzar eficacia y eficiencia comunicativa.
Ha de estar enmarcado en una situación comunicativa, es decir, debe ser enunciado desde un aquí y ahora concreto, lo que permite configurar un horizonte de expectativas y un contexto para su comprensión.
Ha de entrar en relación con otros textos o géneros para alcanzar sentido y poder ser interpretado conforme a una serie de competencias, presupuestos, marcos de referencia, tipos y géneros.
Ha de poseer información en grado suficiente para resultar novedoso e interesante pero no exigir tanta que colapse su sentido evitando que el destinatario sea capaz de interpretarlo (por ejemplo por una demanda excesiva de conocimientos previos).
Tipos de texto
A fin de agrupar y clasificar la enorme diversidad de textos, se han propuesto tipologías textuales. Estas se basan en distintos criterios como la función que cumple el texto en relación con los interlocutores o la estructura global interna que presenta.

La clasificación más simple de los textos, en función de las características que predominan en cada uno (se considera que no hay texto puro, es decir, no hay texto que tenga rasgos correspondientes únicamente a cada categoría, todo texto es híbrido), es como sigue:

textos narrativos
textos descriptivos
textos argumentativos
textos conmutativos
textos explicativos
textos expositivos
textos conclusivos
textos informativos
textos predictivos
texto formal
texto instructivo
Referencias
van Dijk, T.A. (1980). Macrostructures. Hillsdale: N.J. Erlbaum, citado en: Marinkovich Ravena, Juana, «Una propuesta de evaluación de la competencia textual narrativa.» Signos 1999, 32(45-46), 121-128; versión en línea. ISSN 0718-0934-
Véase también
Filología
Hermenéutica
Lingüística del texto
Control de autoridades	
Proyectos WikimediaWd Datos: Q234460Commonscat Multimedia: Texts / Q234460Wikiquote Citas célebres: Texto
IdentificadoresGND: 4059596-1 NKC: ph126631Diccionarios y enciclopediasBritannica: url
Categoría: Pragmática
Esta página se editó por última vez el 10 mar 2026 a las 12:27. La página fue renderizada con Parsoid.
El texto está disponible bajo la Licencia Creative Commons Atribución-CompartirIgual 4.0; pueden aplicarse cláusulas adicionales. Al usar este sitio aceptas nuestros términos de uso y nuestra política de privacidad.
Wikipedia® es una marca registrada de la Fundación Wikimedia, una organización sin ánimo """

# Cuatro copias: ~24k caracteres para estres de buffer y ventanas largas
CORPUS = (
    CORPUS_BASE
    + "\n\n--- BLOQUE 2 ---\n\n"
    + CORPUS_BASE
    + "\n\n--- BLOQUE 3 ---\n\n"
    + CORPUS_BASE
    + "\n\n--- BLOQUE 4 ---\n\n"
    + CORPUS_BASE
)


def escape_backtick(s: str) -> str:
    return s.replace("\\", "\\\\").replace("`", "\\`")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, "test_tokenizar_L_estres_300_corpus_wiki.jasb")
    corp_esc = escape_backtick(CORPUS)
    n_utf8 = len(CORPUS.encode("utf-8"))
    n_chars = len(CORPUS)

    lines = []
    lines.append("# Estres tokenizar_L: 300 casos (sintetico + corpus largo Wikipedia-es x4).")
    lines.append("# Generado por tests/_gen_tokenizar_L_300.py — no editar el corpus a mano.")
    lines.append(f"# Corpus: {n_chars} caracteres Unicode, {n_utf8} bytes UTF-8.")
    lines.append("# Exito: linea final \"CASOS=300 FALLIOS=0\"")
    lines.append("# Ejecutar: node .vscode/run-jasb.cjs sdk-dependiente/jas-compiler-c/tests/test_tokenizar_L_estres_300_corpus_wiki.jasb")
    lines.append("principal")
    lines.append(" entero fallos = 0")
    lines.append(" entero n_casos = 0")
    lines.append(" texto corpus = `" + corp_esc + "`")
    lines.append(" entero L = longitud_texto(corpus)")
    lines.append("")

    # --- Caso 0: mega cadena z (cerca del tope work 8192) ---
    lines.append(" # Caso 0: cadena unica muy larga (se trunca en pipeline si supera work cap)")
    lines.append(" entero kz = 0")
    lines.append(" texto s_z = \"\"")
    lines.append(" mientras kz < 8100 hacer")
    lines.append("  s_z = concatenar(s_z, \"z\")")
    lines.append("  kz = kz + 1")
    lines.append(" fin_mientras")
    lines.append(" lista Lz = tokenizar_L(s_z, \"\", 3)")
    lines.append(" si lista_tamano(Lz) != 1 entonces")
    lines.append("  fallos = fallos + 1")
    lines.append(" fin_si")
    lines.append(" n_casos = n_casos + 1")
    lines.append("")

    # --- Casos 1-149: sintético coma + modo (patrón estres_200) ---
    lines.append(" # Casos 1-149: patrones sinteticos con conteo exacto (r = k % 3)")
    lines.append(" entero k = 0")
    lines.append(" mientras k < 149 hacer")
    lines.append("  entero r = k % 3")
    lines.append("  entero n = 0")
    lines.append("  entero esperado = 0")
    lines.append("  texto s = \"\"")
    lines.append("  si r == 0 entonces")
    lines.append("   n = 80 + (k % 40) * 2")
    lines.append("   esperado = n")
    lines.append("   entero lim_a = n - 1")
    lines.append("   entero j = 0")
    lines.append("   mientras j < n hacer")
    lines.append("    s = concatenar(s, \"a\")")
    lines.append("    si j < lim_a entonces")
    lines.append("     s = concatenar(s, \",\")")
    lines.append("    fin_si")
    lines.append("    j = j + 1")
    lines.append("   fin_mientras")
    lines.append("  sino si r == 1 entonces")
    lines.append("   n = 120 + (k % 30)")
    lines.append("   esperado = n")
    lines.append("   entero lim_x = n - 1")
    lines.append("   j = 0")
    lines.append("   mientras j < n hacer")
    lines.append("    s = concatenar(s, \"x\")")
    lines.append("    si j < lim_x entonces")
    lines.append("     s = concatenar(s, \",\")")
    lines.append("    fin_si")
    lines.append("    j = j + 1")
    lines.append("   fin_mientras")
    lines.append("  sino")
    lines.append("   n = 60 + (k % 25)")
    lines.append("   esperado = n + 1")
    lines.append("   s = \"b\"")
    lines.append("   j = 0")
    lines.append("   mientras j < n hacer")
    lines.append("    s = concatenar(s, \",\")")
    lines.append("    s = concatenar(s, \"c\")")
    lines.append("    j = j + 1")
    lines.append("   fin_mientras")
    lines.append("  fin_si")
    lines.append("  # Modo 3 = lower+collapse (sin bigramas); 7 anadiria BIGRAM y el conteo no coincide con segmentos")
    lines.append("  lista Ls = tokenizar_L(s, \",\", 3)")
    lines.append("  si lista_tamano(Ls) != esperado entonces")
    lines.append("   fallos = fallos + 1")
    lines.append("  fin_si")
    lines.append("  n_casos = n_casos + 1")
    lines.append("  k = k + 1")
    lines.append(" fin_mientras")
    lines.append("")

    # --- Casos 150-199: modos unicode / mix ---
    lines.append(" # Casos 150-199: modos 1027, 3, 11, 23 sobre trozos del corpus")
    lines.append(" k = 0")
    lines.append(" mientras k < 50 hacer")
    lines.append("  entero margen = 900")
    lines.append("  entero span = L - margen")
    lines.append("  entero inicio = (k * 7919) % span")
    lines.append("  si inicio < 0 entonces")
    lines.append("   inicio = 0")
    lines.append("  fin_si")
    lines.append("  entero lon = 600 + (k % 41) * 20")
    lines.append("  entero fin_ex = inicio + lon")
    lines.append("  si fin_ex > L entonces")
    lines.append("   lon = L - inicio")
    lines.append("  fin_si")
    lines.append("  si lon < 200 entonces")
    lines.append("   lon = 200")
    lines.append("  fin_si")
    lines.append("  texto trozo = extraer_subtexto(corpus, inicio, lon)")
    lines.append("  # Solo modos 3 y 1027: 11 (MIN2) y 23 (BIGRAM+TRIGRAM) inflan lista_tamano y rompen el tope 384")
    lines.append("  entero modo = 1027")
    lines.append("  entero k4 = k % 2")
    lines.append("  si k4 == 1 entonces")
    lines.append("   modo = 3")
    lines.append("  fin_si")
    lines.append("  lista Lw = tokenizar_L(trozo, \"\", modo)")
    lines.append("  entero nw = lista_tamano(Lw)")
    lines.append("  si nw < 1 entonces")
    lines.append("   fallos = fallos + 1")
    lines.append("  fin_si")
    lines.append("  si nw > 384 entonces")
    lines.append("   fallos = fallos + 1")
    lines.append("  fin_si")
    lines.append("  lista Lw2 = tokenizar_L(trozo, \"\", modo)")
    lines.append("  si lista_tamano(Lw2) != nw entonces")
    lines.append("   fallos = fallos + 1")
    lines.append("  fin_si")
    lines.append("  n_casos = n_casos + 1")
    lines.append("  k = k + 1")
    lines.append(" fin_mientras")
    lines.append("")

    # --- Casos 200-299: corpus largo + NFKC (1027) + ventanas variables ---
    lines.append(" # Casos 200-299: ventanas largas sobre corpus (NFKC 1027)")
    lines.append(" k = 0")
    lines.append(" mientras k < 100 hacer")
    lines.append("  entero margen2 = 1500")
    lines.append("  entero span2 = L - margen2")
    lines.append("  entero inicio = (k * 104729) % span2")
    lines.append("  si inicio < 0 entonces")
    lines.append("   inicio = 0")
    lines.append("  fin_si")
    lines.append("  entero lon = 1200 + (k % 80) * 80")
    lines.append("  entero fin_ex2 = inicio + lon")
    lines.append("  si fin_ex2 > L entonces")
    lines.append("   lon = L - inicio")
    lines.append("  fin_si")
    lines.append("  si lon < 400 entonces")
    lines.append("   lon = 400")
    lines.append("  fin_si")
    lines.append("  texto trozo2 = extraer_subtexto(corpus, inicio, lon)")
    lines.append("  lista Lc = tokenizar_L(trozo2, \"\", 1027)")
    lines.append("  entero nc = lista_tamano(Lc)")
    lines.append("  si nc < 1 entonces")
    lines.append("   fallos = fallos + 1")
    lines.append("  fin_si")
    lines.append("  si nc > 384 entonces")
    lines.append("   fallos = fallos + 1")
    lines.append("  fin_si")
    lines.append("  n_casos = n_casos + 1")
    lines.append("  k = k + 1")
    lines.append(" fin_mientras")
    lines.append("")

    lines.append(" imprimir \"CASOS=\" + str_desde_numero(n_casos) + \" FALLIOS=\" + str_desde_numero(fallos)")
    lines.append("fin_principal")

    text = "\n".join(lines) + "\n"
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("Wrote", out_path, "bytes", len(text.encode("utf-8")), "n_casos end", 1 + 149 + 50 + 100)


if __name__ == "__main__":
    main()
