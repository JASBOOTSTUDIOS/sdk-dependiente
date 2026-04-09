#!/bin/bash
# Script para verificar el estado del IR Binario

echo "🔍 Verificando estado del IR Binario..."
echo ""

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Función para verificar test
check_test() {
    local test_name=$1
    local test_binary=$2
    
    echo -n "  Verificando $test_name... "
    if [ -f "$test_binary" ]; then
        if ./"$test_binary" > /dev/null 2>&1; then
            echo -e "${GREEN}✅ OK${NC}"
            return 0
        else
            echo -e "${RED}❌ FALLA${NC}"
            return 1
        fi
    else
        echo -e "${YELLOW}⚠️  No existe${NC}"
        return 2
    fi
}

# Verificar que estamos en el directorio correcto
if [ ! -f "Makefile" ]; then
    echo -e "${RED}❌ Error: No estás en el directorio jasboot-ir${NC}"
    exit 1
fi

# Compilar si es necesario
if [ ! -d "bin" ] || [ -z "$(ls -A bin 2>/dev/null)" ]; then
    echo "📦 Compilando..."
    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo -e "${RED}❌ Error al compilar${NC}"
        exit 1
    fi
    echo "✅ Compilación exitosa"
    echo ""
fi

# Verificar binarios
echo "📋 Verificando binarios:"
BINARIES=(
    "ir_test:Test básico"
    "ir_test_completo:Test completo"
    "ir_test_exhaustivo:Test exhaustivo"
    "ir_test_rendimiento:Test rendimiento"
    "jasboot-ir-compiler:Compilador"
    "jasboot-ir-validator:Validador"
    "jasboot-ir-vm:VM"
)

ALL_OK=true
for binary_info in "${BINARIES[@]}"; do
    IFS=':' read -r binary name <<< "$binary_info"
    if [ -f "bin/$binary" ]; then
        echo -e "  ${GREEN}✅${NC} $name (bin/$binary)"
    else
        echo -e "  ${RED}❌${NC} $name (bin/$binary) - NO EXISTE"
        ALL_OK=false
    fi
done

echo ""

# Ejecutar tests
echo "🧪 Ejecutando tests:"
TESTS=(
    "bin/ir_test:Test básico"
    "bin/ir_test_completo:Test completo"
    "bin/ir_test_exhaustivo:Test exhaustivo"
    "bin/ir_test_rendimiento:Test rendimiento"
)

TESTS_OK=0
TESTS_FAIL=0
for test_info in "${TESTS[@]}"; do
    IFS=':' read -r test_path name <<< "$test_info"
    if check_test "$name" "$test_path"; then
        ((TESTS_OK++))
    else
        ((TESTS_FAIL++))
    fi
done

echo ""

# Resumen
echo "📊 Resumen:"
echo "  Tests pasados: $TESTS_OK"
echo "  Tests fallidos: $TESTS_FAIL"
echo ""

if [ $TESTS_FAIL -eq 0 ]; then
    echo -e "${GREEN}✅ Estado: FUNCIONAL${NC}"
    echo ""
    echo "✅ Formato IR: Completo"
    echo "✅ Validación: Completo"
    echo "✅ VM: Completo"
    echo "✅ Tests: Todos pasan"
    echo ""
    echo "⚠️  Pendiente:"
    echo "  - Integración con jasboot-lang"
    echo "  - Soporte para datos"
    echo "  - Backend directo"
    exit 0
else
    echo -e "${RED}❌ Estado: HAY PROBLEMAS${NC}"
    echo ""
    echo "Revisa los errores arriba"
    exit 1
fi
