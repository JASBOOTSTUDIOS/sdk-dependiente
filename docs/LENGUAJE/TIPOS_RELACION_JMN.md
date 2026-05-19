# Tipos de Relación JMN (1–30): Guía Detallada y Casos de Uso

En la Arquitectura de Resonancia Asociativa (**ARA**), los tipos de relación no son simples etiquetas; definen cómo se propaga la energía del pensamiento y cómo se estructuran las respuestas de Neurixis.

**Plan futuro (capa fonética / acústica, fuera del catálogo numérico actual):** [JMN_PLAN_CAPA_FONETICA_FUTURO.md](./JMN_PLAN_CAPA_FONETICA_FUTURO.md).

---

## 1. Asociación General (Tipo 1)

- **Propósito**: Crear una conexión semántica débil o fuerte entre dos conceptos que "aparecen juntos" pero no tienen un orden o jerarquía estricta. Es el pegamento del grafo.
- **Uso**: Agrupar conceptos por contexto o relevancia.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("mar", "sal", 1, 0.8)
  asociar_relacion("café", "mañana", 1, 0.6)
  ```
  *Neurixis entenderá que al hablar de "mar", el concepto "sal" debe activarse en su rastro de pensamiento.*

## 2. Patrón Reconocido (Tipo 2)

- **Propósito**: Identificar estructuras recurrentes. Se usa para que la IA reconozca que un conjunto de activaciones disparan un concepto de mayor nivel.
- **Uso**: Reconocimiento de formas, estilos de habla o estructuras de datos.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("sujeto_verbo_predicado", "oracion_valida", 2, 1.0)
  ```

## 3. Secuencia Narrativa/Gramatical (Tipo 3)

- **Propósito**: **Fundamental para la generación de texto**. Define qué palabra o idea "sigue" a la anterior. Es el motor del lenguaje.
- **Uso**: Construir oraciones, pasos de un proceso o recitar el alfabeto.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("hola", "como", 3, 0.9)
  asociar_relacion("como", "estas", 3, 0.9)
  ```
  *Al invocar `pensar_siguiente("hola")`, la IA elegirá "como" con alta probabilidad.*

## 4. Similitud / Sinónimo (Tipo 4)

- **Propósito**: Establecer equivalencia. La VM crea automáticamente la conexión inversa (A ↔ B).
- **Uso**: Manejo de sinónimos, corrección ortográfica y variaciones regionales.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("feliz", "contento", 4, 0.95)
  ```
  *Si el usuario dice "estoy contento", la IA puede activar nodos vinculados a "feliz".*

## 5. Oposición / Antónimo (Tipo 5)

- **Propósito**: Definir contraste. Al igual que el Tipo 4, es bidireccional.
- **Uso**: Razonamiento lógico, resolución de contradicciones y matices emocionales.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("luz", "oscuridad", 5, 1.0)
  ```

## 6. Pertenencia / Taxonomía (Tipo 6)

- **Propósito**: Definir jerarquías de "es un". Permite a la IA heredar propiedades.
- **Uso**: Clasificación de objetos y conceptos.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("gato", "mamifero", 6, 1.0)
  asociar_relacion("manzana", "fruta", 6, 1.0)
  ```
  *Neurixis sabrá que si algo es un "gato", también es un "mamifero".*

## 7. Causalidad (Tipo 7)

- **Propósito**: Modelar "Causa → Efecto". Es vital para el Motor de Inferencia Lógica (MIL).
- **Uso**: Explicar por qué ocurren las cosas o predecir resultados.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("fuego", "calor", 7, 0.9)
  asociar_relacion("lluvia", "mojado", 7, 1.0)
  ```

## 8. Temporalidad (Tipo 8)

- **Propósito**: Definir orden en el tiempo (Antes / Después).
- **Uso**: Narrar eventos históricos o seguir instrucciones cronológicas.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("desayuno", "almuerzo", 8, 0.7)
  ```

## 9. Intención / Objetivo (Tipo 9)

