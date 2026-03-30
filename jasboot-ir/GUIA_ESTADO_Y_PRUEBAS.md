# 📊 Guía: Estado y Pruebas del IR Binario

**Cómo saber en qué estado está el IR binario y cómo probarlo**

---

## 🎯 Resumen Rápido

### ✅ Lo Que Funciona (Completado)

1. **Formato IR Binario** ✅
   - Header 16 bytes
   - Instrucciones 5 bytes
   - Metadata IA (formato básico)

2. **Herramientas CLI** ✅
   - Compilador (ejemplo básico)
   - Validador
   - VM (ejecución básica)

3. **Tests** ✅
   - Test básico
   - Test completo
   - Test exhaustivo
   - Test de rendimiento

### ❌ Lo Que Falta (Pendiente)

1. **Integración con jasboot-lang** ❌
   - No compila código `.jasb` real
   - Solo tiene ejemplo básico

2. **Soporte para Datos** ✅
   - Strings y constantes grandes soportadas
   - Carga de datos en VM (base 0)

3. **Backend Directo** ❌
   - Solo hay VM (interpretado)
   - No compila a código nativo

---

## 🧪 Cómo Probar el IR Binario

### Paso 1: Compilar Todo

```bash
cd jasboot-ir
make clean
make
```

**Esto genera:**
- `bin/ir_test` - Test básico
- `bin/ir_test_completo` - Test completo
- `bin/ir_test_exhaustivo` - Test exhaustivo
- `bin/ir_test_rendimiento` - Test de rendimiento
- `bin/jasboot-ir-compiler` - Compilador
- `bin/jasboot-ir-opt` - Optimizador IR
- `bin/jasboot-ir-validator` - Validador
- `bin/jasboot-ir-vm` - VM

---

### Paso 2: Ejecutar Tests

#### Test Básico (Formato IR)

```bash
make test
# O directamente:
./bin/ir_test
```

**Qué prueba:**
- ✅ Creación de archivos IR
- ✅ Lectura de archivos IR
- ✅ Escritura de instrucciones
- ✅ Header correcto

**Resultado esperado:**
```
✅ Test básico: Formato IR funciona correctamente
```

---

#### Test Completo (Todo el Sistema)

```bash
make test-all
# O directamente:
./bin/ir_test_completo
```

**Qué prueba:**
- ✅ Formato IR completo
- ✅ Validación de archivos
- ✅ Ejecución en VM
- ✅ Metadata IA
- ✅ Sección de datos (strings/constantes)

**Resultado esperado:**
```
✅ Test completo: Todos los componentes funcionan
```

---

#### Test Exhaustivo (Casos Límite)

```bash
make test-exhaustivo
# O directamente:
./bin/ir_test_exhaustivo
```

**Qué prueba:**
- ✅ Todas las instrucciones individualmente
- ✅ Casos límite (registros máximos, valores máximos)
- ✅ Programas complejos (factorial, etc.)
- ✅ Casos inválidos (detección de errores)

**Resultado esperado:**
```
✅ Test exhaustivo: Todos los casos pasan
```

---

#### Test de Rendimiento (Stress)

```bash
make test-rendimiento
# O directamente:
./bin/ir_test_rendimiento
```

**Qué prueba:**
- ✅ Generación masiva de IR
- ✅ Validación rápida
- ✅ Ejecución de programas grandes
- ✅ Benchmarks de rendimiento

**Resultado esperado:**
```
✅ Test rendimiento: Rendimiento aceptable
```

---

#### Todos los Tests

```bash
make test-todos
```

**Ejecuta todos los tests en secuencia.**

---

### Paso 3: Probar Herramientas CLI

#### 1. Compilador (Generar IR)

```bash
# Crear un archivo de ejemplo
echo "principal\n    retornar 0" > ejemplo.jasb

# Compilar a IR
./bin/jasboot-ir-compiler ejemplo.jasb -o ejemplo.jbo
```

**⚠️ Nota:** El compilador actual es solo un ejemplo básico. **NO compila código jasboot real**, solo genera un IR de ejemplo.

**Resultado esperado:**
```
✅ IR generado: ejemplo.jbo
```

---

#### 2. Validador (Verificar IR)

```bash
./bin/jasboot-ir-validator ejemplo.jbo
```

**Qué verifica:**
- ✅ Magic number correcto
- ✅ Versión compatible
- ✅ Tamaño de código válido
- ✅ Opcodes válidos
- ✅ Saltos válidos

**Resultado esperado:**
```
✅ Validación exitosa
Magic: 0x4A424F4F
Versión: 1
Código: 25 bytes
Instrucciones: 5
```

---

#### 3. VM (Ejecutar IR)

```bash
./bin/jasboot-ir-vm ejemplo.jbo
```

**Qué hace:**
- ✅ Carga el archivo IR
- ✅ Ejecuta las instrucciones
- ✅ Muestra salida (si hay `OBSERVAR`)
- ✅ Muestra estado final de registros

**Resultado esperado:**
```
Ejecutando IR...
Salida: [si hay OBSERVAR]
Registros finales:
  r0: 0
  r1: 0
  ...
✅ Ejecución completada
```

---

#### 4. Optimizador (Reducir IR)

```bash
./bin/jasboot-ir-opt ejemplo.jbo -o ejemplo_opt.jbo --stats
```

**Qué hace:**
- ✅ Aplica optimizaciones multi-paso
- ✅ Reduce instrucciones cuando es posible
- ✅ Mantiene semántica del programa

**Resultado esperado:**
```
Optimización completada
  Instrucciones: 5 -> 3
  Constantes plegadas: ...
  ...
```

