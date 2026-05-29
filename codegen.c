/*
 * codegen.c - Generation de code assembleur i386 (32 bits, syntaxe AT&T).
 *
 * ======================================================================
 *  AVERTISSEMENT : ce module est HORS PERIMETRE du cahier des charges.
 *  Il a ete ecrit par curiosite, comme prolongement du chapitre 2
 *  (assembleur i386) et de l'exercice ppcm : au lieu d'evaluer l'AST, on
 *  emet un fichier .s compilable avec `gcc -m32`. Il ne gere QUE les
 *  entiers (les flottants de l'extension ne sont pas supportes ici) et
 *  ne reproduit pas la gestion "douce" de la division par zero de
 *  l'interpreteur (un `idiv` par zero leve un SIGFPE, comme en C).
 * ======================================================================
 *
 * Modele :
 *   - Convention d'appel cdecl 32 bits : arguments empiles de droite a
 *     gauche par l'appelant, resultat dans %eax, %ebp/%esp pour le cadre
 *     de pile (comme le %ebp du chapitre 2).
 *   - Variables du programme principal  -> globales en .bss (gv_<nom>).
 *   - Parametres et variables locales d'une fonction -> sur la pile, en
 *     deplacement par rapport a %ebp (params en +8/+12..., locales en
 *     -4/-8...).
 *   - Les expressions sont evaluees facon "machine a pile" : le resultat
 *     courant est dans %eax, les operandes intermediaires sont empiles.
 *
 * Usage :  ./ilang -s programme.ilang > programme.s
 *          gcc -m32 programme.s -o programme && ./programme
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

void generate_asm(Node *root);

/* ---------- Compteur de labels uniques ---------- */
static int lblctr = 0;
static int newlbl(void) { return lblctr++; }

/* ---------- Ensemble des variables globales rencontrees ---------- */
static char **gnames = NULL;
static int    gcount = 0;

static void add_global(const char *name)
{
    int i;
    for (i = 0; i < gcount; i++) {
        if (strcmp(gnames[i], name) == 0) {
            return;
        }
    }
    gnames = (char **)realloc(gnames, sizeof(char *) * (gcount + 1));
    gnames[gcount++] = strdup(name);
}

/* ---------- Table des locales de la fonction en cours ---------- */
static char **lnames = NULL;
static int   *loffs  = NULL;
static int    lcount = 0;

static void locals_reset(void)
{
    int i;
    for (i = 0; i < lcount; i++) {
        free(lnames[i]);
    }
    free(lnames);
    free(loffs);
    lnames = NULL;
    loffs = NULL;
    lcount = 0;
}

static void local_add(const char *name, int off)
{
    lnames = (char **)realloc(lnames, sizeof(char *) * (lcount + 1));
    loffs  = (int *)  realloc(loffs,  sizeof(int)    * (lcount + 1));
    lnames[lcount] = strdup(name);
    loffs[lcount]  = off;
    lcount++;
}

/* Renvoie 1 et place le deplacement si name est une locale, 0 sinon. */
static int local_off(const char *name, int *off)
{
    int i;
    for (i = 0; i < lcount; i++) {
        if (strcmp(lnames[i], name) == 0) {
            *off = loffs[i];
            return 1;
        }
    }
    return 0;
}

/* ---------- Pile des labels de boucle (pour arreter / continuer) ---------- */
static int brk_lbl[64];    /* cible de "arreter"   */
static int cont_lbl[64];   /* cible de "continuer" */
static int loop_sp = 0;

/* ---------- Collecte des variables affectees (pour les offsets) ---------- */
/* Ajoute a la table des locales tout nom affecte dans le sous-arbre, sans
 * descendre dans une declaration de fonction imbriquee. negidx suit le
 * prochain deplacement negatif disponible. */
