/*
 * ilang.y - Analyseur syntaxique (Bison) du langage ILANG.
 *
 * Grammaire LALR(1) qui construit l'arbre syntaxique abstrait (AST) via les
 * constructeurs definis dans ast.c. La precedence des operateurs est fixee
 * par les directives %left / %right (section 2.3 du cahier des charges).
 *
 * Note sur le "dangling else" : contrairement au cas classique du cours, il
 * n'y a ici AUCUN conflit shift/reduce. La raison est que les corps de SI
 * sont obligatoirement entre accolades (regle "bloc"). Un SINON est donc
 * toujours precede d'un '}', si bien que l'etat d'analyse n'est jamais
 * ambigu : SINON n'appartient pas a l'ensemble FOLLOW de la reduction du SI
 * sans sinon. La grammaire se compile sans conflit, sans aucune directive de
 * precedence dediee (verifie en retirant toute resolution : bison reste muet).
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* Fournis par Flex / cette unite. */
int yylex(void);
void yyerror(const char *s);
extern int yylineno;

/* Racine de l'AST, renseignee par la regle "programme" et lue par main.c. */
Node *program_root = NULL;
%}

/* Le type des valeurs semantiques utilise Node* et NodeList* : ast.h doit
 * donc etre inclus dans l'en-tete genere (ilang.tab.h) avant l'union. */
%code requires {
    #include "ast.h"
}

%union {
    long num;
    double fnum;
    char *str;
    Node *node;
    NodeList *list;
}

/* Messages d'erreur detailles (token attendu vs recu). */
%define parse.error verbose

/* ---- Tokens ---- */
%token <num>  NOMBRE
%token <fnum> FLOTTANT
%token <str>  IDENTIFIANT
%token SI SINON TANTQUE POUR FONCTION RETOURNER AFFICHER LIRE
%token ARRETER CONTINUER
%token EQ NE LT GT LE GE

/* ---- Types des non-terminaux ---- */
%type <node> instruction expression bloc liste_instructions
%type <node> affectation si_alors tant_que pour
%type <node> declaration_fonction appel_fonction
%type <list> liste_params liste_args params_nonvide args_nonvide

/* ---- Precedences (de la plus faible a la plus forte) ---- */
%left EQ NE LT GT LE GE
%left '+' '-'
%left '*' '/' '%'
%right UMINUS           /* moins unaire */

%start programme

%%

programme
    : liste_instructions                { program_root = $1; }
    ;

liste_instructions
    : /* vide */                        { $$ = new_block(); }
    | liste_instructions instruction    { block_add($1, $2); $$ = $1; }
    ;

bloc
    : '{' liste_instructions '}'        { $$ = $2; }
    ;

instruction
    : affectation                       { $$ = $1; }
    | si_alors                          { $$ = $1; }
    | tant_que                          { $$ = $1; }
    | pour                              { $$ = $1; }
    | declaration_fonction              { $$ = $1; }
    | appel_fonction                    { $$ = $1; }
    | AFFICHER '(' expression ')'       { $$ = new_print($3); }
    | RETOURNER '(' expression ')'      { $$ = new_return($3); }
    | ARRETER                           { $$ = new_break(); }
    | CONTINUER                         { $$ = new_continue(); }
    ;

affectation
    : IDENTIFIANT '=' expression        { $$ = new_assign($1, $3); }
    ;

si_alors
    : SI '(' expression ')' bloc
        { $$ = new_if($3, $5, NULL); }
    | SI '(' expression ')' bloc SINON bloc
        { $$ = new_if($3, $5, $7); }
    ;

tant_que
    : TANTQUE '(' expression ')' bloc   { $$ = new_while($3, $5); }
    ;

pour
    : POUR '(' affectation ';' expression ';' affectation ')' bloc
        { $$ = new_for($3, $5, $7, $9); }
    ;

declaration_fonction
    : FONCTION IDENTIFIANT '(' liste_params ')' bloc
        {
            $$ = new_func_decl($2, $4->items, $4->count, $6);
            free($4);   /* le tableau items est transfere au noeud */
        }
    ;

appel_fonction
    : IDENTIFIANT '(' liste_args ')'
        {
            $$ = new_func_call($1, $3->items, $3->count);
            free($3);
        }
    ;

liste_params
    : /* vide */                        { $$ = nodelist_new(); }
    | params_nonvide                    { $$ = $1; }
    ;

params_nonvide
    : IDENTIFIANT
        { $$ = nodelist_new(); nodelist_add($$, new_var($1)); }
    | params_nonvide ',' IDENTIFIANT
        { nodelist_add($1, new_var($3)); $$ = $1; }
    ;

liste_args
    : /* vide */                        { $$ = nodelist_new(); }
    | args_nonvide                      { $$ = $1; }
    ;

args_nonvide
    : expression
        { $$ = nodelist_new(); nodelist_add($$, $1); }
    | args_nonvide ',' expression
        { nodelist_add($1, $3); $$ = $1; }
    ;

expression
    : expression '+' expression         { $$ = new_binop(OP_ADD, $1, $3); }
    | expression '-' expression         { $$ = new_binop(OP_SUB, $1, $3); }
    | expression '*' expression         { $$ = new_binop(OP_MUL, $1, $3); }
    | expression '/' expression         { $$ = new_binop(OP_DIV, $1, $3); }
    | expression '%' expression         { $$ = new_binop(OP_MOD, $1, $3); }
    | expression EQ expression          { $$ = new_binop(OP_EQ, $1, $3); }
    | expression NE expression          { $$ = new_binop(OP_NE, $1, $3); }
    | expression LT expression          { $$ = new_binop(OP_LT, $1, $3); }
    | expression GT expression          { $$ = new_binop(OP_GT, $1, $3); }
    | expression LE expression          { $$ = new_binop(OP_LE, $1, $3); }
    | expression GE expression          { $$ = new_binop(OP_GE, $1, $3); }
    | '-' expression %prec UMINUS       { $$ = new_unop(OP_NEG, $2); }
    | '(' expression ')'                { $$ = $2; }
    | NOMBRE                            { $$ = new_num($1); }
    | FLOTTANT                          { $$ = new_fnum($1); }
    | IDENTIFIANT                       { $$ = new_var($1); }
    | appel_fonction                    { $$ = $1; }
    | LIRE '(' ')'                      { $$ = new_read(); }
    ;

%%

void yyerror(const char *s)
{
    fprintf(stderr, "Erreur de syntaxe ligne %d : %s\n", yylineno, s);
}
