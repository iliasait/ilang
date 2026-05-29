/*
 * ilang.l - Analyseur lexical (Flex) du langage ILANG.
 *
 * Reconnait les nombres, identifiants, mots-cles, operateurs et delimiteurs,
 * et ignore les espaces et les commentaires (ligne // et bloc / * ... * /).
 * Les tokens sont ceux declares par Bison dans ilang.tab.h.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "ilang.tab.h"   /* genere par bison : definitions des tokens + yylval */
%}

/* yylineno : numero de ligne automatique (utilise pour les messages).
 * noyywrap : pas de fonction yywrap(), un seul fichier d'entree.
 * nounput / noinput : evite des warnings sur des fonctions non utilisees. */
%option yylineno noyywrap nounput noinput

/* Etat exclusif pour les commentaires de bloc. */
%x COMMENT

CHIFFRE     [0-9]
LETTRE      [a-zA-Z_]
ID          {LETTRE}({LETTRE}|{CHIFFRE})*

%%

    /* ---- Espaces (ignores) ---- */
[ \t\r]+            { /* rien */ }
\n                  { /* yylineno gere par %option yylineno */ }

    /* ---- Commentaires ---- */
"//"[^\n]*          { /* commentaire de ligne ignore */ }
"/*"                { BEGIN(COMMENT); }
<COMMENT>"*/"       { BEGIN(INITIAL); }
<COMMENT>\n         { /* compte la ligne, contenu ignore */ }
<COMMENT>.          { /* contenu du commentaire ignore */ }
<COMMENT><<EOF>>    { fprintf(stderr,
                        "Erreur lexicale ligne %d : commentaire de bloc "
                        "non termine\n", yylineno);
                      exit(1); }

    /* ---- Mots-cles ---- */
    /* "tant que" comporte un espace : on l'attrape avant l'identifiant. */
"tant"[ \t]+"que"   { return TANTQUE; }
"si"                { return SI; }
"sinon"             { return SINON; }
"pour"              { return POUR; }
"fonction"          { return FONCTION; }
"retourner"         { return RETOURNER; }
"afficher"          { return AFFICHER; }
"lire"              { return LIRE; }
    /* Ruptures de boucle. "arreter" accepte avec ou sans accent circonflexe
     * (le fichier source peut etre en UTF-8 ou en ASCII). */
"arr"("ê"|"e")"ter" { return ARRETER; }
"continuer"         { return CONTINUER; }

    /* ---- Operateurs de comparaison (avant les operateurs simples) ---- */
"=="                { return EQ; }
"!="                { return NE; }
"<="                { return LE; }
">="                { return GE; }
"<"                 { return LT; }
">"                 { return GT; }

    /* ---- Operateurs arithmetiques et delimiteurs (tokens char) ---- */
"+"                 { return '+'; }
"-"                 { return '-'; }
"*"                 { return '*'; }
"/"                 { return '/'; }
"%"                 { return '%'; }
"="                 { return '='; }
"("                 { return '('; }
")"                 { return ')'; }
"{"                 { return '{'; }
"}"                 { return '}'; }
";"                 { return ';'; }
","                 { return ','; }

    /* ---- Litteraux et identifiants ---- */
    /* Le flottant est teste avant l'entier. Formes acceptees : 3.14, 3.
     * (point final) et .5 (point initial). */
({CHIFFRE}+"."{CHIFFRE}*)|("."{CHIFFRE}+) { yylval.fnum = strtod(yytext, NULL); return FLOTTANT; }
{CHIFFRE}+          { yylval.num = strtol(yytext, NULL, 10); return NOMBRE; }
{ID}                { yylval.str = strdup(yytext); return IDENTIFIANT; }

    /* ---- Caractere invalide ---- */
.                   { fprintf(stderr,
                        "Erreur lexicale ligne %d : caractere invalide "
                        "'%s'\n", yylineno, yytext);
                      exit(1); }

%%