static void collect_assigned(Node *n, int *negidx)
{
    int i;
    int dummy;

    if (n == NULL || n->type == NODE_FUNC_DECL) {
        return;
    }

    if (n->type == NODE_ASSIGN) {
        if (!local_off(n->name, &dummy)) {
            local_add(n->name, -4 * (++(*negidx)));
        }
    }

    collect_assigned(n->left, negidx);
    collect_assigned(n->right, negidx);
    collect_assigned(n->cond, negidx);
    collect_assigned(n->body, negidx);
    collect_assigned(n->elsebody, negidx);
    if (n->args != NULL) {
        for (i = 0; i < n->nargs; i++) {
            collect_assigned(n->args[i], negidx);
        }
    }
}

/* ---------- Generation des expressions (resultat dans %eax) ---------- */
static void gen_expr(FILE *out, Node *n);

static void gen_binop(FILE *out, Node *n)
{
    gen_expr(out, n->left);
    fprintf(out, "\tpushl %%eax\n");
    gen_expr(out, n->right);
    fprintf(out, "\tmovl %%eax, %%ecx\n");   /* ecx = operande droit */
    fprintf(out, "\tpopl %%eax\n");          /* eax = operande gauche */

    switch (n->op) {
    case OP_ADD: fprintf(out, "\taddl %%ecx, %%eax\n"); break;
    case OP_SUB: fprintf(out, "\tsubl %%ecx, %%eax\n"); break;
    case OP_MUL: fprintf(out, "\timull %%ecx, %%eax\n"); break;
    case OP_DIV: fprintf(out, "\tcltd\n\tidivl %%ecx\n"); break;        /* eax = quotient */
    case OP_MOD: fprintf(out, "\tcltd\n\tidivl %%ecx\n\tmovl %%edx, %%eax\n"); break;
    case OP_EQ:  fprintf(out, "\tcmpl %%ecx, %%eax\n\tsete %%al\n\tmovzbl %%al, %%eax\n"); break;
    case OP_NE:  fprintf(out, "\tcmpl %%ecx, %%eax\n\tsetne %%al\n\tmovzbl %%al, %%eax\n"); break;
    case OP_LT:  fprintf(out, "\tcmpl %%ecx, %%eax\n\tsetl %%al\n\tmovzbl %%al, %%eax\n"); break;
    case OP_GT:  fprintf(out, "\tcmpl %%ecx, %%eax\n\tsetg %%al\n\tmovzbl %%al, %%eax\n"); break;
    case OP_LE:  fprintf(out, "\tcmpl %%ecx, %%eax\n\tsetle %%al\n\tmovzbl %%al, %%eax\n"); break;
    case OP_GE:  fprintf(out, "\tcmpl %%ecx, %%eax\n\tsetge %%al\n\tmovzbl %%al, %%eax\n"); break;
    default:
        fprintf(stderr, "codegen: operateur binaire inconnu\n");
        exit(1);
    }
}

static void gen_call(FILE *out, Node *n)
{
    int i;
    /* Arguments empiles de droite a gauche (cdecl). */
    for (i = n->nargs - 1; i >= 0; i--) {
        gen_expr(out, n->args[i]);
        fprintf(out, "\tpushl %%eax\n");
    }
    fprintf(out, "\tcall il_%s\n", n->name);
    if (n->nargs > 0) {
        fprintf(out, "\taddl $%d, %%esp\n", 4 * n->nargs);
    }
    /* resultat deja dans %eax */
}

