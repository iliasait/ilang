/*
 * eval.c - Evaluateur recursif de l'AST ILANG.
 *
 * L'evaluateur parcourt l'arbre en profondeur :
 *   - post-order pour les expressions (on evalue les fils avant le noeud) ;
 *   - pre-order pour les structures de controle (on teste la condition puis
 *     on descend dans le corps).
 *
 * Valeurs : chaque expression s'evalue en une Value etiquetee entier/flottant
 * (cf. ast.h). Les operations entre deux entiers restent entieres (division
 * entiere, modulo) ; des qu'un flottant intervient, le calcul bascule en
 * flottant. Le modulo n'est defini que sur les entiers.
 *
 * Controle de flot non local : trois signaux globaux portent les ruptures
 * de flot :
 *   - return_flag / return_value : "retourner" dans une fonction ;
 *   - loop_signal : "arreter" (break) et "continuer" (continue) dans une
 *     boucle.
 * Un bloc arrete d'executer ses instructions des qu'un de ces signaux est
 * leve ; les boucles consomment loop_signal, l'appel de fonction consomme
 * return_flag.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ast.h"
#include "symtab.h"

/* Prototypes publics (appeles par main.c). */
Value eval(Node *node);
void eval_program(Node *root);

/* Retour de fonction. */
static int return_flag = 0;
static Value return_value;

/* Rupture de boucle. */
typedef enum { SIG_NONE, SIG_BREAK, SIG_CONTINUE } LoopSignal;
static LoopSignal loop_signal = SIG_NONE;

/* Profondeur de boucle dans la fonction courante : permet de detecter un
 * "arreter"/"continuer" utilise hors d'une boucle. Remis a zero a l'entree
 * de chaque fonction (une boucle de l'appelant ne "couvre" pas l'appele). */
static int loop_depth = 0;

/* Conversion utilitaire vers double (pour les calculs mixtes). */
static double as_double(Value v)
{
    return (v.type == VAL_INT) ? (double)v.ival : v.fval;
}

/* Evaluation d'une operation binaire. */
static Value eval_binop(Node *node)
{
    Value a = eval(node->left);
    Value b = eval(node->right);
    int both_int = (a.type == VAL_INT && b.type == VAL_INT);

    switch (node->op) {

    /* --- Arithmetique --- */
    case OP_ADD:
        return both_int ? make_int(a.ival + b.ival)
                        : make_float(as_double(a) + as_double(b));
    case OP_SUB:
        return both_int ? make_int(a.ival - b.ival)
                        : make_float(as_double(a) - as_double(b));
    case OP_MUL:
        return both_int ? make_int(a.ival * b.ival)
                        : make_float(as_double(a) * as_double(b));
    case OP_DIV:
        if (both_int) {
            if (b.ival == 0) {
                fprintf(stderr, "Erreur ligne %d : division par zero\n",
                        node->line);
                return make_int(0);
            }
            return make_int(a.ival / b.ival);
        } else {
            double db = as_double(b);
            if (db == 0.0) {
                fprintf(stderr, "Erreur ligne %d : division par zero\n",
                        node->line);
                return make_float(0.0);
            }
            return make_float(as_double(a) / db);
        }
    case OP_MOD:
        if (!both_int) {
            fprintf(stderr, "Erreur ligne %d : modulo non defini sur les "
                            "flottants\n", node->line);
            return make_int(0);
        }
        if (b.ival == 0) {
            fprintf(stderr, "Erreur ligne %d : modulo par zero\n", node->line);
            return make_int(0);
        }
        return make_int(a.ival % b.ival);

    /* --- Comparaisons (resultat toujours entier 0/1) --- */
    case OP_EQ:
        return make_int(both_int ? (a.ival == b.ival)
                                 : (as_double(a) == as_double(b)));
    case OP_NE:
        return make_int(both_int ? (a.ival != b.ival)
                                 : (as_double(a) != as_double(b)));
    case OP_LT:
        return make_int(both_int ? (a.ival < b.ival)
                                 : (as_double(a) < as_double(b)));
    case OP_GT:
        return make_int(both_int ? (a.ival > b.ival)
                                 : (as_double(a) > as_double(b)));
    case OP_LE:
        return make_int(both_int ? (a.ival <= b.ival)
                                 : (as_double(a) <= as_double(b)));
    case OP_GE:
        return make_int(both_int ? (a.ival >= b.ival)
                                 : (as_double(a) >= as_double(b)));

    default:
        fprintf(stderr, "Erreur interne : operateur binaire inconnu (%d)\n",
                node->op);
        exit(1);
    }
}

