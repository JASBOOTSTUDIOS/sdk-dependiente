# 🧠 ARQUITECTURA DE RESONANCIA ASOCIATIVA (ARA)

Este documento define la **Lógica ARA**, el motor de pensamiento de Neurixis basado en pesos en la memoria JMN y algoritmos para procesar la interacción humana sin textos pre-escritos.

---

## ⚖️ 1. SIGNIFICADO DE LOS PESOS (0.0000 a 1.0000)

En Neurixis, el peso representa la **fuerza de la sinapsis** entre dos conceptos. Aunque el motor JMN soporta precisión de 32 bits (~7 decimales), estandarizamos el uso de **4 decimales** para un aprendizaje suave y una resolución de conflictos robusta.

| Rango de Peso | Significado Semántico | Ejemplo (A -> B) |
| :--- | :--- | :--- |
| **0.0000 - 0.2000** | Asociación débil o ruido. | "hola" -> "clima" |
| **0.3000 - 0.5000** | Relación contextual posible. | "perro" -> "parque" |
| **0.6000 - 0.8000** | Relación fuerte / Secuencia común. | "buenos" -> "días" |
| **0.9000 - 1.0000** | Definición o Identidad. | "neurixis" -> "IA" |

---

## 🧬 2. TIPOS DE RELACIONES Y SU USO

Usamos los tipos nativos de JMN para diferentes propósitos:

1.  **Tipo 1 (Asociación):** Define **qué es algo**. Se usa para la detección de intenciones y categorización.
    *   *Ejemplo:* `asociar("quiero", "intencion_peticion", 1, 0.7000)`
2.  **Tipo 2 (Secuencia):** Define **cómo se habla**. Es la base de la generación palabra por palabra.
    *   *Ejemplo:* `asociar("cómo", "estás", 2, 0.9000)`
3.  **Tipo 3 (Similitud):** Define **sinónimos y variantes**. Permite que la IA entienda "triste" y "apenado" como conceptos cercanos.

---

## 📐 3. ALGORITMOS DE CÁLCULO

### A. Detección de Intención (Suma de Activación)
Para determinar qué quiere el usuario, calculamos el **Potencial de Activación (PA)** de cada nodo de intención:

\[ PA(Intención) = \sum (Peso(Palabra_{in} \to Intención)) \]

*   **Regla:** La intención ganadora es aquella cuyo \( PA \) sea mayor y supere un umbral mínimo (ej: 0.5000).
*   **Propagación:** Si una palabra no tiene conexión directa, se busca en sus asociados de Tipo 3 (sinónimos) con una penalización del 20% (multiplicador 0.8000).

### B. Generación de Respuesta (Navegación por Pesos)
La respuesta no se busca, se **construye** siguiendo el camino de mayor peso secuencial:

1.  **Inicio:** Se activa el nodo `inicio_[intencion_ganadora]`.
2.  **Predicción:** Se ejecuta `pensar_siguiente(nodo_actual)`.
3.  **Selección:** JMN devuelve el nodo conectado por **Tipo 2** con el peso más alto.
4.  **Variabilidad:** Para evitar ser robótico, si hay dos nodos con pesos muy cercanos (diferencia < 0.0500), se elige uno al azar.
5.  **Fin:** El proceso se detiene al encontrar el nodo `fin_oracion`.

---

## 🧬 4. APRENDIZAJE HEBBIANO EMOCIONAL (AME)

"Nodos que se activan juntos, se conectan más fuerte, y la emoción dicta la velocidad".

En Neurixis, el aprendizaje no es lineal. Se modula según dos factores:
1.  **Valencia:** ¿La interacción fue buena o mala? (Determina el signo del cambio).
2.  **Intensidad (Adrenalina):** ¿Qué tan impactante fue el hecho? (Determina la magnitud).

#### Fórmula de Ajuste de Pesos:
\[ \Delta Peso = \Delta_{base} \times Multiplicador_{Intensidad} \]

| Estado Emocional | Valencia | Intensidad (Adrenalina) | $\Delta$ Final Sugerido |
| :--- | :--- | :--- | :--- |
| **Calma / Rutina** | Neutra | 1.0x | +0.0010 (Aprendizaje lento) |
| **Satisfacción** | Positiva | 2.0x | +0.0100 (Refuerzo estándar) |
| **Euforia / Éxito** | Positiva | 5.0x | +0.0500 (Grabado fuerte) |
| **Frustración** | Negativa | 2.0x | -0.0200 (Evitación estándar) |
| **Trauma / Error Crítico** | Negativa | 10.0x | -0.1000 (Bloqueo inmediato) |

*   **Aprendizaje por Impacto:** Si la intensidad es > 0.9000, se crea una conexión de Tipo 1 (Asociación) instantánea con peso 0.8000, simulando un recuerdo imborrable.

---

## 🛡️ 5. FILTRADO DE BASURA Y EVOLUCIÓN (SISTEMA INMUNE)

Para evitar que la IA aprenda "basura" (errores tipográficos, insultos aleatorios, ruidos), implementamos un sistema de **Memoria Transitoria vs. Permanente**.

