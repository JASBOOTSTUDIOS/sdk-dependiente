# Compilador Jasboot en C
CC = gcc
CFLAGS = -std=c11 -Wall -Iinclude -DJASBOOT_LANG_INTEGRATION

SRC = src/keywords.c \
      src/lexer.c \
      src/main.c \
      src/parser.c \
      src/nodes.c \
      src/codegen.c \
      src/diagnostic.c \
      src/resolve.c \
      src/symbol_table.c \
      src/token_vec.c \
      src/sistema_llamadas.c \
      src/jbc_ir_opt.c \
      src/win_spawn.c

OBJ = $(SRC:.c=.o)
TARGET = jbc.exe

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