/* Evaluation d'un appel de fonction (avec gestion de la pile de frames). */
static Value eval_func_call(Node *node)
{
    Node *decl = func_lookup(node->name);
    Value *argvals;
    Value result;
    int i;

    if (decl == NULL) {
        fprintf(stderr, "Erreur ligne %d : fonction '%s' non definie\n",
                node->line, node->name);
        exit(1);
    }

    if (node->nargs != decl->nargs) {
        fprintf(stderr,
                "Erreur ligne %d : fonction '%s' attend %d argument(s), "
                "%d fourni(s)\n",
                node->line, node->name, decl->nargs, node->nargs);
        exit(1);
    }

    /* 1) Evaluer les arguments dans l'environnement de l'APPELANT. */
    argvals = NULL;
    if (node->nargs > 0) {
        argvals = (Value *)malloc(sizeof(Value) * node->nargs);
        if (argvals == NULL) {
            fprintf(stderr, "Erreur fatale : memoire insuffisante (appel)\n");
            exit(1);
        }
        for (i = 0; i < node->nargs; i++) {
            argvals[i] = eval(node->args[i]);
        }
    }

    /* 2) Empiler un nouveau frame et y lier les parametres. */
    env_push();
    for (i = 0; i < decl->nargs; i++) {
        env_set(decl->args[i]->name, argvals[i]);
    }
    free(argvals);

    /* 3) Evaluer le corps. Le drapeau de retour est remis a zero juste
     *    avant, puis consomme juste apres. La profondeur de boucle est
     *    sauvegardee puis remise a zero : un "arreter" dans cette fonction
     *    ne doit pas s'appliquer a une boucle de l'appelant. */
    {
        int saved_depth = loop_depth;
        loop_depth = 0;
        return_flag = 0;
        eval(decl->body);
        result = return_flag ? return_value : make_int(0);
        return_flag = 0;
        loop_depth = saved_depth;
    }

    /* 4) Depiler le frame (les variables locales disparaissent). */
    env_pop();

    return result;
}

Value eval(Node *node)
{
    Value v;
    int i;

    if (node == NULL) {
        return make_int(0);
    }

    switch (node->type) {

    case NODE_NUM:
        return node->num;

    case NODE_VAR:
        if (!env_get(node->name, &v)) {
            fprintf(stderr, "Erreur ligne %d : variable '%s' non definie\n",
                    node->line, node->name);
            exit(1);
        }
        return v;

    case NODE_BINOP:
        return eval_binop(node);

    case NODE_UNOP:
        /* Seul le moins unaire existe. */
        v = eval(node->left);
        return (v.type == VAL_INT) ? make_int(-v.ival) : make_float(-v.fval);

    case NODE_ASSIGN:
        v = eval(node->left);
        env_set(node->name, v);
        return v;

    case NODE_IF:
        if (value_is_true(eval(node->cond))) {
            eval(node->body);
        } else if (node->elsebody != NULL) {
            eval(node->elsebody);
        }
        return make_int(0);

    case NODE_WHILE:
        loop_depth++;
        while (value_is_true(eval(node->cond))) {
            eval(node->body);
            if (return_flag) {
                break;
            }
            if (loop_signal == SIG_BREAK) {
                loop_signal = SIG_NONE;
                break;
            }
            if (loop_signal == SIG_CONTINUE) {
                loop_signal = SIG_NONE;
                continue;
            }
        }
        loop_depth--;
        return make_int(0);

    case NODE_FOR:
        loop_depth++;
        for (eval(node->left);
             value_is_true(eval(node->cond));
             eval(node->right)) {
            eval(node->body);
            if (return_flag) {
                break;
            }
            if (loop_signal == SIG_BREAK) {
                loop_signal = SIG_NONE;
                break;
            }
            if (loop_signal == SIG_CONTINUE) {
                loop_signal = SIG_NONE;
                continue;   /* execute quand meme l'increment */
            }
        }
        loop_depth--;
        return make_int(0);

    case NODE_FUNC_DECL:
        /* Enregistrement de la fonction quand on la rencontre dans le flot
         * d'instructions (les fonctions sont definies avant usage dans les
         * programmes de demonstration). */
        func_define(node);
        return make_int(0);

    case NODE_FUNC_CALL:
        return eval_func_call(node);

    case NODE_PRINT:
        v = eval(node->left);
        value_print(v);
        return make_int(0);

    case NODE_READ:
        {
            long x;
            if (scanf("%ld", &x) != 1) {
                x = 0;   /* entree invalide ou fin de flux */
            }
            return make_int(x);
        }

    case NODE_RETURN:
        return_value = eval(node->left);
        return_flag = 1;
        return return_value;

    case NODE_BREAK:
        if (loop_depth == 0) {
            fprintf(stderr, "Erreur ligne %d : 'arreter' utilise hors d'une "
                            "boucle\n", node->line);
            exit(1);
        }
        loop_signal = SIG_BREAK;
        return make_int(0);

    case NODE_CONTINUE:
        if (loop_depth == 0) {
            fprintf(stderr, "Erreur ligne %d : 'continuer' utilise hors d'une "
                            "boucle\n", node->line);
            exit(1);
        }
        loop_signal = SIG_CONTINUE;
        return make_int(0);

    case NODE_BLOCK:
        for (i = 0; i < node->nargs; i++) {
            eval(node->args[i]);
            /* On stoppe le bloc des qu'une rupture de flot est declenchee. */
            if (return_flag || loop_signal != SIG_NONE) {
                break;
            }
        }
        return make_int(0);

    default:
        fprintf(stderr, "Erreur interne : type de noeud inconnu (%d)\n",
                node->type);
        exit(1);
    }
}

void eval_program(Node *root)
{
    int i;

    /* Pre-passe : on enregistre toutes les fonctions declarees au premier
     * niveau du programme AVANT de l'executer. Cela autorise l'appel d'une
     * fonction definie plus loin dans le source (reference avant declaration)
     * ainsi que la recursion croisee, sans imposer un ordre de declaration. */
    if (root != NULL && root->type == NODE_BLOCK) {
        for (i = 0; i < root->nargs; i++) {
            if (root->args[i]->type == NODE_FUNC_DECL) {
                func_define(root->args[i]);
            }
        }
    }

    eval(root);
}
