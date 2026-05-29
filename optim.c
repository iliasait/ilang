/*
 * optim.c - Optimisations sur l'AST (chapitre 8 du cours).
 *
 * Deux passes, appliquees avant l'evaluation et fusionnees ici en un seul
 * parcours recursif (un seul passage sur l'arbre) :
 *
 *   1. Pliage de constantes (constant folding) : un noeud BINOP dont les
 *      deux fils sont des litteraux est remplace par le litteral resultat.
 *      Exemple : 3 + 4 -> 7, ou 3.14 * 2 -> 6.28. Idem pour le moins
 *      unaire : -(5) -> -5.
 *
 *   2. Elimination du code mort (dead code elimination) : un NODE_IF dont
 *      la condition est un litteral faux (0 ou 0.0) est supprime ; il est
 *      remplace par sa branche "sinon" (ou par rien). Symetriquement, si la
 *      condition est un litteral vrai, on garde directement le "alors".
 *
 * optimize() renvoie le noeud (eventuellement remplace). Le code appelant
 * doit donc reaffecter :  racine = optimize(racine);
 *
 * Garde-fou : on NE plie PAS une division/modulo qui echouerait (par zero,
 * ou modulo sur flottants), afin de laisser l'evaluateur emettre le message
 * d'erreur attendu a l'execution.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

Node *optimize(Node *node);

static double as_double(Value v)
{
    return (v.type == VAL_INT) ? (double)v.ival : v.fval;
}

/* Peut-on plier ce BINOP a constantes sans provoquer d'erreur ? */
static int can_fold(int op, Value a, Value b)
{
    int both_int = (a.type == VAL_INT && b.type == VAL_INT);

    if (op == OP_DIV) {
        return both_int ? (b.ival != 0) : (as_double(b) != 0.0);
    }
    if (op == OP_MOD) {
        return both_int && (b.ival != 0);   /* modulo entiers uniquement */
    }
    return 1;
}

/* Calcule la valeur d'un BINOP a fils constants (sans verification : le
 * code appelant a deja teste can_fold). Memes regles que l'evaluateur. */
static Value do_fold(int op, Value a, Value b)
{
    int both_int = (a.type == VAL_INT && b.type == VAL_INT);

    switch (op) {
    case OP_ADD: return both_int ? make_int(a.ival + b.ival)
                                 : make_float(as_double(a) + as_double(b));
    case OP_SUB: return both_int ? make_int(a.ival - b.ival)
                                 : make_float(as_double(a) - as_double(b));
    case OP_MUL: return both_int ? make_int(a.ival * b.ival)
                                 : make_float(as_double(a) * as_double(b));
    case OP_DIV: return both_int ? make_int(a.ival / b.ival)
                                 : make_float(as_double(a) / as_double(b));
    case OP_MOD: return make_int(a.ival % b.ival);
    case OP_EQ:  return make_int(both_int ? (a.ival == b.ival)
                                          : (as_double(a) == as_double(b)));
    case OP_NE:  return make_int(both_int ? (a.ival != b.ival)
                                          : (as_double(a) != as_double(b)));
    case OP_LT:  return make_int(both_int ? (a.ival <  b.ival)
                                          : (as_double(a) <  as_double(b)));
    case OP_GT:  return make_int(both_int ? (a.ival >  b.ival)
                                          : (as_double(a) >  as_double(b)));
    case OP_LE:  return make_int(both_int ? (a.ival <= b.ival)
                                          : (as_double(a) <= as_double(b)));
    case OP_GE:  return make_int(both_int ? (a.ival >= b.ival)
                                          : (as_double(a) >= as_double(b)));
    default:     return make_int(0);   /* inatteignable */
    }
}

Node *optimize(Node *node)
{
    int i;

    if (node == NULL) {
        return NULL;
    }

    switch (node->type) {

    case NODE_NUM:
    case NODE_VAR:
    case NODE_READ:
    case NODE_BREAK:
    case NODE_CONTINUE:
        return node;   /* feuilles : rien a faire */

    case NODE_BINOP: {
        node->left  = optimize(node->left);
        node->right = optimize(node->right);

        if (node->left->type == NODE_NUM && node->right->type == NODE_NUM
            && can_fold(node->op, node->left->num, node->right->num)) {
            Value folded = do_fold(node->op, node->left->num, node->right->num);
            Node *n = (folded.type == VAL_INT) ? new_num(folded.ival)
                                               : new_fnum(folded.fval);
            n->line = node->line;
            free_node(node);   /* libere aussi les deux NODE_NUM fils */
            return n;
        }
        return node;
    }

    case NODE_UNOP:
        node->left = optimize(node->left);
        if (node->left->type == NODE_NUM) {
            Value c = node->left->num;
            Node *n = (c.type == VAL_INT) ? new_num(-c.ival)
                                          : new_fnum(-c.fval);
            n->line = node->line;
            free_node(node);
            return n;
        }
        return node;

    case NODE_ASSIGN:
        node->left = optimize(node->left);
        return node;

    case NODE_IF:
        node->cond     = optimize(node->cond);
        node->body     = optimize(node->body);
        node->elsebody = optimize(node->elsebody);

        if (node->cond->type == NODE_NUM) {
            Node *keep;
            if (!value_is_true(node->cond->num)) {
                /* Condition toujours fausse : on garde le "sinon". */
                keep = node->elsebody;
                node->elsebody = NULL;   /* detache pour ne pas le liberer */
            } else {
                /* Condition toujours vraie : on garde le "alors". */
                keep = node->body;
                node->body = NULL;
            }
            free_node(node);   /* libere la condition et la branche morte */
            return keep;       /* peut etre NULL (if sans sinon supprime) */
        }
        return node;

    case NODE_WHILE:
        node->cond = optimize(node->cond);
        node->body = optimize(node->body);
        return node;

    case NODE_FOR:
        node->left  = optimize(node->left);
        node->cond  = optimize(node->cond);
        node->right = optimize(node->right);
        node->body  = optimize(node->body);
        return node;

    case NODE_FUNC_DECL:
        node->body = optimize(node->body);
        return node;

    case NODE_FUNC_CALL:
        for (i = 0; i < node->nargs; i++) {
            node->args[i] = optimize(node->args[i]);
        }
        return node;

    case NODE_PRINT:
    case NODE_RETURN:
        node->left = optimize(node->left);
        return node;

    case NODE_BLOCK: {
        /* On optimise chaque instruction ; certaines (NODE_IF mort sans
         * sinon) peuvent devenir NULL : on les retire alors du bloc. */
        int w = 0;   /* indice d'ecriture (compactage) */
        for (i = 0; i < node->nargs; i++) {
            Node *opt = optimize(node->args[i]);
            if (opt != NULL) {
                node->args[w++] = opt;
            }
        }
        node->nargs = w;
        return node;
    }

    default:
        return node;
    }
}
