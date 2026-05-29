#ifndef SYMTAB_H
#define SYMTAB_H

/*
 * symtab.h - Table des symboles d'ILANG.
 *
 * Conformement au cahier des charges, la table des symboles combine deux
 * idees :
 *   - une TABLE DE HACHAGE pour stocker les variables (section 2.2) ;
 *   - une PILE D'ENVIRONNEMENTS (frames) pour gerer la portee locale des
 *     fonctions, a la maniere de la pile %ebp en assembleur (section 3.6).
 *
 * Chaque frame de la pile est une table de hachage independante. On empile
 * un frame a l'entree d'une fonction, on le depile a la sortie. La base de
 * la pile est l'environnement global du programme principal. Le champ parent
 * sert de lien de controle (restauration du frame courant a la sortie de
 * fonction) ; la resolution des variables, elle, est LEXICALE : frame courant
 * puis environnement global, sans passer par les frames intermediaires (voir
 * env_get).
 *
 * Les fonctions, elles, sont globales : elles sont rangees dans une table
 * separee (func table) accessible depuis n'importe quel frame.
 */

#include "ast.h"

#define HASH_SIZE 211   /* nombre premier : limite les collisions */

/* Une entree (variable) dans une table de hachage. */
typedef struct Symbol {
    char *name;
    Value value;           /* entier ou flottant */
    struct Symbol *next;   /* chainage en cas de collision */
} Symbol;

/* Un environnement = un frame de la pile = une table de hachage. */
typedef struct Env {
    Symbol *buckets[HASH_SIZE];
    struct Env *parent;    /* frame precedent dans la pile (NULL = global) */
} Env;

/* ---- Gestion de la pile d'environnements ---- */

void env_init(void);          /* cree l'environnement global */
void env_push(void);          /* empile un nouveau frame (entree fonction) */
void env_pop(void);           /* depile le frame courant (sortie fonction) */

/* Recherche une variable (portee lexicale). Renvoie 1 si trouvee (et place la
 * valeur dans *out), 0 sinon. La recherche regarde le frame courant puis, en
 * repli, l'environnement global ; elle ignore les frames intermediaires de la
 * pile d'appel (une fonction ne voit pas les locales de son appelant). */
int env_get(const char *name, Value *out);

/* Affecte une variable dans le frame COURANT (sommet de pile).
 * Cree la variable si elle n'existe pas encore dans ce frame : c'est ce
 * qui rend les variables locales aux fonctions et globales au programme
 * principal (dont le frame courant est l'environnement global). */
void env_set(const char *name, Value value);

/* ---- Gestion de la table des fonctions (globale) ---- */

void func_define(Node *decl);          /* enregistre une NODE_FUNC_DECL */
Node *func_lookup(const char *name);   /* NULL si fonction inconnue */

/* Liberation complete (variables + fonctions). */
void symtab_free(void);

#endif /* SYMTAB_H */