---

## 📋 Checklist de Estado

### ✅ Funcionalidades Completas

- [x] **Formato IR Binario**
  - [x] Header 16 bytes
  - [x] Instrucciones 5 bytes
  - [x] Metadata IA (formato)
  - [x] Lectura/escritura de archivos

- [x] **Validación Básica**
  - [x] Magic number
  - [x] Versión
  - [x] Opcodes válidos
  - [x] Saltos válidos

- [x] **VM Básica**
  - [x] Carga de archivos IR
  - [x] Ejecución de instrucciones
  - [x] Registros virtuales
  - [x] Memoria básica
  - [x] Stack básico

- [x] **Herramientas CLI**
  - [x] Compilador (ejemplo)
  - [x] Validador
  - [x] VM

- [x] **Tests**
  - [x] Test básico
  - [x] Test completo
  - [x] Test exhaustivo
  - [x] Test de rendimiento

---

### ❌ Funcionalidades Pendientes

- [ ] **Integración con jasboot-lang**
  - [ ] Codegen conectado al parser real
  - [ ] Compila archivos `.jasb` reales
  - [ ] Acceso a AST real

- [ ] **Soporte para Datos**
  - [ ] Strings en sección de datos
  - [ ] Constantes grandes
  - [ ] Mapeo de direcciones

- [ ] **Backend Directo**
  - [ ] Compilación a código nativo
  - [ ] Sin necesidad de VM

- [x] **Optimizaciones**
  - [x] Dead code elimination
  - [x] Constant folding
  - [x] Propagación de inmediatos
  - [x] Simplificación de saltos

- [x] **JASB-SEC (mínimo)**
  - [x] Reglas básicas en validador
  - [ ] Control Flow Graph
  - [ ] Análisis estático

- [ ] **Debugging**
  - [ ] Breakpoints
  - [ ] Step-by-step
  - [ ] Inspección de registros

---

## 🔍 Cómo Verificar el Estado

### Método 1: Ejecutar Todos los Tests

```bash
cd jasboot-ir
make test-todos
```

**Si todos pasan:** ✅ Estado básico completo

**Si fallan:** ❌ Revisar errores específicos

---

### Método 2: Probar Flujo Completo

```bash
# 1. Compilar
make

# 2. Generar IR (ejemplo)
./bin/jasboot-ir-compiler ejemplo.jasb -o ejemplo.jbo

# 3. Validar
./bin/jasboot-ir-validator ejemplo.jbo

# 4. Ejecutar
./bin/jasboot-ir-vm ejemplo.jbo
```

**Si todo funciona:** ✅ Herramientas CLI funcionan

**Si falla:** ❌ Revisar componente específico

---

### Método 3: Comparar con Especificación

Revisa `FALTANTE.md` para ver qué falta según la especificación.

---

## 📊 Comparación: Compilador vs. IR Binario

### Compilador (jasboot-lang)

**Cómo probarlo:**
```bash
cd jasboot-lang
./jasboot programa.jasb
nasm -f elf64 salida.asm
gcc salida.o -o programa
./programa
```

**Ventaja:** ✅ Puedes probar con código real

**Estado:** ✅ Funcional

---

### IR Binario (jasboot-ir)

**Cómo probarlo:**
```bash
cd jasboot-ir
make test-todos
./bin/jasboot-ir-compiler ejemplo.jasb -o ejemplo.jbo
./bin/jasboot-ir-validator ejemplo.jbo
./bin/jasboot-ir-vm ejemplo.jbo
```

**Limitación:** ⚠️ Solo ejemplo básico (no código real)

**Estado:** ⚠️ Formato completo, falta integración

---

## 🎯 Estado Actual Resumido

### ✅ Lo Que Funciona (100%)

1. **Formato IR Binario** - Completo
2. **Validación Básica** - Completo
3. **VM Básica** - Completo
4. **Herramientas CLI** - Completo
5. **Tests** - Completo

### ⚠️ Lo Que Falta (Para Funcionalidad Completa)

1. **Integración con jasboot-lang** - Crítico
2. **Soporte para Datos** - Crítico
3. **Backend Directo** - Importante
4. **Optimizaciones** - ✅ Completado
5. **JASB-SEC** - Futuro

---

## 💡 Cómo Saber el Estado en Tiempo Real

### Comando Rápido

```bash
cd jasboot-ir
make test-todos && echo "✅ IR Binario: Funcional" || echo "❌ IR Binario: Hay problemas"
```

### Verificar Componentes Individuales

```bash
# Formato IR
./bin/ir_test && echo "✅ Formato OK" || echo "❌ Formato falla"

# Validación
./bin/jasboot-ir-validator ejemplo.jbo && echo "✅ Validación OK" || echo "❌ Validación falla"

# VM
./bin/jasboot-ir-vm ejemplo.jbo && echo "✅ VM OK" || echo "❌ VM falla"
```

---

## 📝 Notas Importantes

1. **El IR binario está funcional** para su propósito actual (formato, validación, VM básica)

2. **Falta integración** con jasboot-lang para compilar código real

3. **Los tests son exhaustivos** - si pasan todos, el formato está correcto

4. **La VM es básica** - funciona pero no es optimizada

5. **El compilador es ejemplo** - no compila código jasboot real aún

---

## 🚀 Próximos Pasos

Para completar el IR binario:

1. **Integrar codegen con jasboot-lang** (crítico)
2. **Implementar soporte de datos** (crítico)
3. **Mejorar VM o implementar backend** (importante)

---

**Última actualización:** 2026-01-19