- **Propósito**: Vincular una acción o estado con una meta.
- **Uso**: Detectar qué quiere el usuario (intenciones de compra, ayuda, información).
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("estudiar", "aprender", 9, 0.9)
  asociar_relacion("que", "intencion_pregunta", 9, 1.0)
  ```

## 10. Valoración / Prioridad (Tipo 10)

- **Propósito**: Definir la calidad, ética o tono de una respuesta.
- **Uso**: Filtrar respuestas ofensivas o priorizar un tono amable.

---

## Fusión y Agregación (α_τ y φ)

A partir de la Fase 6 del modelo IA, los canales de evidencia (tipos 1-30) se combinan mediante pesos de fusión (**α_τ**) y funciones agregadoras (**φ**). 

- **Pesos de Fusión (α_τ)**: Determinan qué canal domina en la decisión final. Por defecto, Neurixis prioriza el canal de **Secuencia (τ=3)** y **Acción (τ=27)** para generar lenguaje fluido.
- **Inhibición (Fase 7/8)**: Para evitar bucles infinitos (como repetir la misma pregunta), el motor de fusión penaliza nodos que ya han sido utilizados en el historial reciente de la conversación.
- **Implementación**: Ver `neurixis_pensar_siguiente_humano` en `generador.jasb`.

- **Propósito**: Asignar peso ético, moral o de importancia a un concepto.
- **Uso**: Personalidad de la IA, filtros de seguridad y toma de decisiones.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("mentir", "malo", 10, 0.8)
  asociar_relacion("ayudar", "bueno", 10, 0.9)
  ```

## 11. Ubicación Espacial (Tipo 11)

- **Propósito**: Relacionar una entidad con su lugar físico o virtual.
- **Uso**: Geografía, navegación y contexto de objetos.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("torre_eiffel", "paris", 11, 1.0)
  ```

## 12. Propiedad / Atributo (Tipo 12)

- **Propósito**: Definir características descriptivas de un objeto.
- **Uso**: Descripción de entidades.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("cielo", "azul", 12, 0.7)
  asociar_relacion("limon", "acido", 12, 0.9)
  ```

## 13. Parte de / Meronimia (Tipo 13)

- **Propósito**: Relación "Parte → Todo". A diferencia del Tipo 6, esto es composición física.
- **Uso**: Descomposición de objetos complejos.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("rueda", "coche", 13, 1.0)
  asociar_relacion("tecla", "teclado", 13, 1.0)
  ```

## 14. Consecuencia Lógica (Tipo 14)

- **Propósito**: Modelar el "Entonces" de una premisa. Es una causalidad más abstracta.
- **Uso**: Razonamiento deductivo complejo.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("delito", "carcel", 14, 0.8)
  ```

## 15. Condición / Prerrequisito (Tipo 15)

- **Propósito**: Definir qué se necesita para que algo sea posible.
- **Uso**: Planificación de tareas y validación de estados.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("llave", "abrir_puerta", 15, 1.0)
  asociar_relacion("internet", "navegar", 15, 1.0)
  ```

## 16. Instancia / Individuo (Tipo 16)

- **Propósito**: Distinguir entre un concepto general (clase) y un objeto o sujeto específico (individuo).
- **Uso**: Identidad de entidades únicas, nombres propios y objetos concretos.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("pelusa", "gato", 16, 1.0)
  asociar_relacion("sol", "estrella", 16, 1.0)
  ```

## 17. Posesión / Tenencia (Tipo 17)

- **Propósito**: Indicar que una entidad es dueña o tiene a su disposición otra entidad, sin que sea parte de su estructura física.
- **Uso**: Modelar propiedad, inventarios y relaciones de pertenencia legal o situacional.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("juan", "llaves", 17, 1.0)
  asociar_relacion("ia", "datos", 17, 0.8)
  ```

## 18. Funcionalidad / Uso (Tipo 18)

- **Propósito**: Definir la utilidad de un objeto o la acción que permite realizar.
- **Uso**: Razonamiento procedimental y selección de herramientas.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("martillo", "clavar", 18, 1.0)
  asociar_relacion("neurixis", "asistir", 18, 0.9)
  ```

## 19. Evidencia / Fuente (Tipo 19)

- **Propósito**: Vincular un conocimiento con su origen o la prueba que lo sustenta.
- **Uso**: Veracidad, gestión de confianza y rastreo de información.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("tierra_redonda", "ciencia", 19, 1.0)
  asociar_relacion("rumor", "usuario_desconocido", 19, 0.4)
  ```

## 20. Magnitud / Comparación (Tipo 20)

- **Propósito**: Establecer escalas relativas y comparaciones cualitativas entre conceptos.
- **Uso**: Razonamiento comparativo (más que, menos que, igual a).
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("elefante", "raton", 20, 0.9) # Significa "más grande que"
  ```

