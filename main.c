/*
 * main.c - Point d'entree de l'interpreteur ILANG.
 *
 * Chaine de traitement (cf. cahier des charges, section 3.1) :
 *
 *   source ILANG -> [Flex] tokens -> [Bison] AST -> [optim] AST -> [eval]
 *
 * Usage :
 *   ./ilang fichier.ilang     execute un fichier source
 *   ./ilang                   mode interactif : lit le programme sur stdin
 *   ./ilang -s fichier.ilang  genere l'assembleur i386 (extension hors-scope)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "symtab.h"

/* Fournis par Bison / Flex. */
extern int yyparse(void);
extern int yylex_destroy(void);   /* libere le buffer interne de Flex */
extern FILE *yyin;
extern Node *program_root;

/* Fournis par eval.c, optim.c et codegen.c. */
void eval_program(Node *root);
Node *optimize(Node *node);
void generate_asm(Node *root);    /* backend assembleur (hors perimetre) */

int main(int argc, char **argv)
{
    int parse_status;
    int asm_mode = 0;        /* 1 = generer de l'assembleur (option -s)     */
    int no_opt = 0;          /* 1 = desactiver les optimisations (--no-opt) */
    const char *path = NULL; /* fichier source, NULL = entree standard      */
    int i;

    /* Analyse des arguments : les options commencent par '-', le premier
     * argument restant est le fichier source. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            asm_mode = 1;
        } else if (strcmp(argv[i], "--no-opt") == 0) {
            no_opt = 1;
        } else if (path == NULL) {
            path = argv[i];
        }
    }

    /* Choix de la source : fichier ou entree standard (mode interactif). */
    if (path != NULL) {
        yyin = fopen(path, "r");
        if (yyin == NULL) {
            fprintf(stderr, "Erreur : impossible d'ouvrir le fichier '%s'\n",
                    path);
            return 1;
        }
    } else {
        if (!asm_mode) {
            printf("ILANG - interpreteur (mode interactif)\n");
            printf("Saisissez votre programme, puis Ctrl-D pour l'executer.\n");
        }
        yyin = stdin;
    }

    /* Initialisation de l'environnement global. */
    env_init();

    /* Analyse lexicale + syntaxique : construit l'AST dans program_root. */
    parse_status = yyparse();

    if (parse_status == 0 && program_root != NULL) {
        /* Optimisations sur l'AST (pliage de constantes, code mort).
         * Desactivables via --no-opt (utile pour mesurer leur effet). */
        if (!no_opt) {
            program_root = optimize(program_root);
        }

        if (asm_mode) {
            /* Backend : on emet l'assembleur sur la sortie standard. */
            generate_asm(program_root);
        } else {
            /* Evaluation. */
            eval_program(program_root);
        }
    }

    /* Nettoyage. */
    if (program_root != NULL) {
        free_node(program_root);
    }
    symtab_free();
    if (yyin != NULL && yyin != stdin) {
        fclose(yyin);
    }
    yylex_destroy();

    return parse_status;
}
