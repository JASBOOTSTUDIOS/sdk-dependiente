# Makefile simplificado para jasboot IR (Windows)

CC = gcc
# Raíz del paquete JMN (directorio que contiene src/). Por defecto: hermano de jasboot-ir en sdk-dependiente.
JMN_PKG ?= ../jasboot-jmn-core
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc -I$(JMN_PKG)/src -I$(JMN_PKG)/src/memoria_neuronal -DJASBOOT_LANG_INTEGRATION
LDFLAGS = -g

# Directorios
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

# JMN Neural Memory Sources
JMN_DIR = $(JMN_PKG)/src/memoria_neuronal
JMN_OBJS = $(BUILD_DIR)/jmn_nodos.o \
           $(BUILD_DIR)/jmn_conexiones.o \
           $(BUILD_DIR)/jmn_io.o \
           $(BUILD_DIR)/jmn_core.o \
           $(BUILD_DIR)/jmn_estructuras.o \
           $(BUILD_DIR)/jmn_utilidades.o \
           $(BUILD_DIR)/jmn_cognitivo.o \
           $(BUILD_DIR)/jmn_texto.o \
           $(BUILD_DIR)/jmn_compat.o

# VM Objects
VM_OBJS = $(BUILD_DIR)/vm.o \
          $(BUILD_DIR)/ir_format.o \
          $(BUILD_DIR)/reader_ir.o \
          $(BUILD_DIR)/optimizer_ir.o \
          $(BUILD_DIR)/ir_vm.o \
          $(BUILD_DIR)/cognitive_stubs.o

# Compiler Objects
COMPILER_OBJS = $(BUILD_DIR)/ir_compiler.o \
                $(BUILD_DIR)/codegen_ir.o \
                $(BUILD_DIR)/ir_format.o \
                $(BUILD_DIR)/optimizer_ir.o

# Targets
TARGET_VM = $(BIN_DIR)/jasboot-ir-vm.exe
TARGET_COMPILER = $(BIN_DIR)/jasboot-ir-compiler.exe

all: $(BUILD_DIR) $(BIN_DIR) $(TARGET_VM) $(TARGET_COMPILER)

$(BUILD_DIR):
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

$(BIN_DIR):
	@if not exist $(BIN_DIR) mkdir $(BIN_DIR)

# Link VM
$(TARGET_VM): $(VM_OBJS) $(JMN_OBJS)
	$(CC) $(VM_OBJS) $(JMN_OBJS) -o $@ $(LDFLAGS)

# Link Compiler
$(TARGET_COMPILER): $(COMPILER_OBJS)
	$(CC) $(COMPILER_OBJS) -o $@ $(LDFLAGS)

# Compile src/*.c -> build/*.o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile JMN sources (Nombres reales sin prefijo jmn_)
$(BUILD_DIR)/jmn_nodos.o: $(JMN_DIR)/memoria_neuronal_nodos.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_conexiones.o: $(JMN_DIR)/memoria_neuronal_conexiones.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_io.o: $(JMN_DIR)/memoria_neuronal_io.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_core.o: $(JMN_DIR)/memoria_neuronal_core.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_estructuras.o: $(JMN_DIR)/memoria_neuronal_estructuras.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_utilidades.o: $(JMN_DIR)/memoria_neuronal_utilidades.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_cognitivo.o: $(JMN_DIR)/memoria_neuronal_cognitivo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_texto.o: $(JMN_DIR)/memoria_neuronal_texto_fix.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/jmn_compat.o: $(JMN_PKG)/src/platform_compat.c
	$(CC) $(CFLAGS) -c $< -o $@ -I$(JMN_PKG)/src

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(BIN_DIR) rmdir /s /q $(BIN_DIR)

.PHONY: all clean