## 21. Frecuencia / Probabilidad (Tipo 21)

- **Propósito**: Modelar cuán habitual es una relación o evento.
- **Uso**: Manejo de incertidumbre y predicción estadística.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("invierno", "frio", 21, 0.8) # "Suele hacer frío"
  ```

## 22. Parentesco / Social (Tipo 22)

- **Propósito**: Definir vínculos familiares, jerarquías sociales o relaciones interpersonales.
- **Uso**: Entender dinámicas humanas y estructuras organizacionales.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("ana", "pedro", 22, 1.0) # "Madre de"
  asociar_relacion("empleado", "jefe", 22, 0.7)
  ```

## 23. Calificación / Especificación (Tipo 23)

- **Propósito**: **Eliminar la necesidad de IDs técnicos con guiones bajos**. Une un concepto base con un calificador que define su naturaleza.
- **Uso**: Construir conceptos complejos a partir de átomos (Sustantivo + Adjetivo).
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("hecho", "cientifico", 23, 1.0)
  asociar_relacion("tierra", "redonda", 23, 1.0)
  ```
  *Neurixis ya no necesita el ID `hecho_cientifico`; entiende que es un "hecho" con la cualidad de "científico".*

## 24. Acción / Desempeño (Tipo 24)

- **Propósito**: Vincular un sujeto con la acción que realiza (Verbalización).
- **Uso**: Estructurar el núcleo de una oración o proceso activo.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("juan", "corre", 24, 1.0)
  asociar_relacion("sol", "brilla", 24, 1.0)
  ```

## 25. Complemento / Objeto (Tipo 25)

- **Propósito**: Completar el significado de una acción vinculándola con su objeto.
- **Uso**: Cerrar la estructura Sujeto-Verbo-Objeto (SVO) de forma atómica.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("come", "manzana", 25, 1.0)
  asociar_relacion("estudia", "astronomia", 25, 1.0)
  ```

## 26. Cuantificación / Valor (Tipo 26)

- **Propósito**: Vincular un concepto con su valor numérico atómico.
- **Uso**: Permitir que la IA sepa "cuántos" o "cuánto" de algo existe.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("manzanas", "5", 26, 1.0)
  ```

## 27. Unidad de Medida / Contexto Numérico (Tipo 27)

- **Propósito**: Definir la escala o sistema de medida de un valor.
- **Uso**: Distinguir entre distancia, tiempo, peso, etc.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("5", "kilometros", 27, 1.0)
  asociar_relacion("edad", "años", 27, 1.0)
  ```

## 28. Operador Matemático (Tipo 28)

- **Propósito**: Identificar qué palabras disparan funciones de cálculo en el motor MIL.
- **Uso**: Mapear el lenguaje natural a opcodes de la VM (Suma, Resta, etc.).
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("mas", "suma", 28, 1.0)
  asociar_relacion("diferencia", "resta", 28, 1.0)
  ```

## 29. Situación / Contexto Situacional (Tipo 29)

- **Propósito**: Definir el escenario o marco general en el que ocurre una acción o existe un concepto.
- **Uso**: Establecer el "escenario" de una conversación o evento.
- **Ejemplo Práctico**:
  ```jasboot
  asociar_relacion("cena", "restaurante", 29, 1.0)
  ```

## 30. Referencia Lógica / Deducción (Tipo 30)

- **Propósito**: Vincular un concepto con otro a través de una operación o dependencia, permitiendo el razonamiento dinámico.
- **Uso**: Resolver problemas como "Ana tiene 6 años más que Juan" sin guardar datos estáticos.
- **Ejemplo Práctico**:
  ```jasboot
  # Ana -> Juan (Referencia Lógica con modificador en el peso o rastro)
  asociar_relacion("ana", "juan", 30, 1.0) 
  ```

---

*Última revisión: 2026-05-13. Estas definiciones completan el mapa cognitivo ARA para un entendimiento situacional profundo y un motor de inferencia lógica (MIL) avanzado.*