### A. El Umbral de Consolidación (0.3000)
*   **Zona de Dudas (0.0000 - 0.2999):** Todo concepto o asociación nueva nace aquí. Es memoria volátil. Si no se repite o no tiene carga emocional, el proceso de `decae_conexiones` de JMN lo borrará automáticamente al final del día.
*   **Zona de Hecho (0.3000+):** Una vez que una asociación supera el 0.3000, se considera "conocimiento" y es mucho más difícil de borrar.

### B. Evolución de 0.2000 a 0.8000 (Criterios)
Una asociación sube de rango por dos vías:
1.  **Frecuencia (Hábito):** Si el usuario dice "A -> B" muchas veces en calma, el peso sube +0.0010 cada vez.
2.  **Impacto (Adrenalina):** Si el usuario dice "A -> B" en un momento de alta intensidad emocional, el peso puede subir +0.0500 o más de un solo golpe.

### C. Detección de Basura
La IA utiliza el **Filtro de Coherencia**:
*   Si un nuevo concepto no tiene asociaciones con el "Núcleo de Lenguaje" (palabras funcionales), se etiqueta como **Ruido** y se le asigna un peso inicial de 0.0500 (fácil de olvidar).
*   Si el concepto contiene caracteres no válidos o es una repetición sin sentido, se bloquea la creación del nodo.

---

## 🧬 6. DETERMINACIÓN DEL TIPO DE RELACIÓN

La IA decide el tipo de conexión según la estructura del aprendizaje:

| Escenario | Tipo Asignado | Lógica de Decisión |
| :--- | :--- | :--- |
| **Flujo de charla** | **Tipo 2 (Secuencia)** | Si la palabra B sigue a la A en el tiempo. |
| **Categorización** | **Tipo 1 (Asociación)** | Si el usuario dice "A es un B" o "A pertenece a B". |
| **Corrección** | **Tipo 3 (Similitud)** | Si el usuario dice "A es lo mismo que B" o usa A y B en contextos idénticos. |

---

## ⚖️ 7. HOMEOSTASIS Y PREVENCIÓN DE SATURACIÓN

Para evitar que todos los pesos lleguen a 1.0000, implementamos tres mecanismos de control:

### A. Crecimiento Asintótico (Amortiguación)
\[ \Delta Peso_{real} = (1.0000 - Peso_{actual}) \times \Delta Peso_{emocional} \]
Cuanto más cerca está un peso de 1.0000, más pequeño es el incremento.

### B. Normalización Competitiva (Suma Cero)
La "energía" asociativa de un nodo es finita. Si una conexión se fortalece, las demás conexiones del mismo tipo que salen del mismo nodo deben debilitarse proporcionalmente.
*   **Regla:** $\sum (Pesos_{salientes}) \approx 1.0000$.

### C. Decaimiento Natural (Entropía)
Cada vez que se ejecuta `consolidar_memoria()`, todos los pesos del grafo se multiplican por un factor de persistencia (ej: 0.9995).

---

## 🛡️ 8. PROTECCIÓN DE RELEVANCIA (MEMORIA A LARGO PLAZO)

### A. Inmunidad por Peso (0.9000+)
Las asociaciones que superan el umbral de **0.9000** son marcadas automáticamente como "Conocimiento Permanente". El algoritmo de decaimiento las ignora por completo.

### B. Soporte por Conectividad (Nodos Maestros)
Un nodo con más de 10 conexiones salientes fuertes (>0.7000) se considera un **"Nodo Maestro"**. Recibe un bono de persistencia (multiplicador 0.99999).

### C. Tasa de Olvido Dinámica
*   **Zona Roja (0.3000 - 0.5000):** Decaimiento 0.9990.
*   **Zona Verde (0.5001 - 0.8999):** Decaimiento 0.9999.
*   **Zona Platino (0.9000+):** Decaimiento 1.0000.

---

## ❓ 9. MANEJO DE LO DESCONOCIDO (CURIOSIDAD)

### A. Algoritmo de Inferencia por Contexto
1.  **Registro Silencioso:** Crea el nodo con peso de vitalidad bajo (0.1000).
2.  **Anclaje Contextual:** Asocia la palabra desconocida con las palabras conocidas de la frase (Tipo 1).
3.  **Activación de Curiosidad:** Si el PA de intenciones es < 0.4000, activa el nodo `estado_curiosidad`.

### B. Generación por Anclaje (Curiosidad Dinámica)
1.  **Inyección del Ancla:** La palabra desconocida se inyecta en el buffer de *Spreading Activation*.
2.  **Composición:** El generador navega desde `inicio_curiosidad`. El nodo anclado ganará la competencia en el punto gramatical donde se requiera un objeto.

---

## ✍️ 10. SIGNOS DE PUNTUACIÓN Y ESTRUCTURA

### A. En la Interpretación
Los signos `?` y `!` activan nodos de control (`intencion_pregunta`, `intencion_exclamacion`) y luego se limpian de los tokens.

