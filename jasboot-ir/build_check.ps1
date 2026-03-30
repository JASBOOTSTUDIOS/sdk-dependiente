$ErrorActionPreference = "Stop"

$BUILD_DIR = "build"
$BIN_DIR = "bin"
$SRC_DIR = "src"
$JMN_DIR = "..\jasboot-lang\src\memoria_neuronal"
$COMPAT_SRC = "..\jasboot-lang\src\platform_compat.c"
$EXE_PATH = "$BIN_DIR\jasboot-ir-vm.exe"

# 1. Cleanup
Write-Host "Limpiando build anterior..." -ForegroundColor Cyan
if (Test-Path $BUILD_DIR) { Remove-Item $BUILD_DIR -Recurse -Force }
if (Test-Path $BIN_DIR) { Remove-Item $BIN_DIR -Recurse -Force }
New-Item -ItemType Directory -Force -Path $BUILD_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $BIN_DIR | Out-Null

# 2. Compilation Flags
$CFLAGS = "-Wall -Wextra -std=c11 -g -I$SRC_DIR -I..\jasboot-lang\src -DJASBOOT_LANG_INTEGRATION"

# 3. Compile Core
Write-Host "Compilando VM Core..." -ForegroundColor Cyan
gcc $CFLAGS.Split(" ") -c "$SRC_DIR/vm.c" -o "$BUILD_DIR/vm.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando vm.c" }

gcc $CFLAGS.Split(" ") -c "$SRC_DIR/ir_vm.c" -o "$BUILD_DIR/ir_vm.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando ir_vm.c" }

gcc $CFLAGS.Split(" ") -c "$SRC_DIR/ir_format.c" -o "$BUILD_DIR/ir_format.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando ir_format.c" }

gcc $CFLAGS.Split(" ") -c "$SRC_DIR/reader_ir.c" -o "$BUILD_DIR/reader_ir.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando reader_ir.c" }

# 4. Compile JMN
Write-Host "Compilando JMN..." -ForegroundColor Cyan
$jmn_files = Get-ChildItem "$JMN_DIR\*.c"
foreach ($file in $jmn_files) {
    Write-Host "  Compilando $($file.Name)..."
    gcc $CFLAGS.Split(" ") -c $file.FullName -o "$BUILD_DIR/$($file.BaseName).o"
    if ($LASTEXITCODE -ne 0) { throw "Error compilando $($file.Name)" }
}

# 5. Compile Compat
Write-Host "Compilando Compat..." -ForegroundColor Cyan
gcc $CFLAGS.Split(" ") -c $COMPAT_SRC -o "$BUILD_DIR/jmn_compat.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando jmn_compat.o" }

# 6. Link
Write-Host "Enlazando..." -ForegroundColor Cyan
$obj_files = Get-ChildItem "$BUILD_DIR\*.o" | Select-Object -ExpandProperty FullName
gcc -g $obj_files -o $EXE_PATH
if ($LASTEXITCODE -ne 0) { throw "Error enlazando" }

# 7. Validation
if (-not (Test-Path $EXE_PATH)) {
    throw "FATAL: El binario no se creó."
}
$timestamp = (Get-Item $EXE_PATH).LastWriteTime
Write-Host "BUILD EXITOSO: $EXE_PATH ($timestamp)" -ForegroundColor Green