static void gen_expr(FILE *out, Node *n)
{
    int off;

    switch (n->type) {

    case NODE_NUM:
        if (n->num.type == VAL_FLOAT) {
            fprintf(stderr, "codegen: les flottants ne sont pas supportes en "
                            "mode assembleur (entiers uniquement)\n");
            exit(1);
        }
        fprintf(out, "\tmovl $%ld, %%eax\n", n->num.ival);
        break;

    case NODE_VAR:
        if (local_off(n->name, &off)) {
            fprintf(out, "\tmovl %d(%%ebp), %%eax\n", off);
        } else {
            add_global(n->name);
            fprintf(out, "\tmovl gv_%s, %%eax\n", n->name);
        }
        break;

    case NODE_BINOP:
        gen_binop(out, n);
        break;

    case NODE_UNOP:
        gen_expr(out, n->left);
        fprintf(out, "\tnegl %%eax\n");
        break;

    case NODE_FUNC_CALL:
        gen_call(out, n);
        break;

    case NODE_READ:
        fprintf(out, "\tpushl $gv__readtmp\n");
        fprintf(out, "\tpushl $.LCin\n");
        fprintf(out, "\tcall scanf\n");
        fprintf(out, "\taddl $8, %%esp\n");
        fprintf(out, "\tmovl gv__readtmp, %%eax\n");
        break;

    default:
        fprintf(stderr, "codegen: expression non supportee (type %d)\n",
                n->type);
        exit(1);
    }
}

/* ---------- Generation des instructions ---------- */
static void gen_stmt(FILE *out, Node *n);

static void store_var(FILE *out, const char *name)
{
    int off;
    if (local_off(name, &off)) {
        fprintf(out, "\tmovl %%eax, %d(%%ebp)\n", off);
    } else {
        add_global(name);
        fprintf(out, "\tmovl %%eax, gv_%s\n", name);
    }
}

static void gen_stmt(FILE *out, Node *n)
{
    int i;

    if (n == NULL) {
        return;
    }

    switch (n->type) {

    case NODE_BLOCK:
        for (i = 0; i < n->nargs; i++) {
            gen_stmt(out, n->args[i]);
        }
        break;

    case NODE_ASSIGN:
        gen_expr(out, n->left);
        store_var(out, n->name);
        break;

    case NODE_PRINT:
        gen_expr(out, n->left);
        fprintf(out, "\tpushl %%eax\n");
        fprintf(out, "\tpushl $.LCout\n");
        fprintf(out, "\tcall printf\n");
        fprintf(out, "\taddl $8, %%esp\n");
        break;

    case NODE_FUNC_CALL:
        gen_call(out, n);   /* resultat ignore */
        break;

    case NODE_RETURN:
        gen_expr(out, n->left);
        fprintf(out, "\tleave\n\tret\n");
        break;

    case NODE_IF: {
        int el = newlbl(), end = newlbl();
        gen_expr(out, n->cond);
        fprintf(out, "\tcmpl $0, %%eax\n\tjz .L%d\n", el);
        gen_stmt(out, n->body);
        fprintf(out, "\tjmp .L%d\n", end);
        fprintf(out, ".L%d:\n", el);
        gen_stmt(out, n->elsebody);
        fprintf(out, ".L%d:\n", end);
        break;
    }

    case NODE_WHILE: {
        int start = newlbl(), end = newlbl();
        fprintf(out, ".L%d:\n", start);
        gen_expr(out, n->cond);
        fprintf(out, "\tcmpl $0, %%eax\n\tjz .L%d\n", end);
        cont_lbl[loop_sp] = start;
        brk_lbl[loop_sp] = end;
        loop_sp++;
        gen_stmt(out, n->body);
        loop_sp--;
        fprintf(out, "\tjmp .L%d\n", start);
        fprintf(out, ".L%d:\n", end);
        break;
    }

    case NODE_FOR: {
        int start = newlbl(), cont = newlbl(), end = newlbl();
        gen_stmt(out, n->left);            /* initialisation */
        fprintf(out, ".L%d:\n", start);
        gen_expr(out, n->cond);
        fprintf(out, "\tcmpl $0, %%eax\n\tjz .L%d\n", end);
        cont_lbl[loop_sp] = cont;
        brk_lbl[loop_sp] = end;
        loop_sp++;
        gen_stmt(out, n->body);
        loop_sp--;
        fprintf(out, ".L%d:\n", cont);
        gen_stmt(out, n->right);           /* increment */
        fprintf(out, "\tjmp .L%d\n", start);
        fprintf(out, ".L%d:\n", end);
        break;
    }

    case NODE_BREAK:
        if (loop_sp == 0) {
            fprintf(stderr, "codegen: 'arreter' hors d'une boucle\n");
            exit(1);
        }
        fprintf(out, "\tjmp .L%d\n", brk_lbl[loop_sp - 1]);
        break;

    case NODE_CONTINUE:
        if (loop_sp == 0) {
            fprintf(stderr, "codegen: 'continuer' hors d'une boucle\n");
            exit(1);
        }
        fprintf(out, "\tjmp .L%d\n", cont_lbl[loop_sp - 1]);
        break;

    case NODE_FUNC_DECL:
        /* Les fonctions imbriquees ne sont pas gerees par le backend. */
        break;

    default:
        fprintf(stderr, "codegen: instruction non supportee (type %d)\n",
                n->type);
        exit(1);
    }
}

