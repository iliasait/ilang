# Makefile - Interpreteur ILANG
#
# Cibles :
#   make        compile l'interpreteur (binaire ./ilang)
#   make clean  supprime les fichiers generes
#   make test   compile puis execute les programmes de demonstration
#
# Les fichiers lex.yy.c (Flex) et ilang.tab.c/.h (Bison) sont generes
# automatiquement a partir de ilang.l et ilang.y.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -g
LEX     = flex
YACC    = bison

# Code que nous ecrivons : compile avec tous les avertissements actives.
SRC     = ast.c symtab.c eval.c optim.c codegen.c main.c
OBJ     = $(SRC:.c=.o)

# Code genere (Flex/Bison) : compile sans -Wextra pour eviter le bruit des
# avertissements propres au code machine-genere (zero warning de notre cote).
GENOBJ  = ilang.tab.o lex.yy.o
GENFLAGS = -std=gnu11 -g

BIN     = ilang

.PHONY: all clean test asm

all: $(BIN)

$(BIN): $(OBJ) $(GENOBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(GENOBJ)

# --- Bison : grammaire -> parseur + en-tete des tokens ---
ilang.tab.c ilang.tab.h: ilang.y
	$(YACC) -d -o ilang.tab.c ilang.y

# --- Flex : regles -> analyseur lexical (depend de l'en-tete des tokens) ---
lex.yy.c: ilang.l ilang.tab.h
	$(LEX) -o lex.yy.c ilang.l

# --- Compilation du code genere ---
ilang.tab.o: ilang.tab.c ast.h
	$(CC) $(GENFLAGS) -c ilang.tab.c -o ilang.tab.o

lex.yy.o: lex.yy.c ilang.tab.h ast.h
	$(CC) $(GENFLAGS) -c lex.yy.c -o lex.yy.o

# --- Compilation de notre code ---
ast.o:    ast.c ast.h
symtab.o: symtab.c symtab.h ast.h
eval.o:   eval.c ast.h symtab.h
optim.o:  optim.c ast.h
codegen.o: codegen.c ast.h
main.o:   main.c ast.h symtab.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(BIN)
	@echo "=== Fibonacci ==="
	./$(BIN) programmes/fibonacci.ilang
	@echo "=== PGCD ==="
	./$(BIN) programmes/pgcd.ilang
	@echo "=== Interactif (entree = 7) ==="
	@echo 7 | ./$(BIN) programmes/interactif.ilang
	@echo "=== Flottants ==="
	./$(BIN) programmes/flottants.ilang
	@echo "=== Boucles (arreter / continuer) ==="
	./$(BIN) programmes/boucles.ilang

# Demonstration du backend assembleur (extension hors-perimetre, entiers).
# Necessite gcc-multilib (paquet pour compiler en 32 bits : gcc -m32).
asm: $(BIN)
	@echo "=== Generation assembleur : pgcd.ilang -> pgcd.s ==="
	./$(BIN) -s programmes/pgcd.ilang > pgcd.s
	gcc -m32 -no-pie pgcd.s -o pgcd_asm
	@echo "=== Execution du binaire assemble ==="
	./pgcd_asm

clean:
	rm -f $(OBJ) $(GENOBJ) $(BIN) ilang.tab.c ilang.tab.h lex.yy.c pgcd.s pgcd_asm
