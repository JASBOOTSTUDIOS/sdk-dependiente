# PowerShell build script for VM
$ErrorActionPreference = "Stop"

$IR_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $IR_DIR

$SDK_DEPENDIENTE = Resolve-Path "$IR_DIR\.."
$JMN_PKG = "$SDK_DEPENDIENTE\jasboot-jmn-core"

$BUILD_DIR = "build"
$SRC_DIR = "src"
$JMN_DIR = "$JMN_PKG\src\memoria_neuronal"
$COMPAT_SRC = "$JMN_PKG\src\platform_compat.c"
$BIN_DIR = "bin"

if (-not (Test-Path $BUILD_DIR)) { New-Item -ItemType Directory -Path $BUILD_DIR }
if (-not (Test-Path $BIN_DIR)) { New-Item -ItemType Directory -Path $BIN_DIR }

Write-Host "Cleaning Build..."
Remove-Item -Path "$BUILD_DIR\*" -ErrorAction SilentlyContinue

$CFLAGS = @(
    "-Wall", "-Wextra", "-std=c11", "-O3", "-march=native",
    "-I$SRC_DIR", "-I$JMN_PKG\src", "-I$JMN_DIR",
    "-I$IR_DIR\third_party\utf8proc",
    "-DJASBOOT_LANG_INTEGRATION"
)

$LDLIBS = @()
if ($IsWindows -or $env:OS -eq "Windows_NT") {
    $LDLIBS += "-lws2_32"
}

Write-Host "Compiling VM Core..."
gcc $CFLAGS -c "$SRC_DIR/mai.c" -o "$BUILD_DIR/mai.o"
gcc $CFLAGS -c "$SRC_DIR/vm.c" -o "$BUILD_DIR/vm.o"
gcc $CFLAGS -DUTF8PROC_STATIC -c "$IR_DIR/third_party/utf8proc/utf8proc.c" -o "$BUILD_DIR/utf8proc.o"
gcc $CFLAGS -c "$SRC_DIR/vm_unicode_norm.c" -o "$BUILD_DIR/vm_unicode_norm.o"
gcc $CFLAGS -c "$SRC_DIR/vm_analitica_mlp.c" -o "$BUILD_DIR/vm_analitica_mlp.o"
gcc $CFLAGS -c "$SRC_DIR/ir_vm.c" -o "$BUILD_DIR/ir_vm.o"
gcc $CFLAGS -c "$SRC_DIR/ir_format.c" -o "$BUILD_DIR/ir_format.o"
gcc $CFLAGS -c "$SRC_DIR/reader_ir.c" -o "$BUILD_DIR/reader_ir.o"
gcc $CFLAGS -c "$SRC_DIR/cognitive_stubs.c" -o "$BUILD_DIR/cognitive_stubs.o"

Write-Host "Compiling JMN..."
Get-ChildItem -Path "$JMN_DIR\*.c" | ForEach-Object {
    $name = $_.BaseName
    Write-Host "Compiling $_"
    gcc $CFLAGS -c $_.FullName -o "$BUILD_DIR/$name.o"
}

Write-Host "Compiling Compat..."
gcc $CFLAGS -c $COMPAT_SRC -o "$BUILD_DIR/jmn_compat.o"

Write-Host "Linking..."
gcc -O3 -march=native "$BUILD_DIR/*.o" -o "$BIN_DIR/jasboot-ir-vm-trace.exe" $LDLIBS
Copy-Item -Path "$BIN_DIR/jasboot-ir-vm-trace.exe" -Destination "$BIN_DIR/jasboot-ir-vm.exe" -Force

Write-Host "Build Successful."
Pop-Location
