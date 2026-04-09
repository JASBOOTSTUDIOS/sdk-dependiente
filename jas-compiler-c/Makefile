# Compilador Jasboot en C - Lexer
CC = gcc
CFLAGS = -std=c11 -Wall -Iinclude

SRC = src/keywords.c src/lexer.c src/main.c
OBJ = $(SRC:.c=.o)
TARGET = jbc-lexer

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