### B. En la Generación
Los signos existen como **nodos físicos** (`nodo_punto`, `nodo_coma`). La IA predice su aparición mediante pesos secuenciales (Tipo 2).

---

## 🧩 11. INTUICIÓN LINGÜÍSTICA Y EMOJIS

### A. Fuzzy Matching (Detección de Semejanzas)
1.  **Cálculo de Distancia:** Si una palabra no existe, busca en el grafo palabras con distancia de Levenshtein <= 2.
2.  **Activación por Proximidad:** El nodo similar recibe una activación parcial (70%).
3.  **Aprendizaje de Variantes:** Si hay éxito, crea relación de **Tipo 3 (Similitud)** entre el error y la palabra correcta.

### B. Semántica de Emojis
Los emojis son **Nodos de Alta Intensidad**.
1.  **Carga Emocional:** "😊" -> `emocion_alegria` (Peso 0.9500).
2.  **Contextualización:** Actúan como multiplicadores de la intención detectada.

---

## 🧬 12. MORFOLOGÍA Y UNIÓN DE CONCEPTOS

### A. Extracción de Raíz
El normalizador busca prefijos/sufijos comunes para anclar variantes ("corriendo", "corrí") al concepto maestro ("correr") mediante conexiones de **Tipo 1**.

### B. Selección de Forma (Gramática por Secuencia)
La IA elige la palabra exacta basándose en el **Ajuste Secuencial (Tipo 2)** con la palabra anterior.
*   *Ejemplo:* `estoy` -> `corriendo` (Peso alto) vs `estoy` -> `correr` (Peso bajo).

---

## 🌐 13. MANEJO DE CONTEXTO (MEMORIA DINÁMICA)

### A. Memoria de Corto Plazo
Buffer circular de los últimos **10 intercambios** para resolución de referencias y evitar repeticiones.

### B. Persistencia de Contexto (Checkpointing)
La activación de la RAM se sincroniza con `neurixis_estado.jmn` tras cada interacción si el cambio es > 15% o tras 5s de inactividad.

### C. Nodos de Estado Contextual
Interruptores globales como `estado_humor_usuario`, `tema_dominante`, `expectativa_respuesta`.

---

## ⚖️ 14. FILTROS DE COHERENCIA GRAMATICAL

### A. Etiquetas de Concordancia
Cada palabra tiene asociaciones de **Tipo 1** con `gram_femenino`, `gram_masculino`, etc. Penalización de 0.1000 si no coinciden con la palabra previa.

### B. Derivación de Número
Si se requiere un plural inexistente, el sistema aplica sufijos (+s, +es) al nodo maestro y crea el nodo hijo dinámicamente.

---

## 🔄 15. MECANISMO DE SACIEDAD (EVITAR BUCLES)
Cada palabra emitida recibe una inhibición de **-1.0000** hasta el fin de la oración (reset en `nodo_punto`).

---

## 🎭 16. PERSONALIDAD COMO SESGO PERMANENTE
Nodos de rasgo (`rasgo_curiosidad`, etc.) siempre encendidos al **0.5000**, enviando energía constante a sus asociados.

---

## 🧬 17. SÍNTESIS DE PALABRAS NUEVAS (MATERIALIZACIÓN)
Creación de neologismos uniendo raíces y sufijos conocidos (ej: raíz + "mente") y registro inmediato en JMN.

---

## 🧮 18. MOTOR DE INFERENCIA LÓGICA (MIL)

### A. Mapeo de Operadores y Funciones
Símbolos matemáticos y palabras clave anclados a acciones (`+`, `-`, `*`, `/`, `^`, `v/`, `log`, `pi`, `e`).

### B. El Puente Cognitivo
Si el PA de un nodo de acción es > 0.8000, la VM de Jasboot ejecuta la operación e inyecta el resultado como un "Ancla de Verdad".

---

## 🏗️ 19. DEFINICIÓN DE NODOS MAESTROS (ANCLAS)
Infraestructura invisible, inmune al decaimiento, que actúa como hubs de control.
*   **Predefinidos:** Esqueleto funcional inicial.
*   **Emergentes:** Creados por clustering de aprendizaje.

---

## 🔢 20. REPRESENTACIÓN ATÓMICA DE NÚMEROS
Solo 10 nodos maestros (`digito_0` a `digito_9`). Las palabras numéricas ("mil") se asocian a una **Secuencia (Tipo 2)** de estos dígitos.

---

## 🔤 21. CONOCIMIENTO ATÓMICO DE CARACTERES
Cada letra es un nodo maestro. El alfabeto es una cadena de secuencias de Tipo 2.

### A. Navegación Bidireccional
Uso de `pensar_anterior` para recorrer secuencias en sentido opuesto (ej: alfabeto al revés) cuando el nodo `estado_inverso` está activo.

---

## 📄 22. ESTRUCTURA Y FORMATO DE RESPUESTA
Nodos de intención estructural (`formato_markdown`, `formato_lista`) que inyectan energía a símbolos de formato (`#`, `*`, `-`) para sesgar la generación.
