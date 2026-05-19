# Referencia Completa del Lenguaje Jasboot

Documento de sintaxis, funcionalidades y semántica del lenguaje de programación Jasboot. Paradigma 100% español.

---

## Índice

1. [Introducción](#1-introducción)
2. [Estructura del programa](#2-estructura-del-programa)
3. [Tipos de datos](#3-tipos-de-datos)
4. [Variables y declaraciones](#4-variables-y-declaraciones)
5. [Literales](#5-literales)
6. [Operadores](#6-operadores)
7. [Sentencias de control](#7-sentencias-de-control)
8. [Manejo de excepciones](#8-manejo-de-excepciones)
9. [Bucles y repetición](#9-bucles-y-repetición)
10. [Funciones](#10-funciones)
11. [Registros (estructuras)](#11-registros-estructuras)
12. [Clases y Herencia](#12-clases-y-herencia)
13. [Entrada y salida](#13-entrada-y-salida)
14. [Memoria neuronal (JMN)](#14-memoria-neuronal-jmn)
15. [Llamadas de sistema](#15-llamadas-de-sistema)
16. [Módulos y bibliotecas](#16-módulos-y-bibliotecas)
17. [Gestor de Paquetes (jbc)](#17-gestor-de-paquetes-jbc)

---

## 1. Introducción

Jasboot es un lenguaje de programación que sigue el paradigma de pensamiento humano: usa **conceptos** en lugar de variables, **cuando** en lugar de if, **mientras** en lugar de while, etc. La indentación define bloques; no se usan llaves.

### Palabras prohibidas (inglés)

El lexer rechaza: `if`, `else`, `for`, `while`, `function`, `return`, `true`, `false`, `null`, `var`, `let`, `const`, `class`, `import`, `export`. Use los equivalentes en español.

### Implementación de referencia (compilador jbc)

La **fuente de verdad** práctica del lenguaje que compila a `.jbo` es el compilador C **jbc** en `sdk-dependiente/jas-compiler-c/`:

- **Palabras clave y tokens:** `src/keywords.c`
- **Nombres aceptados como llamada predefinida** `nombre(...)` (además de funciones usuario): `src/sistema_llamadas.c`
- **Qué genera bytecode:** `src/codegen.c` (función `visit_call_sistema`): _no_ todo lo listado en `sistema_llamadas.c` tiene necesariamente codegen; si algo no compila o no hace nada útil, busque el nombre ahí.

---

## 2. Estructura del programa

Un programa Jasboot tiene tres secciones principales, en cualquier orden:

- **Registros** (`registro` … `fin_registro`)
- **Clases** (`clase` … `fin_clase`)
- **Variables globales** (declaraciones en el nivel raíz)
- **Funciones** (`funcion` … `fin_funcion`)
- **Bloque principal** (`principal` … `fin_principal`)
- **Exportación** (`enviar {A, B, C}`)

```jasb
clase Persona
    texto nombre
    entero edad

    funcion inicializar(texto n, entero e)
        este.nombre = n
        este.edad = e
    fin_funcion
fin_clase

enviar {Persona} # Exporta la clase para ser usada por otros archivos

texto saludo = "Hola"

funcion saludar(Persona p) retorna
    imprimir saludo + ", " + p.nombre
fin_funcion

principal
    Persona juan = Persona("Juan", 30)
    saludar(juan)
fin_principal
```

---

## 3. Tipos de datos

| Tipo         | Descripción                                          | Ejemplo            |
| ------------ | ---------------------------------------------------- | ------------------ |
| `entero`     | Número entero 64 bits con signo                      | `42`               |
| `u32`        | Entero 32 bits sin signo                             | `0x80000000`       |
| `u64`        | Entero 64 bits sin signo                             | `0xFFFFFFFF`       |
| `flotante`   | Número decimal                                       | `3.14`             |
| `texto`      | Cadena de caracteres                                 | `"Hola"`           |
| `caracter`   | Un carácter ASCII (0–255)                            | `'A'`              |
| `bool`       | Booleano (verdadero/falso)                           | `verdadero`        |
| `lista`      | Colección ordenada                                   | `[1, 2, 3]`        |
| `lista<T>`   | Colección tipada (T=entero, registro...)             | `lista<Persona> v` |
| `mapa`       | Diccionario clave-valor (asociativo)                 | `{"a": 1}`         |
| `mapa<T>`    | Mapa con valores de tipo T                           | `mapa<Persona> m`  |
| `vec2`       | Dos flotantes (`x`, `y`)                             | `vec2 v`           |
| `vec3`       | Tres flotantes (`x`, `y`, `z`)                       | `vec3 v`           |
| `vec4`       | Cuatro flotantes (`x`, `y`, `z`, `w`)                | `vec4 v`           |
| `mat4`       | Matriz 4×4 en flotante (128 bytes, row-major)        | `mat4 m`           |
| `mat3`       | Matriz 3×3 en flotante (72 bytes)                    | `mat3 m`           |
| `concepto`   | ID de concepto en memoria neuronal                   | `'hola'`           |
| `nulo`       | Valor nulo (ausencia de valor)                       | `nulo`             |
| `indefinido` | Valor no definido (variable sin asignar)             | `indefinido`       |
| `NeN`        | No es un Número (resultado de operaciones inválidas) | `NeN`              |

### Conversión de Tipos

Jasboot es un lenguaje de tipado fuerte, pero permite ciertas conversiones automáticas para facilitar la legibilidad:

- **Numérica:** `entero` <-> `flotante` (implícita en ambos sentidos).
- **A Texto:** `entero` -> `texto` y `flotante` -> `texto` (implícita en asignación y concatenación).
- **Explícita:** Para otros casos, use `convertir_entero()`, `convertir_flotante()` o `str_desde_numero()`.

### Interpolación de Cadenas

Las variables y expresiones pueden ser insertadas directamente dentro de una cadena de texto usando la sintaxis `${...}`.

```jasb
entero edad = 25
texto msj = "Tengo ${edad} años"  # "Tengo 25 años"
imprimir "Suma: ${5 + 3}"         # "Suma: 8"
```

---

## 4. Variables y declaraciones

### Declaración con tipo

```
tipo nombre [= expresión]
```

```jasb
entero x = 10
flotante pi = 3.14159
texto mensaje = "Hola"
lista<entero> numeros = [1, 2, 3]
mapa<Persona> agenda = {}  # Mapa donde los valores son de tipo Persona
```

### Acceso a Colecciones

Las listas y mapas soportan acceso mediante corchetes o punto (para mapas):

```jasb
# Listas
elemento primer = numeros[0]

# Mapas (dos formas equivalentes)
texto n1 = mi_mapa["nombre"]
texto n2 = mi_mapa.nombre    # Acceso directo por clave como si fuera un campo
```

### Constantes

```
constante tipo nombre = expresión
```

Las constantes son variables inmutables. El valor es obligatorio en la declaración y no puede reasignarse después.

```jasb
constante entero PI = 3
constante texto SALUDO = "Hola"
constante flotante E = 2.718
```

Tipos permitidos por **jbc** en `constante`: `entero`, `texto`, `flotante`, `caracter`, `bool`, `u32`, `u64`, `u8`, `byte`, `vec2`, `vec3`, `vec4`, `mat4`, `mat3`. Intentar asignar a una constante produce error de compilación.

### Asignación

```
nombre = expresión
objeto[indice] = expresión   # lista o mapa
objeto.miembro = expresión   # registro
```

```jasb
x = x + 1
lista[0] = 100
persona.nombre = "Ana"
```

No se puede asignar a constantes.

### Conversión Automática en Asignación

Desde la versión 1.1, el compilador permite asignar valores numéricos directamente a variables de tipo `texto`. El sistema realiza la conversión a cadena automáticamente:

```jasb
texto ts = obtener_timestamp()  # El entero se convierte a texto automáticamente
texto pi_str = 3.14             # El flotante se convierte a "3.14"
```

**Internamente:** `VarDeclNode` → `OP_ESCRIBIR` (o `OP_LEER` para lectura). Los offsets se calculan en `symbol_table.c` y `resolve.c`. Las constantes tienen `is_const` en la tabla de símbolos. Si el destino es `texto` y el origen es numérico, se emite `OP_STR_DESDE_NUMERO`.

---

## 5. Literales

### Números

```jasb
42          # entero
0xFF        # hexadecimal (unsigned)
0x80000000  # hex sin signo (para u32/u64)
3.14        # flotante
.5          # flotante 0.5
```

### Cadenas

```jasb
"Hola mundo"
"Línea 1\nLínea 2"    # \n nueva línea
"Tab:\tcolumna"       # \t tabulador
"Comillas: \"abc\""   # \"
"Barra: \\"           # \\
```

### Carácter

```jasb
'A'                   # Un solo carácter entre comillas simples
'!'                   # Literal caracter (código ASCII)
```

Un literal con **una sola** letra entre comillas simples es de tipo `caracter` y almacena su código ASCII. Cadenas de varios caracteres (`'hola'`) se tratan como `concepto` (memoria neuronal).

### Conceptos (memoria neuronal)

```jasb
'nombre_concepto'     # ID de concepto, sintaxis con comillas simples (varios caracteres)
```

### Listas

```jasb
[1, 2, 3]
["a", "b", "c"]
[]                   # lista vacía
[1, "texto", 3.14]   # tipos mixtos
```

**Internamente:** `ListLiteralNode` → `OP_MEM_LISTA_CREAR` + `OP_MEM_LISTA_AGREGAR` en bucle.

### Mapas

```jasb
{"clave": "valor"}
{nombre: "Jas", edad: 10}  # Las claves pueden ser identificadores sin comillas
{}                         # mapa vacío
```

**Internamente:** `MapLiteralNode` → `OP_MEM_MAPA_CREAR` + `OP_MEM_MAPA_PONER` en bucle.

### Booleanos

```jasb
verdadero
falso
```

### Valores especiales

```jasb
nulo           # Valor nulo (ausencia de valor)
indefinido     # Valor no definido
NeN            # No es un Número (NaN), resultado de operaciones inválidas con flotantes
```

---

## 6. Operadores

### Aritméticos

| Operador | Descripción    | Ejemplo |
| -------- | -------------- | ------- |
| `+`      | Suma           | `a + b` |
| `-`      | Resta          | `a - b` |
| `*`      | Multiplicación | `a * b` |
| `/`      | División       | `a / b` |
| `%`      | Módulo         | `a % b` |

**Internamente:** `OP_SUMAR`, `OP_RESTAR`, `OP_MULTIPLICAR`, `OP_DIVIDIR`, `OP_MODULO`. Para flotantes: `OP_*_FLT`.

### Comparación

| Operador | Descripción   | Alternativa en español  |
| -------- | ------------- | ----------------------- |
| `==`     | Igual         | `es igual a`            |
| `!=`     | Distinto      | -                       |
| `<`      | Menor         | `menor que` / `menor a` |
| `>`      | Mayor         | `mayor que` / `mayor a` |
| `<=`     | Menor o igual | -                       |
| `>=`     | Mayor o igual | -                       |

```jasb
x == 10
edad mayor que 18
nombre es igual a "admin"
```

### Interpolación de cadenas

Jasboot permite interpolar expresiones complejas dentro de cadenas usando `${}`. Esto funciona con variables, accesos a miembros, índices de listas y llamadas a funciones.

```jasb
registro Punto
    x: entero
    y: entero
fin_registro

Punto p = {x: 10, y: 20}
lista<Punto> trayectoria = [p]

imprimir "El punto es (${p.x}, ${p.y})"
imprimir "Primer punto de la lista: ${trayectoria[0].x}"
```

### Lógicos

| Operador | Descripción       |
| -------- | ----------------- |
| `y`      | AND lógico        |
| `o`      | OR lógico         |
| `no`     | NOT (también `!`) |

```jasb
activo y conectado
edad >= 18 o es_admin
no terminado
```

### Bits

| Operador | Descripción              |
| -------- | ------------------------ |
| `<<`     | Desplazamiento izquierda |
| `>>`     | Desplazamiento derecha   |

**Internamente:** `OP_BIT_SHL`, `OP_BIT_SHR`.

### Flotantes: funciones predefinidas (un argumento o dos)

En expresiones se pueden llamar (radianes en trigonometría):

| Función                               | Descripción                 |
| ------------------------------------- | --------------------------- |
| `sin(x)`, `cos(x)`, `tan(x)`          | Trigonometría               |
| `atan2(y, x)` o `arcotangente2(y, x)` | Arcotangente dos argumentos |
| `exp(x)`, `log(x)`, `log10(x)`        | Exponencial y logaritmos    |

No hay en fuente un `raiz(x)` genérico; la raíz cuadrada de flotantes se usa internamente en opcodes (p. ej. longitud de vectores).

### Ternario

```
condición ? valor_si_verdadero : valor_si_falso
```

```jasb
edad >= 18 ? "adulto" : "menor"
```

**Internamente:** `TernaryNode` → emisión de condición + `OP_SI` + ramas.

### Precedencia (mayor a menor)

1. Paréntesis `()`
2. Unario: `-`, `no`
3. Multiplicativos: `*`, `/`, `%`, `<<`, `>>`
4. Aditivos: `+`, `-`
5. Comparación: `==`, `!=`, `<`, `>`, `<=`, `>=`
6. Lógico AND: `y`
7. Lógico OR: `o`
8. Ternario: `?:`

---

## 7. Sentencias de control

### Condicional: cuando / si

```jasb
cuando condición entonces hacer
    # bloque si verdadero
sino
    # bloque si falso
fin_cuando
```

Alias: `si` / `fin_si`. El `entonces` y `hacer` son opcionales en algunas variantes.

```jasb
si x > 0 hacer
    imprimir "Positivo"
sino si x < 0 hacer
    imprimir "Negativo"
sino
    imprimir "Cero"
fin_si
```

**Internamente:** `IfNode` → evaluación de condición, `OP_SI`, saltos condicionales, `OP_IR`.

### Selección: seleccionar

Permite ejecutar diferentes bloques de código según el valor de una expresión.

```jasb
seleccionar expresion hacer
    caso valor1
        # bloque si expresion == valor1
    caso valor2
        # bloque si expresion == valor2
    defecto
        # bloque si no coincide ningún caso
fin_seleccionar
```

**Internamente:** `SwitchNode` → emite comparación por cada caso + saltos.

---

## 8. Manejo de excepciones

Jasboot permite capturar errores de ejecución (como división por cero o clave inexistente en mapa) mediante bloques de control de excepciones.

### intentar / atrapar / final

```jasb
intentar
    # Código que puede fallar
    entero x = 10 / 0
atrapar mensaje
    # Se ejecuta si ocurre un error (el mensaje se guarda en la variable 'mensaje')
    imprimir "Error capturado: " + mensaje
final
    # Se ejecuta siempre al final, haya error o no (opcional)
    imprimir "Limpieza final"
fin_intentar
```

### lanzar

Permite generar un error personalizado manualmente.

```jasb
si saldo < monto entonces
    lanzar "Saldo insuficiente"
fin_si
```

---

## 9. Bucles y repetición

### Bucle: mientras

```jasb
mientras condición hacer
    # cuerpo del bucle
fin_mientras
```

### Bucle: para

El bucle `para` permite iterar con un contador de forma clásica.

```jasb
para i = 0 hasta 10 hacer
    imprimir i
fin_para

para i = 10 hasta 0 paso -1 hacer
    imprimir i
fin_para
```

### Bucle: `para_cada` / `para cada`

Itera sobre **listas** (y el mismo mecanismo IR que usa el compilador para listas en memoria). Sintaxis admitida:

```text
para_cada <tipo> <variable> [ indice <nombre_entero> ] sobre|en <expresión> hacer
    …
fin_para_cada
```

- **`para_cada`** y la variante con espacio **`para cada`** son equivalentes.
- **`<tipo>`** de elemento: `entero`, `flotante`, `texto`, `bool`, o **`caracter`** (ver más abajo).
- Tras el nombre de variable se puede declarar un contador entero con **`indice nombre`** (índice `0` … `n-1` en cada vuelta).
- La colección va tras **`sobre`** o tras el identificador **`en`** (sin comillas), como sinónimo de `sobre`.

**Lista**

```jasb
lista<entero> numeros = [10, 20, 30]
para_cada entero n indice i sobre numeros hacer
    imprimir "Indice: " + str_desde_numero(i) + ", Valor: " + str_desde_numero(n)
fin_para_cada
```

**Texto carácter a carácter** (`caracter`: cada iteración expone un **`texto`** de longitud 0 o 1)

```jasb
texto s = "Ab!"
para_cada caracter c sobre s hacer
    imprimir c
fin_para_cada
```

> **Mapas:** si necesitas recorrer un `mapa`, convierte o recorre las claves/valores con las APIs de mapa o una lista intermedia; la forma exacta depende del módulo y del compilador en uso.

### Romper y continuar

```jasb
mientras verdadero hacer
    cuando encontrado hacer
        romper
    fin_cuando
    cuando omitir hacer
        continuar
    fin_cuando
fin_mientras
```

- **romper:** Sale del bucle más interno.
- **continuar:** Salta al inicio de la siguiente iteración.

**Internamente:** `OP_IR` a la etiqueta de salida (romper) o de inicio (continuar).

---

## 10. Funciones

### Definición

```jasb
funcion nombre(tipo1 param1, tipo2 param2) retorna [tipo]
    # cuerpo
    retornar expresión
fin_funcion
```

```jasb
funcion sumar(entero a, entero b) retorna entero
    retornar a + b
fin_funcion

funcion saludar(texto nombre) retorna
    imprimir "Hola, " + nombre
fin_funcion
```

### Llamada

```jasb
variable = sumar(10, 20)
saludar("Mundo")
```

**Internamente:** `FunctionNode` → etiqueta, parámetros en offsets locales. Llamada: `OP_LLAMAR` + parche de salto. Retorno: `OP_RETORNAR` + `OP_MOVER` resultado a R1.

---

## 11. Registros (estructuras)

Los registros son agrupaciones simples de datos. Soportan dos tipos de sintaxis para definir campos:

```jasb
# Sintaxis 1: nombre: tipo (Estilo moderno)
registro Persona
    nombre: texto
    edad: entero
fin_registro

# Sintaxis 2: tipo nombre (Estilo clásico)
registro Coordenada
    flotante x
    flotante y
fin_registro
```

Los registros pueden definirse tanto a nivel global como dentro de funciones (locales).

```jasb
principal
    registro Punto
        x: entero
        y: entero
    fin_registro

    Punto p
    p.x = 10
    p.y = 20
    imprimir "Punto: ${p.x}, ${p.y}"
fin_principal
```

---

## 12. Clases y Herencia

Jasboot soporta un sistema completo de clases con herencia, polimorfismo y visibilidad.

### Definición y Herencia

Se usa `clase` y `extiende`.

```jasb
clase Animal
    texto nombre

    funcion inicializar(texto n)
        este.nombre = n
    fin_funcion

    funcion hablar()
        imprimir este.nombre + " hace un sonido"
    fin_funcion
fin_clase

clase Perro extiende Animal
    entero edad_perruna

    # Sobrescritura de constructor
    funcion inicializar(texto n, entero e)
        padre.inicializar(n)  # Llamada al constructor base
        este.edad_perruna = e
    fin_funcion

    # Sobrescritura de método (Polimorfismo)
    funcion hablar()
        imprimir este.nombre + " dice: ¡Guao!"
        imprimir "--- Detalle Base ---"
        padre.hablar()       # Llamada al método de la clase base
    fin_funcion
fin_clase
```

### Palabras Clave Especiales

- **`este`**: Referencia a la instancia actual del objeto (como `this` o `self`).
- **`padre`**: Referencia a la clase base inmediata para invocar métodos o constructores originales (como `super`).
- **`inicializar`**: Nombre reservado para el método constructor que se invoca automáticamente al instanciar.

### Visibilidad

Se puede usar el modificador `privado` para campos y métodos, restringiendo su acceso solo a métodos de la propia clase.

```jasb
clase CuentaBancaria
    privado flotante saldo

    funcion inicializar(flotante inicial)
        este.saldo = inicial
    fin_funcion

    funcion obtener_saldo() retorna flotante
        retornar este.saldo
    fin_funcion
fin_clase
```

### Polimorfismo (Despacho Dinámico)

El compilador genera dispatch dinámico. Si una variable de tipo clase base contiene una instancia de una clase derivada, al llamar a un método se ejecutará la versión de la clase derivada.

---

## 13. Entrada y salida

### imprimir / imprimir_sin_salto

Imprime expresiones en la consola estándar.

**Conversión Implícita:**
Soporta conversión automática de cualquier tipo numérico a `texto` durante la impresión o concatenación con el operador `+`.

```jasb
entero edad = 25
imprimir "Tengo " + edad + " años"  # Imprime: Tengo 25 años
```

### ingresar_texto / ingreso_inmediato

Permite capturar entrada del usuario. `ingresar_texto()` espera a que el usuario presione Enter, mientras que `ingreso_inmediato()` (alias `percibir_teclado()`) captura la primera tecla presionada.

---

## 14. Memoria neuronal (JMN)

Jasboot integra de forma nativa la **JMN (Red de Memoria Jasboot)**, permitiendo almacenar y recuperar información de forma asociativa y persistente.

### Operaciones Básicas

| Comando                               | Descripción                                                                                                                                    |
| ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `crear_memoria(ruta)`                 | Crea o abre un archivo de memoria neuronal `.jmn`.                                                                                             |
| `cerrar_memoria()`                    | Cierra la memoria actual y guarda los cambios.                                                                                                 |
| `recordar clave con valor v`          | Almacena un valor asociado a una clave en la red.                                                                                              |
| `buscar clave`                        | Recupera el valor asociado a la clave (se almacena en la palabra reservada `resultado`).                                                       |
| `asociar_relacion(c1, c2, t, p)`      | Crea un vínculo con tipo `t` (1,2,3) y peso `p` (0.0-1.0).                                                                                     |
| `buscar_peso(c1, c2)`                 | Obtiene la fuerza de conexión (0.0-1.0) entre dos conceptos.                                                                                   |
| `buscar_asociados_lista(c, K)`        | Retorna lista de hasta `K` asociados ordenados por relevancia/peso.                                                                            |
| `vecinos_jmn(c, K, tipo)`             | **Alias nativo** de `buscar_asociados_lista` (mismo bytecode); nombre alineado con Fase 0 del grafo. Opcional tercer argumento `tipo` (1..30). |
| `conexiones_salientes_de(c, K, tipo)` | Alias de `asociados_lista_de` / `buscar_asociados_lista`.                                                                                      |
| `buscar_asociados_rango(L, m, M)`     | Búsqueda masiva para lista `L` con pesos entre `m` y `M`. Retorna `mapa`.                                                                      |

### **g(τ)** — pasos 1, 2 y 3 (alineado con [`flujo_model_IA/03_g_tau.md`](../../flujo_model_IA/03_g_tau.md))

El documento **`03_g_tau.md`** describe el vector **`g[1..30]`**, la máscara **`mask(C,τ)`** y la contribución **`w·g(τ)·h(d)·mask`**. En Jasboot eso se materializa así:

| Paso                         | Rol en el doc                                                                | En Jasboot                                                                                                                                                                                                                                                   |
| ---------------------------- | ---------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **1. CARGAR**                | Plantilla **`G_default`** + perfil **`P`** (y overrides por dominio)         | **`cargar_perfil_g("defecto" \| "analitico" \| "explorador" \| "secuencial")`**; ajustes puntuales con **`configurar_peso_g`** / **`configurar_pesos_g`**; merge adicional vía **`JASBOOT_PROPAGAR_G`** / **`JASBOOT_PROPAGAR_MASK`** (ver **`AGENTS.md`**). |
| **2. NORMALIZAR** (opcional) | Acotar suma o máximo de **`g`**                                              | **`normalizar_pesos_g()`** (modo max o suma según **`JASBOOT_PROPAGAR_G_NORM`**).                                                                                                                                                                            |
| **3. EN USO**                | Por cada arista: **`factor ← g[τ]·mask`**, acumular con **`w`** y **`h(d)`** | Automático dentro de **`propagar_activacion`**, **`propagar_activacion_de`**, **`propagar_activacion_semillas`** y **`propagar_activacion_mai`**: la VM pasa la tabla actual (**`g_tau`**, **`mask_tau`**) a JMN en cada expansión.                          |

**Ejemplo de app:** Neurixis aplica el pipeline tras **`crear_memoria`** y antes de cada **`activar_propagacion_contextual`** (`apps/neurixis/modulos/generador.jasb`, función **`neurixis_pipeline_g_tau_ara`**).

### Propagación, **g(τ)** y máscaras por tipo

En la propagación de activación, cada arista de tipo **`τ`** escala la contribución por **`g[τ]·mask[τ]`** (peso global por tipo y máscara **0..1**), además del peso **`w`** almacenado en la arista y del factor **`h(d)`** (ver **`docs/LENGUAJE/jmn/PROPAGACION_D_MAX_K_AUDITORIA_Y_ESTRES.md`** y **`AGENTS.md`**).

| Función                                                                  | Descripción                                                                                                           |
| ------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| `propagar_activacion(origen [, d_max [, K [, flags]]])`                  | Ganador por activación (byte bajo de `tipo_rel` en el pack IR; **0** = todos los tipos admitidos en la expansión).    |
| `propagar_activacion_de(origen, tipo_literal [, d_max [, K [, flags]]])` | Igual con **`tipo`** fijo en compilación (literal entero).                                                            |
| `propagar_activacion_semillas(lista, d_max, K [, flags])`                | Semillas múltiples (**`lista`** de IDs).                                                                              |
| `propagar_activacion_mai(...)`                                           | Rama máscara MAI (mismo opcode VM con flag **`RELATIVE`**); ver **`docs/LENGUAJE/CAPAS_JMN_MAI_VM_Y_USO.md`**.        |
| `configurar_peso_g(tau, peso)`                                           | Asigna **`g[tau]`** (**`tau`** 1..30).                                                                                |
| `configurar_pesos_g(lista)`                                              | Lista flotante: entrada **`i`** → **`g[i+1]`**.                                                                       |
| `normalizar_pesos_g()`                                                   | Normaliza **`g`** (max o suma según **`JASBOOT_PROPAGAR_G_NORM`**).                                                   |
| `cargar_perfil_g(nombre)`                                                | Perfiles admitidos: **`defecto`**, **`analitico`**, **`explorador`**, **`secuencial`** (y alias documentados en JMN). |
| `configurar_mascara_g(tau, valor)`                                       | **`mask[tau]`** en **0..1**.                                                                                          |
| `configurar_mascaras_g(lista)`                                           | Lista flotante → **`mask[i+1]`**.                                                                                     |

Variables de entorno relacionadas: **`JASBOOT_PROPAGAR_G`**, **`JASBOOT_PROPAGAR_MASK`**, **`JASBOOT_PROPAGAR_G_NORM`**, **`JASBOOT_PROPAGAR_H_*`**, etc. (lista completa en **`AGENTS.md`**).

Modelo conceptual **`g` / mask / h / fusión φ**: carpeta **`flujo_model_IA/`** e índice **`flujo_model_IA/INDICE_REFERENCIA_FLUJO_MODELO_IA.md`**.

**Importante:** los bits **`IR_INST_FLAG_*`** deben coincidir entre **`jas-compiler-c/include/opcodes.h`** y **`jasboot-ir/src/ir_format.h`**; si no, la propagación puede interpretarse como MAI y fallar en tiempo de ejecución hasta recompilar los `.jasb`.

---

## Flujo Model IA — Qué está implementado nativo (SDK)

Esta sección resume, para el SDK (`sdk-dependiente/`), qué partes del flujo descrito en `flujo_model_IA/` están soportadas **a nivel nativo** (JMN/VM/runtime) y qué partes se implementan en **capa aplicación** (Jasboot).

> Referencia detallada en el SDK: `docs/FLUJO_MODELO_IA_IMPLEMENTADO.md`.

| Fase | Archivo `flujo_model_IA/`          | Estado en SDK               | Dónde vive (alto nivel)                                                                           |
| ---- | ---------------------------------- | --------------------------- | ------------------------------------------------------------------------------------------------- |
| 01   | `01_tokenizacion_L.md`             | Nativo                      | VM/runtime (builtins `tokenizar_L` / `claves_L`)                                                  |
| 02   | `02_d_max.md`                      | Nativo                      | JMN (`jmn_propagar_activacion_semillas`)                                                          |
| 03   | `03_g_tau.md`                      | Nativo                      | JMN (`g_tau[1..30]`) + VM (builtins `cargar_perfil_g`, `configurar_peso_g`, `normalizar_pesos_g`) |
| 04   | `04_h_d.md`                        | Nativo                      | JMN (`h_mode`, `h_lambda`, `h_kappa`) + VM (builtins `configurar_h_*`)                            |
| 05   | `05_mask_C_tau.md`                 | Nativo                      | JMN (`mask_tau[1..30]`) + VM (builtins `configurar_mascara_g`)                                    |
| 06   | `06_alpha_phi.md`                  | Nativo                      | JMN (`E[v,τ]`, `alpha_tau[1..30]`, fusión `φ` suma/max)                                           |
| 07   | `07_politica_tau_10.md`            | Nativo (bloqueo por umbral) | JMN (umbral de rechazo τ=10 en `JMNPropagarExtra`)                                                |
| 08   | `08_ranking_y_diversidad_MMR.md`   | Nativo (si se activa)       | JMN (post-proceso MMR + similitud coseno)                                                         |
| 09   | `09_d_max_dinamico.md`             | Parcial                     | Hay campos `dmax_*` en `JMNPropagarExtra`, falta cálculo automático genérico en el núcleo         |
| 10   | `10_inferencia_simbolica_MIL.md`   | App                         | Lógica MIL en Jasboot (no hay motor genérico nativo)                                              |
| 11   | `11_metacognicion_y_simulacion.md` | App                         | Crítico/replanificación en Jasboot (orquestación de producto)                                     |
| 12   | `12_memoria_episodica_temporal.md` | App                         | Episodios/temporalidad como grafo + mantenimiento en Jasboot                                      |

### Journal (`.jwl`), recuperación y contexto MAI (Fase 2.2)

Junto al archivo `ruta.jmn`, el runtime puede mantener **`ruta.jmn.jwl`** (journal append-only) para recuperación ante cortes o `.jmn` corrupto. Variables `JASBOOT_JWL_*` y flujo de replay: ver **[jmn/JMN_JOURNAL_Y_CONSOLIDACION.md](jmn/JMN_JOURNAL_Y_CONSOLIDACION.md)** y `AGENTS.md`.

| Llamada                              | Descripción                                                                                                                  |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| `mai_contexto_recientes()`           | Lista (hasta 10 IDs) del foco reciente MAI, más reciente primero.                                                            |
| `mai_contexto_recientes(n)`          | Igual con tope `n` entero **1..10** (literal o expresión).                                                                   |
| `mai_contexto()` / `mai_contexto(n)` | Alias de `mai_contexto_recientes`.                                                                                           |
| `consolidar_sueno()`                 | Igual que `consolidar_memoria` / `dormir` / `consolidar`: mantenimiento del grafo (decae, olvido de débiles, consolidación). |

### Búsqueda Introspectiva en JMN

> **Nuevo en 2024**: Sistema modular de búsqueda en toda la memoria neuronal.

La **búsqueda introspectiva** permite buscar texto dentro de las claves y valores de todos los conceptos almacenados en JMN. Ofrece 4 niveles de funcionalidad según tus necesidades.

#### Funciones de Búsqueda (Estado de Implementación)

| Función                                         |      Estado       | Descripción                                               |
| ----------------------------------------------- | :---------------: | --------------------------------------------------------- |
| `buscar_en_memoria(termino)`                    |  ✅ **FUNCIONA**  | Búsqueda básica, primer resultado, case-insensitive       |
| `buscar_en_memoria_cs(termino, cs)`             |  ✅ **FUNCIONA**  | Con control de mayúsculas (0=insensitive, 1=sensitive)    |
| `buscar_en_memoria_lista(termino, max)`         | ⏳ Implementada\* | Lista de IDs (requiere corrección de listas JMN)          |
| `buscar_en_memoria_detallada(termino, max, cs)` | ⏳ Implementada\* | Con metadata completa (requiere corrección de listas JMN) |

> **Nota**: Las funciones marcadas con \* están completamente implementadas y encuentran resultados correctamente, pero dependen del sistema de listas JMN que tiene un bug pendiente de corrección. Las funciones básicas (sin listas) funcionan perfectamente.

#### 1. `buscar_en_memoria(termino)` - Búsqueda Básica ✅

Busca texto en claves y valores, devuelve el primer resultado encontrado (case-insensitive).

```jasb
crear_memoria("datos.jmn")

recordar "inteligencia_artificial" con valor "Sistema cognitivo"
recordar "redes_neuronales" con valor "Arquitectura del cerebro"

# Buscar
buscar_en_memoria("cerebro")
si resultado != 0 entonces
    imprimir("Encontrado: " + resultado)  # "Arquitectura del cerebro"
fin_si

cerrar_memoria()
```

**Características:**

- ✅ Case-insensitive (ignora mayúsculas)
- ✅ Retorna primer resultado en `resultado`
- ✅ Busca en claves y valores
- ✅ Retorna 0 si no encuentra nada

#### 2. `buscar_en_memoria_cs(termino, case_sensitive)` - Con Control de Mayúsculas ✅

Búsqueda con control de case-sensitivity.

```jasb
crear_memoria("datos.jmn")

recordar "Python" con valor "Lenguaje de programación"
recordar "python_snake" con valor "Serpiente pitón"

# Buscar exactamente "Python" (mayúscula)
buscar_en_memoria_cs("Python", 1)  # case-sensitive = 1
imprimir("Exacto: " + resultado)  # "Python"

# Buscar cualquier variante de "python"
buscar_en_memoria_cs("python", 0)  # case-sensitive = 0
imprimir("Flexible: " + resultado)  # "Python" o "python_snake"

cerrar_memoria()
```

**Parámetros:**

- `termino` (texto): Término a buscar
- `case_sensitive` (entero): `0` = insensitive, `1` = sensitive

**Sinónimo:** `buscar_introspectiva(termino)` es equivalente a `buscar_en_memoria(termino)`

#### 3. `buscar_en_memoria_lista(termino, max)` - Lista de Resultados ⏳

> ⚠️ **Implementada pero requiere corrección de listas JMN**

Devuelve una lista con múltiples IDs encontrados (cuando el sistema de listas funcione).

```jasb
# Sintaxis (funcionalidad pendiente de sistema de listas)
elemento lista = buscar_en_memoria_lista("aprendizaje", 5)
elemento tam = mem_lista_tamano(lista)

entero i = 0
mientras i < tam hacer
    elemento id = mem_lista_obtener(lista, i)
    imprimir("Resultado " + i + ": " + id)
    i = i + 1
fin_mientras
```

**Parámetros:**

- `termino` (texto): Término a buscar
- `max` (entero): Máximo de resultados (1-100, default 10)

#### 4. `buscar_en_memoria_detallada(termino, max, cs)` - Con Metadata ⏳

> ⚠️ **Implementada pero requiere corrección de listas JMN**

Devuelve lista de mapas con metadata completa: ID, texto, posición, relevancia, tipo.

```jasb
# Sintaxis (funcionalidad pendiente de sistema de listas)
elemento lista = buscar_en_memoria_detallada("procesamiento", 3, 0)
elemento tam = mem_lista_tamano(lista)

entero i = 0
mientras i < tam hacer
    elemento mapa = mem_lista_obtener(lista, i)

    elemento id = mapa_obtener(mapa, "id")
    elemento texto = mapa_obtener(mapa, "texto")
    elemento posicion = mapa_obtener(mapa, "posicion")
    elemento relevancia = mapa_obtener(mapa, "relevancia")
    elemento es_clave = mapa_obtener(mapa, "es_clave")

    imprimir("ID: " + id)
    imprimir("Texto: " + texto)
    imprimir("Posición: " + posicion)
    imprimir("Relevancia: " + relevancia)

    i = i + 1
fin_mientras
```

**Parámetros:**

- `termino` (texto): Término a buscar
- `max` (entero): Máximo de resultados (1-100)
- `case_sensitive` (entero): `0` = insensitive, `1` = sensitive (opcional)

**Metadata disponible:**

- `id`: ID del concepto
- `texto`: Texto completo del concepto
- `posicion`: Posición donde se encontró
- `longitud_match`: Longitud de la coincidencia
- `es_clave`: 1 si es clave, 0 si es valor
- `relevancia`: Score 0.0-1.0

### Ejemplo de uso completo

```jasb
principal
    crear_memoria("mi_memoria.jmn")

    # Almacenar información
    recordar "usuario_nombre" con valor "Aurora"
    recordar "usuario_rol" con valor "IA"

    # Asociar conceptos
    asociar "Aurora" con "IA"

    # Recuperar
    buscar "usuario_nombre"
    imprimir "Nombre: " + resultado  # Imprime: Nombre: Aurora

    # Búsqueda introspectiva básica (FUNCIONA)
    buscar_en_memoria("Aurora")
    si resultado != 0 entonces
        imprimir "Encontrado: " + resultado
    fin_si

    # Búsqueda con control de mayúsculas (FUNCIONA)
    buscar_en_memoria_cs("AURORA", 0)  # case-insensitive
    imprimir "Resultado: " + resultado

    # Operaciones avanzadas (funciones no documentadas pero funcionales)
    buscar_asociados "Aurora"
    imprimir "ID asociado: " + resultado  # Ej: 2147483650

    propagar_activacion "IA"
    imprimir "Activación: " + resultado  # Ej: 2826870867

    cerrar_memoria()
fin_principal
```

### Documentación Adicional

- Ver: `docs/LENGUAJE/jmn/PROPAGACION_D_MAX_K_AUDITORIA_Y_ESTRES.md` — propagación con **`d_max`**, **`K`**, auditoría `JASBOOT_PROPAGAR_AUDIT`, pruebas de estrés
- Ver: `flujo_model_IA/INDICE_REFERENCIA_FLUJO_MODELO_IA.md` — mapa del modelo **`g(τ)`**, **`h(d)`**, máscaras y fusión teórica **`φ`**

Para más información sobre búsqueda introspectiva:

- Ver: `docs/BUSQUEDA_INTROSPECTIVA_COMPLETA.md` - Documentación técnica completa
- Ver: `docs/API_buscar_introspectiva.md` - Referencia rápida de API
- Ver: `docs/LENGUAJE/jmn/BUSQUEDA_INTROSPECTIVA.md` - Guía de uso JMN

---

## 15. Llamadas de sistema

### Cadenas y Conversión

| Función               | Descripción                                       |
| --------------------- | ------------------------------------------------- |
| `str_desde_numero(n)` | Convierte `entero` o `flotante` a `texto`.        |
| `decimal(f, p)`       | Convierte `flotante` a `texto` con `p` decimales. |
| `str_a_entero(s)`     | Convierte `texto` a `entero`.                     |
| `str_a_flotante(s)`   | Convierte `texto` a `flotante`.                   |
| `longitud(s)`         | Retorna la longitud de una cadena o lista.        |
| `concatenar(s1, s2)`  | Une dos cadenas de texto.                         |

### `tokenizar_L` y `claves_L` (pipeline L nativo)

Parten un **texto** en una **`lista`** de identificadores de cadena (hashes en caché de texto / JMN). El pipeline L realiza un procesamiento en capas (Normalización, Segmentación, Enriquecimiento y Filtrado) antes de emitir los tokens.

#### Firmas Soportadas

| Firma                                       | Descripción                                                                                  |
| ------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `tokenizar_L(texto)`                        | Separador por defecto `" "`; modo por defecto **3** (minúsculas + colapsar).                 |
| `tokenizar_L(texto, separador)`             | Si `separador` es `""`, segmenta por **blancos Unicode** (Zs/Zl/Zp, ASCII).                  |
| `tokenizar_L(texto, sep, modo)`             | Ver tabla de bits de **modo** abajo.                                                         |
| `tokenizar_L(..., modo, stops_csv)`         | CSV de stopwords adicionales (solo con bit **256**).                                         |
| `tokenizar_L(..., modo, stops, min_len)`    | Longitud mínima (**1..64**). **0** usa el modo (bit **8** ⇒ 2).                              |
| `tokenizar_L(..., modo, stops, min, lemas)` | **(Nuevo)** Sexto argumento: CSV de lematización `palabra=lema` (solo con bit **16777216**). |

`claves_L` es un **alias** exacto de `tokenizar_L`.

#### Bits de Modo (Configuración del Pipeline)

El `modo` es un entero donde cada bit activa una funcionalidad:

| Bit           | Constante     | Funcionalidad                                                                       |
| ------------- | ------------- | ----------------------------------------------------------------------------------- |
| **1**         | `LOWER`       | Convierte a minúsculas (Unicode-aware si se usa 1024-8192).                         |
| **2**         | `COLLAPSE`    | Colapsa múltiples espacios en uno solo y recorta extremos.                          |
| **4**         | `BIGRAM`      | Genera n-gramas de 2 palabras (`"palabra1 palabra2"`).                              |
| **16**        | `TRIGRAM`     | Genera n-gramas de 3 palabras.                                                      |
| **32**        | `STEM`        | Stemming ligero para español (quita sufijos comunes).                               |
| **64**        | `STRIP_PUNCT` | Elimina puntuación en los extremos de cada token.                                   |
| **256**       | `STOPWORDS`   | Filtra palabras vacías (builtin ES + CSV opcional).                                 |
| **512**       | `CONTRACT`    | Fusiona contracciones (`de`+`el` -> `del`, `a`+`el` -> `al`).                       |
| **1024**      | `NFKC`        | Normalización Unicode NFKC (Recomendado para IA).                                   |
| **16384**     | `STRIPMARK`   | Elimina acentos y marcas diacríticas (café -> cafe).                                |
| **262144**    | `PUNCT_WS`    | Trata puntuación (P\*) como separadores de palabras.                                |
| **524288**    | `SYMBOL_WS`   | Trata símbolos/emojis (So) como separadores.                                        |
| **16777216**  | `LEMMA`       | **(Nuevo)** Aplica lematización usando el CSV del 6º argumento.                     |
| **33554432**  | `EXISTING`    | **(Nuevo) Política de Nodos Efímeros**: Solo emite tokens que ya existan en la JMN. |
| **67108864**  | `4GRAM`       | **(Nuevo)** Genera n-gramas de 4 palabras.                                          |
| **134217728** | `5GRAM`       | **(Nuevo)** Genera n-gramas de 5 palabras.                                          |

#### Ejemplos de Uso

```jasboot
# 1. Tokenización básica para chat (minúsculas + colapsar + quitar puntuación)
# Modo: 1 + 2 + 64 = 67
lista L1 = tokenizar_L("¡Hola, Mundo!", " ", 67)
# Resultado: ["hola", "mundo"]

# 2. Pipeline L completo para IA (NFKC + Bigramas + Lemas + Nodos existentes)
# Modo: 1024 + 4 + 16777216 + 33554432 = 50332676
texto lemas = "corriendo=correr,casas=casa"
lista L2 = tokenizar_L("Corriendo por las casas", "", 50332676, "", 0, lemas)
# Resultado: ["correr", "por", "las", "casa", "correr por", "por las", "las casa"] (solo si existen en JMN)

# 3. Análisis de frases (N-gramas hasta 5 palabras)
# Modo: 1 + 4 + 16 + 67108864 + 134217728 = 201326613
lista L3 = tokenizar_L("el rapido zorro marron salta", " ", 201326613)
# Resultado: [..., "el rapido zorro marron salta"] (pentagrama incluido)
```

#### Límites y Alcance

- **Buffer de trabajo**: Máximo **8192 bytes** por llamada. Textos más largos se truncan.
- **Capacidad de salida**: Hasta **384 tokens** por llamada.
- **Longitud de token**: Máximo **255 caracteres** por token individual.
- **Lematización**: Máximo **256 reglas** `palabra=lema` en el CSV.
- **Stopwords**: Máximo **128 palabras** extra en el CSV.

Para una referencia técnica exhaustiva de los bits y el orden de normalización, consulte **`sdk-dependiente/docs/TOKENIZAR_L_PIPELINE_NATIVE.md`**.

Para **dividir sin** pipeline L (sin registro de modo), use **`dividir_texto(texto, separador)`**.

### Tiempo y Sistema

| Función                        | Descripción                                                                     |
| ------------------------------ | ------------------------------------------------------------------------------- |
| `obtener_timestamp()`          | Retorna el tiempo Unix actual (en segundos) como `entero`.                      |
| `formatear_timestamp(ts, fmt)` | Retorna el timestamp `ts` formateado según la cadena `fmt` (estilo `strftime`). |

**Ejemplo de formateo de tiempo:**

```jasb
principal
    entero ahora = obtener_timestamp()

    # Formatos comunes:
    texto f1 = formatear_timestamp(ahora, "%d/%m/%Y %H:%M:%S") # 17/04/2026 16:30:00
    texto f2 = formatear_timestamp(ahora, "%H:%M")             # 16:30
    texto f3 = formatear_timestamp(ahora, "%A, %d de %B")      # Friday, 17 de April

    imprimir "Fecha: " + f1
fin_principal
```

| Función                  | Descripción                                           |
| ------------------------ | ----------------------------------------------------- |
| `pausa_milisegundos(ms)` | Detiene la ejecución por `ms` milisegundos.           |
| `sistema_ejecutar(cmd)`  | Ejecuta un comando del sistema operativo.             |
| `sys_argc()`             | Retorna el número de argumentos de línea de comandos. |
| `sys_argv(i)`            | Retorna el argumento `i` de la línea de comandos.     |

### Archivos (FS)

| Función                               | Descripción                                                                                                                                                                                                                         |
| ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `abrir_archivo(ruta, modo)`           | Abre un archivo. Modos típicos: `"r"` lectura, `"w"` escritura truncando, `"a"` añadir, **`"wb"`** escritura binaria/truncando (uso del runtime para serializar `.jbr` desde la stdlib). Retorna descriptor `entero`; `0` si falla. |
| `leer_linea_archivo(fd)`              | Lee una línea (`texto`/ID según compilador); sin fd usa el archivo actual.                                                                                                                                                          |
| **`escribir_archivo(contenido, fd)`** | **Escribe texto en el archivo.** El primer argumento es el texto a escribir; el segundo es el descriptor (opcional omite segundo y usa archivo actual del proceso VM). Esta es la firma que usa también `guardar_archivo_jbr`.      |
| `cerrar_archivo(fd)`                  | Cierra el descriptor.                                                                                                                                                                                                               |
| `existe_archivo(ruta)`                | Entero distinto de cero si el archivo existe.                                                                                                                                                                                       |
| `fs_leer_texto(ruta)` (`fs_*` alias)  | Lectura de archivo completo a texto donde esté declarado en el SDK.                                                                                                                                                                 |

> **Persistencia `.jbr`:** el formato de recurso cerebral para el modelo de lenguaje N-gram está documentado en `stdlib/analitica-neuronal/docs/MODELO_LENGUAJE_JBR_Y_JMN.md`. No confundir con `.jmn` (memoria neuronal asociativa).

#### Ejemplo mínimo: exportar modelo a `.jbr` y cargarlo después

La API de alto nivel vive en `ModeloLenguaje` tras `inicializar_nlp_generativo_y_series()`. Ejemplo reproducible también en `apps/michelle_IA/herramientas/test_export_jbr_desde_lenguaje.jasb`:

```jasboot
usar {
    g_analitica,
    inicializar_fachada,
    inicializar_nlp_generativo_y_series
} de "stdlib/analitica-neuronal/analitica.jasb"

principal
    crear_memoria("mi_scratch.jmn")
    inicializar_fachada()
    inicializar_nlp_generativo_y_series()

    g_analitica.nlp_gen.entrenar("frase ejemplo uno dos tres")
    g_analitica.nlp_gen.entrenar("otra linea para el historial")

    entero ok = g_analitica.nlp_gen.guardar_archivo_jbr("salida_micromodelo.jbr", "mi_parte")
    # ok = numero de bloques escritos (>0 si hay historial)

    # Carga: 0 borra modelo y reaplica archivo; 1 fusiona si orden_n coincide
    entero docs = g_analitica.nlp_gen.cargar_archivo_jbr("salida_micromodelo.jbr", 0)

    consolidar_memoria()
    cerrar_memoria()
fin_principal
```

---

## 16. Módulos y bibliotecas

Jasboot soporta un sistema de módulos para organizar y reutilizar código.

### Importación de módulos

Jasboot utiliza la palabra clave `usar` para importar otros archivos. Por seguridad y claridad, se requiere que la importación sea explícita.

```jasb
usar { nombre1, nombre2 } de "ruta/al/archivo.jasb"
```

También se aceptan importaciones masivas (todo el módulo resuelto por el enlazador):

```jasb
usar todas de "ruta/al/archivo.jasb"
usar todo de "ruta/al/archivo.jasb"
```

> **Recomendación:** Prefiera `usar { … } de "…"` para dejar explícitos los símbolos y evitar colisiones de nombres. `usar todas de` / `usar todo de` siguen soportados por el compilador cuando la ruta y los `enviar` del módulo son correctos.

### Exportación de símbolos

Para hacer funciones y clases disponibles para otros módulos:

```jasb
# Exportar funciones específicas
enviar funcion mi_funcion
enviar clase MiClase

# Exportar múltiples elementos
enviar {funcion1, funcion2, clase1, clase2}

# Exportar todo (no recomendado, explícito es mejor)
enviar todas
```

**Reglas importantes:**

- Solo se puede exportar lo que está definido en el archivo actual
- Las funciones deben ser declaradas antes de exportarse
- Las clases deben estar completamente definidas antes de exportarse

### Ejemplo completo

**Archivo: `matematicas.jasb`**

```jasb
funcion sumar(entero a, entero b) retorna entero
    retornar a + b
fin_funcion

funcion multiplicar(entero a, entero b) retorna entero
    retornar a * b
fin_funcion

enviar {sumar, multiplicar}
```

**Archivo: `principal.jasb`**

```jasb
usar {sumar, multiplicar} de "matematicas.jasb"

principal
    entero resultado1 = sumar(5, 3)
    entero resultado2 = multiplicar(4, 6)
    imprimir "Suma: " + resultado1
    imprimir "Multiplicación: " + resultado2
fin_principal
```

### Módulos de la biblioteca estándar

La biblioteca estándar de Jasboot está organizada en módulos:

```jasb
# Importar logger
usar todas de "stdlib\\logging\\logger.jasb"

# Importar utilidades matemáticas
usar todas de "stdlib\\math\\basicas.jasb"

# Importar componentes de UI
usar todas de "stdlib\\Estructa\\ui.jasb"
```

---

## 17. Gestor de Paquetes (jbc)

El compilador `jbc` también actúa como gestor de ejecución y compilación.

### Comandos principales

| Comando                 | Descripción                                                  |
| ----------------------- | ------------------------------------------------------------ |
| `jbc <archivo.jasb>`    | Compila el archivo a `.jbo`.                                 |
| `jbc -r <archivo.jasb>` | Compila y ejecuta inmediatamente.                            |
| `jbc -e`                | Establece el directorio de trabajo en la carpeta del script. |
| `jbc -v`                | Muestra la versión del compilador y la VM.                   |

En el repositorio, el script **`.vscode/run-jasb.cjs`** (F5 / depuración) busca **`jbc`** y la **VM** en **`sdk-dependiente/`** (y alternativas bajo `sdk/`). Puede fijar rutas con `JASBOOT_JBC` y `JASBOOT_VM` / `JASBOOT_VM_TRACE`.

---

## Resumen de opcodes principales

| Categoría | Opcodes                                                                                                                                |
| --------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| Clases    | `OP_HEAP_RESERVAR` (con Class ID), `OP_LEER` (Class ID para dispatch), `OP_LLAMAR`                                                     |
| Cadenas   | `OP_STR_REEMPLAZAR` (`0x72`): reemplazo global de subcadena en texto (registros haystack, patrón, reemplazo; resultado = id de texto). |

---

**Última actualización:** 2026-05-10 (`para_cada`/`para cada` con tipo `caracter` sobre texto, `indice`, `sobre`/`en`, `reemplazar`/`remplazar`/`reemplazar_texto`, opcode `OP_STR_REEMPLAZAR`; nota sobre formas de `usar`).
