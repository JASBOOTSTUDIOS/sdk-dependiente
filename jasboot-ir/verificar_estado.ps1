# Script PowerShell para verificar el estado del IR Binario

Write-Host "🔍 Verificando estado del IR Binario..." -ForegroundColor Cyan
Write-Host ""

# Verificar que estamos en el directorio correcto
if (-not (Test-Path "Makefile")) {
    Write-Host "❌ Error: No estás en el directorio jasboot-ir" -ForegroundColor Red
    exit 1
}

# Compilar si es necesario
if (-not (Test-Path "bin") -or (Get-ChildItem "bin" -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0) {
    Write-Host "📦 Compilando..." -ForegroundColor Yellow
    & make clean 2>&1 | Out-Null
    & make 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Error al compilar" -ForegroundColor Red
        exit 1
    }
    Write-Host "✅ Compilación exitosa" -ForegroundColor Green
    Write-Host ""
}

# Verificar binarios
Write-Host "📋 Verificando binarios:" -ForegroundColor Cyan
$binaries = @(
    @{Name="Test básico"; Path="bin/ir_test"},
    @{Name="Test completo"; Path="bin/ir_test_completo"},
    @{Name="Test exhaustivo"; Path="bin/ir_test_exhaustivo"},
    @{Name="Test rendimiento"; Path="bin/ir_test_rendimiento"},
    @{Name="Compilador"; Path="bin/jasboot-ir-compiler"},
    @{Name="Optimizador"; Path="bin/jasboot-ir-opt"},
    @{Name="Validador"; Path="bin/jasboot-ir-validator"},
    @{Name="VM"; Path="bin/jasboot-ir-vm"}
)

foreach ($bin in $binaries) {
    if (Test-Path $bin.Path) {
        Write-Host "  ✅ $($bin.Name) ($($bin.Path))" -ForegroundColor Green
    } else {
        Write-Host "  ❌ $($bin.Name) ($($bin.Path)) - NO EXISTE" -ForegroundColor Red
    }
}

Write-Host ""

# Ejecutar tests
Write-Host "🧪 Ejecutando tests:" -ForegroundColor Cyan
$tests = @(
    @{Name="Test básico"; Path="bin/ir_test"},
    @{Name="Test completo"; Path="bin/ir_test_completo"},
    @{Name="Test exhaustivo"; Path="bin/ir_test_exhaustivo"},
    @{Name="Test rendimiento"; Path="bin/ir_test_rendimiento"}
)

$testsOk = 0
$testsFail = 0

foreach ($test in $tests) {
    Write-Host -NoNewline "  Verificando $($test.Name)... "
    if (Test-Path $test.Path) {
        & $test.Path 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "✅ OK" -ForegroundColor Green
            $testsOk++
        } else {
            Write-Host "❌ FALLA" -ForegroundColor Red
            $testsFail++
        }
    } else {
        Write-Host "⚠️  No existe" -ForegroundColor Yellow
        $testsFail++
    }
}

Write-Host ""

# Resumen
Write-Host "📊 Resumen:" -ForegroundColor Cyan
Write-Host "  Tests pasados: $testsOk" -ForegroundColor Green
Write-Host "  Tests fallidos: $testsFail" -ForegroundColor $(if ($testsFail -eq 0) { "Green" } else { "Red" })
Write-Host ""

if ($testsFail -eq 0) {
    Write-Host "✅ Estado: FUNCIONAL" -ForegroundColor Green
    Write-Host ""
    Write-Host "✅ Formato IR: Completo"
    Write-Host "✅ Validación: Completo"
    Write-Host "✅ VM: Completo"
    Write-Host "✅ Tests: Todos pasan"
    Write-Host ""
    Write-Host "⚠️  Pendiente:" -ForegroundColor Yellow
    Write-Host "  - Integración con jasboot-lang"
    Write-Host "  - JASB-SEC"
    Write-Host "  - Backend directo"
    exit 0
} else {
    Write-Host "❌ Estado: HAY PROBLEMAS" -ForegroundColor Red
    Write-Host ""
    Write-Host "Revisa los errores arriba"
    exit 1
}