/* ---------- Generation d'une fonction ---------- */
static void gen_function(FILE *out, Node *decl)
{
    int i;
    int negidx = 0;
    int framesize;

    locals_reset();

    /* Parametres : deplacements positifs +8, +12, ... */
    for (i = 0; i < decl->nargs; i++) {
        local_add(decl->args[i]->name, 8 + 4 * i);
    }
    /* Variables locales affectees : deplacements negatifs -4, -8, ... */
    collect_assigned(decl->body, &negidx);

    framesize = 4 * negidx;
    framesize = (framesize + 15) & ~15;   /* arrondi a 16 (alignement) */

    fprintf(out, "\n\t.globl il_%s\n", decl->name);
    fprintf(out, "il_%s:\n", decl->name);
    fprintf(out, "\tpushl %%ebp\n\tmovl %%esp, %%ebp\n");
    if (framesize > 0) {
        fprintf(out, "\tsubl $%d, %%esp\n", framesize);
    }

    gen_stmt(out, decl->body);

    /* Retour par defaut (si la fonction ne fait pas de "retourner"). */
    fprintf(out, "\tmovl $0, %%eax\n\tleave\n\tret\n");

    locals_reset();
}

/* ---------- Point d'entree du backend ---------- */
void generate_asm(Node *root)
{
    FILE *out = stdout;
    int i;

    if (root == NULL || root->type != NODE_BLOCK) {
        return;
    }

    fprintf(out, "# Genere par ILANG (backend i386, hors perimetre du projet)\n");
    fprintf(out, "\t.text\n");

    /* 1) Les fonctions. */
    for (i = 0; i < root->nargs; i++) {
        if (root->args[i]->type == NODE_FUNC_DECL) {
            gen_function(out, root->args[i]);
        }
    }

    /* 2) Le programme principal -> fonction main (variables = globales). */
    locals_reset();   /* aucune locale : tout est global au niveau principal */
    fprintf(out, "\n\t.globl main\nmain:\n");
    fprintf(out, "\tpushl %%ebp\n\tmovl %%esp, %%ebp\n");
    fprintf(out, "\tandl $-16, %%esp\n");   /* alignement de pile (printf) */
    for (i = 0; i < root->nargs; i++) {
        if (root->args[i]->type != NODE_FUNC_DECL) {
            gen_stmt(out, root->args[i]);
        }
    }
    fprintf(out, "\tmovl $0, %%eax\n\tleave\n\tret\n");

    /* 3) Donnees : formats + variables globales. */
    fprintf(out, "\n\t.section .rodata\n");
    fprintf(out, ".LCout:\n\t.string \"%%d\\n\"\n");
    fprintf(out, ".LCin:\n\t.string \"%%d\"\n");

    fprintf(out, "\n\t.bss\n");
    fprintf(out, "\t.align 4\ngv__readtmp:\n\t.zero 4\n");
    for (i = 0; i < gcount; i++) {
        fprintf(out, "\t.align 4\ngv_%s:\n\t.zero 4\n", gnames[i]);
    }
}
