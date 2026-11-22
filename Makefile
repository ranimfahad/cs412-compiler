# Makefile for the MiniLang compiler (CS412 course project)
#
#   make        build the compiler (./minilang, or minilang.exe on Windows)
#   make test   build, then run every test in tests/ and compare outputs
#   make clean  remove everything the build created
#
# Works on Linux/macOS and on Windows with MSYS2 (flex, bison, gcc, make
# from the UCRT64 environment).

CC       = gcc
FLEX     = flex
BISON    = bison
CFLAGS   = -std=c11 -Wall -Wextra -g
CPPFLAGS = -Isrc -Ibuild
# -Wall: all Bison warnings. -Wcounterexamples: if the grammar ever has a
# conflict, show an example input that triggers it.
BISONFLAGS = -Wall -Wcounterexamples

# Windows executables need the .exe suffix, or make can't tell the
# target is already built and relinks it every time.
ifeq ($(OS),Windows_NT)
  EXE = .exe
endif

TARGET = minilang$(EXE)
BUILD  = build

# Object files: hand-written sources plus the generated scanner and parser.
OBJS = $(BUILD)/main.o $(BUILD)/tokens.o $(BUILD)/ast.o \
       $(BUILD)/lexer.o $(BUILD)/parser.o \
       $(BUILD)/symtab.o $(BUILD)/semantic.o \
       $(BUILD)/tac.o $(BUILD)/irgen.o \
       $(BUILD)/optimize.o $(BUILD)/interp.o

# Every object depends on all headers, including the generated parser.h
# (it defines the token codes). Simple and always correct for a small project.
HDRS = src/tokens.h src/lexer.h src/ast.h $(BUILD)/parser.h \
       src/symtab.h src/semantic.h src/tac.h src/irgen.h \
       src/optimize.h src/interp.h

.PHONY: all test expected clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Phase 1: Flex turns the lexer specification into C code.
$(BUILD)/lexer.c: src/lexer.l | $(BUILD)
	$(FLEX) -o $@ $<

# Phase 2: Bison turns the grammar into C code (parser.c) and a header with
# the token codes and value types (parser.h). Because of %expect 0 in
# parser.y, this step fails if the grammar has any conflicts.
$(BUILD)/parser.c: src/parser.y | $(BUILD)
	$(BISON) $(BISONFLAGS) --header=$(BUILD)/parser.h -o $@ $<

# parser.h is created by the same command as parser.c.
$(BUILD)/parser.h: $(BUILD)/parser.c
	@:

# The generated scanner contains some static helpers we never call.
$(BUILD)/lexer.o: $(BUILD)/lexer.c $(HDRS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -c -o $@ $<

$(BUILD)/parser.o: $(BUILD)/parser.c $(HDRS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

# Run all test programs and compare against tests/*/expected/*.out
test: $(TARGET)
	sh tests/run_tests.sh ./$(TARGET)

# Regenerate the expected outputs from the current compiler.
# Review the changes before keeping them!
expected: $(TARGET)
	sh tests/run_tests.sh ./$(TARGET) --update

clean:
	rm -rf $(BUILD) minilang minilang.exe tests/*/actual
