/*
 * symtab.c - Implementation de la table des symboles ILANG.
 *
 * Voir symtab.h pour le modele d'ensemble (pile de frames, chaque frame
 * etant une table de hachage ; table des fonctions globale separee).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"

/* Sommet de la pile d'environnements (frame courant). */
static Env *current_env = NULL;

/* Base de la pile : l'environnement global du programme principal. Il sert de
 * portee de repli pour la resolution des variables (voir env_get). */
static Env *global_env = NULL;

/* Tete de la liste chainee des fonctions definies (table globale). */
typedef struct FuncEntry {
    char *name;
    Node *decl;
    struct FuncEntry *next;
} FuncEntry;

static FuncEntry *func_list = NULL;

/* Fonction de hachage djb2 (Dan Bernstein), classique et efficace. */
static unsigned int hash(const char *s)
{
    unsigned int h = 5381;
    int c;
    while ((c = (unsigned char)*s++) != 0) {
        h = ((h << 5) + h) + c;   /* h * 33 + c */
    }
    return h % HASH_SIZE;
}

void env_init(void)
{
    current_env = (Env *)calloc(1, sizeof(Env));
    if (current_env == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (env_init)\n");
        exit(1);
    }
    current_env->parent = NULL;   /* base de la pile = environnement global */
    global_env = current_env;     /* memorise la base pour la portee lexicale */
}

void env_push(void)
{
    Env *e = (Env *)calloc(1, sizeof(Env));
    if (e == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (env_push)\n");
        exit(1);
    }
    e->parent = current_env;
    current_env = e;
}

void env_pop(void)
{
    Env *old = current_env;
    int i;

    if (old == NULL) {
        return;
    }

    /* Liberer toutes les variables de ce frame. */
    for (i = 0; i < HASH_SIZE; i++) {
        Symbol *s = old->buckets[i];
        while (s != NULL) {
            Symbol *next = s->next;
            free(s->name);
            free(s);
            s = next;
        }
    }

    current_env = old->parent;
    free(old);
}

/* Cherche un nom dans un frame donne uniquement. Renvoie 1 si trouve. */
static int lookup_in(Env *e, const char *name, Value *out)
{
    Symbol *s;
    if (e == NULL) {
        return 0;
    }
    for (s = e->buckets[hash(name)]; s != NULL; s = s->next) {
        if (strcmp(s->name, name) == 0) {
            *out = s->value;
            return 1;
        }
    }
    return 0;
}

int env_get(const char *name, Value *out)
{
    /* Portee LEXICALE : on cherche d'abord dans le frame courant (les locales
     * de la fonction en cours, ou les globales si l'on est dans le programme
     * principal), puis en repli dans l'environnement global. On ne consulte
     * PAS les frames intermediaires de la pile d'appel : une fonction ne voit
     * donc jamais les variables locales de son appelant. */
    if (lookup_in(current_env, name, out)) {
        return 1;
    }
    if (current_env != global_env && lookup_in(global_env, name, out)) {
        return 1;
    }
    return 0;
}

void env_set(const char *name, Value value)
{
    unsigned int idx = hash(name);
    Symbol *s = current_env->buckets[idx];

    /* La variable existe-t-elle deja dans le frame courant ? */
    while (s != NULL) {
        if (strcmp(s->name, name) == 0) {
            s->value = value;   /* mise a jour */
            return;
        }
        s = s->next;
    }

    /* Sinon, creation dans le frame courant (variable locale/globale). */
    s = (Symbol *)malloc(sizeof(Symbol));
    if (s == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (env_set)\n");
        exit(1);
    }
    s->name = strdup(name);
    s->value = value;
    s->next = current_env->buckets[idx];
    current_env->buckets[idx] = s;
}

void func_define(Node *decl)
{
    FuncEntry *f;

    /* Redefinition : on remplace simplement le corps existant. */
    for (f = func_list; f != NULL; f = f->next) {
        if (strcmp(f->name, decl->name) == 0) {
            f->decl = decl;
            return;
        }
    }

    f = (FuncEntry *)malloc(sizeof(FuncEntry));
    if (f == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (func_define)\n");
        exit(1);
    }
    f->name = strdup(decl->name);
    f->decl = decl;
    f->next = func_list;
    func_list = f;
}

Node *func_lookup(const char *name)
{
    FuncEntry *f;
    for (f = func_list; f != NULL; f = f->next) {
        if (strcmp(f->name, name) == 0) {
            return f->decl;
        }
    }
    return NULL;
}

void symtab_free(void)
{
    FuncEntry *f;

    /* Depiler tous les frames restants (normalement il ne reste que le
     * global, mais on est robuste). */
    while (current_env != NULL) {
        env_pop();
    }

    /* Liberer la table des fonctions (les Node sont liberes via free_node
     * sur la racine de l'AST, on ne libere ici que les entrees). */
    f = func_list;
    while (f != NULL) {
        FuncEntry *next = f->next;
        free(f->name);
        free(f);
        f = next;
    }
    func_list = NULL;
}
