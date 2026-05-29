/*
 * ast.c - Construction et liberation des noeuds de l'AST ILANG.
 *
 * Les constructeurs sont appeles depuis les actions semantiques de Bison
 * (ilang.y). Chaque constructeur alloue un Node, l'initialise a zero puis
 * renseigne uniquement les champs pertinents pour son type.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* La ligne courante est maintenue par le lexer (yylineno via une variable
 * exportee). On la recopie dans chaque noeud cree pour pouvoir afficher
 * des messages d'erreur localises a l'evaluation. */
extern int yylineno;

/* Allocation de base : alloue un Node, le met a zero et fixe le type. */
static Node *new_node(NodeType type)
{
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (n == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (calloc)\n");
        exit(1);
    }
    n->type = type;
    n->line = yylineno;
    return n;
}

/* ---- Utilitaires sur les valeurs ---- */

Value make_int(long i)
{
    Value v;
    v.type = VAL_INT;
    v.ival = i;
    v.fval = 0.0;
    return v;
}

Value make_float(double f)
{
    Value v;
    v.type = VAL_FLOAT;
    v.ival = 0;
    v.fval = f;
    return v;
}

int value_is_true(Value v)
{
    if (v.type == VAL_INT) {
        return v.ival != 0;
    }
    return v.fval != 0.0;
}

void value_print(Value v)
{
    if (v.type == VAL_INT) {
        printf("%ld\n", v.ival);
    } else {
        /* %g : notation compacte (6.28 reste 6.28, 3.0 s'affiche 3) */
        printf("%g\n", v.fval);
    }
}

/* ---- Constructeurs ---- */

Node *new_num(long value)
{
    Node *n = new_node(NODE_NUM);
    n->num = make_int(value);
    return n;
}

Node *new_fnum(double value)
{
    Node *n = new_node(NODE_NUM);
    n->num = make_float(value);
    return n;
}

Node *new_var(char *name)
{
    Node *n = new_node(NODE_VAR);
    n->name = name;   /* deja duplique par le lexer (strdup) */
    return n;
}

Node *new_binop(int op, Node *left, Node *right)
{
    Node *n = new_node(NODE_BINOP);
    n->op = op;
    n->left = left;
    n->right = right;
    return n;
}

Node *new_unop(int op, Node *operand)
{
    Node *n = new_node(NODE_UNOP);
    n->op = op;
    n->left = operand;
    return n;
}

Node *new_assign(char *name, Node *expr)
{
    Node *n = new_node(NODE_ASSIGN);
    n->name = name;
    n->left = expr;
    return n;
}

Node *new_if(Node *cond, Node *body, Node *elsebody)
{
    Node *n = new_node(NODE_IF);
    n->cond = cond;
    n->body = body;
    n->elsebody = elsebody;
    return n;
}

Node *new_while(Node *cond, Node *body)
{
    Node *n = new_node(NODE_WHILE);
    n->cond = cond;
    n->body = body;
    return n;
}

Node *new_for(Node *init, Node *cond, Node *incr, Node *body)
{
    Node *n = new_node(NODE_FOR);
    n->left = init;
    n->cond = cond;
    n->right = incr;
    n->body = body;
    return n;
}

Node *new_func_decl(char *name, Node **params, int nparams, Node *body)
{
    Node *n = new_node(NODE_FUNC_DECL);
    n->name = name;
    n->args = params;
    n->nargs = nparams;
    n->body = body;
    return n;
}

Node *new_func_call(char *name, Node **args, int nargs)
{
    Node *n = new_node(NODE_FUNC_CALL);
    n->name = name;
    n->args = args;
    n->nargs = nargs;
    return n;
}

Node *new_print(Node *expr)
{
    Node *n = new_node(NODE_PRINT);
    n->left = expr;
    return n;
}

Node *new_read(void)
{
    return new_node(NODE_READ);
}

Node *new_return(Node *expr)
{
    Node *n = new_node(NODE_RETURN);
    n->left = expr;
    return n;
}

Node *new_break(void)
{
    return new_node(NODE_BREAK);
}

Node *new_continue(void)
{
    return new_node(NODE_CONTINUE);
}

Node *new_block(void)
{
    Node *n = new_node(NODE_BLOCK);
    n->args = NULL;
    n->nargs = 0;
    return n;
}

void block_add(Node *block, Node *stmt)
{
    if (stmt == NULL) {
        return;   /* instruction vide eliminee par l'optimiseur, p.ex. */
    }
    block->args = (Node **)realloc(block->args,
                                   sizeof(Node *) * (block->nargs + 1));
    if (block->args == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (realloc)\n");
        exit(1);
    }
    block->args[block->nargs] = stmt;
    block->nargs++;
}

NodeList *nodelist_new(void)
{
    NodeList *l = (NodeList *)malloc(sizeof(NodeList));
    if (l == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (nodelist)\n");
        exit(1);
    }
    l->items = NULL;
    l->count = 0;
    return l;
}

void nodelist_add(NodeList *list, Node *node)
{
    list->items = (Node **)realloc(list->items,
                                   sizeof(Node *) * (list->count + 1));
    if (list->items == NULL) {
        fprintf(stderr, "Erreur fatale : memoire insuffisante (nodelist_add)\n");
        exit(1);
    }
    list->items[list->count] = node;
    list->count++;
}

void free_node(Node *node)
{
    int i;

    if (node == NULL) {
        return;
    }

    /* Liberation recursive de tous les fils possibles. */
    free_node(node->left);
    free_node(node->right);
    free_node(node->cond);
    free_node(node->body);
    free_node(node->elsebody);

    if (node->args != NULL) {
        for (i = 0; i < node->nargs; i++) {
            free_node(node->args[i]);
        }
        free(node->args);
    }

    if (node->name != NULL) {
        free(node->name);
    }

    free(node);
}
