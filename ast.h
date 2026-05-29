#ifndef AST_H
#define AST_H

/*
 * ast.h - Definition de l'arbre syntaxique abstrait (AST) du langage ILANG.
 *
 * Chaque noeud est une structure C avec un type (NodeType) et un ensemble
 * de fils. Tous les noeuds partagent la meme structure Node ; selon le type,
 * seuls certains champs sont significatifs (cf. commentaires ci-dessous).
 *
 * Ce decoupage suit le cahier des charges (section 3.5).
 */

/* ---- Valeur d'execution ----
 * ILANG manipule deux types numeriques : des entiers et des flottants.
 * Une Value est etiquetee (tagged union) : le champ type indique lequel des
 * deux champs (ival ou fval) est significatif. Cela permet de conserver la
 * semantique entiere d'origine (division entiere, modulo) tout en ajoutant
 * le support des flottants sans casser l'existant. */
typedef enum { VAL_INT, VAL_FLOAT } ValType;

typedef struct Value {
    ValType type;
    long ival;     /* significatif si type == VAL_INT   */
    double fval;   /* significatif si type == VAL_FLOAT  */
} Value;

/* Type d'un noeud de l'AST. */
typedef enum {
    NODE_NUM,        /* litteral numerique (entier ou flottant) : num            */
    NODE_VAR,        /* reference de variable      : name                        */
    NODE_BINOP,      /* operation binaire          : op, left, right             */
    NODE_UNOP,       /* operation unaire (- )      : op, left                    */
    NODE_ASSIGN,     /* affectation                : name, left (= expression)   */
    NODE_IF,         /* condition si/sinon         : cond, body, elsebody        */
    NODE_WHILE,      /* boucle tant que            : cond, body                  */
    NODE_FOR,        /* boucle pour                : left(init), cond,           */
                     /*                              right(incr), body           */
    NODE_FUNC_DECL,  /* declaration de fonction    : name, args(params), nargs,  */
                     /*                              body                        */
    NODE_FUNC_CALL,  /* appel de fonction          : name, args, nargs           */
    NODE_PRINT,      /* afficher(expr)             : left                        */
    NODE_READ,       /* lire()                     : (aucun champ)               */
    NODE_RETURN,     /* retourner(expr)            : left                        */
    NODE_BREAK,      /* arreter (sortie de boucle) : (aucun champ)               */
    NODE_CONTINUE,   /* continuer (iteration suiv.): (aucun champ)               */
    NODE_BLOCK       /* suite d'instructions       : args(statements), nargs     */
} NodeType;

/* Operateurs pour NODE_BINOP et NODE_UNOP. */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,   /* arithmetique          */
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,  /* comparaison           */
    OP_NEG                                     /* moins unaire (UMINUS) */
} OpType;

/* Noeud generique de l'AST. */
typedef struct Node {
    NodeType type;

    Value num;          /* NODE_NUM (entier ou flottant)            */
    char *name;         /* NODE_VAR, NODE_ASSIGN, NODE_FUNC_*        */
    int op;             /* NODE_BINOP, NODE_UNOP (OpType)            */

    struct Node *left;      /* operande gauche / init de boucle / expr  */
    struct Node *right;     /* operande droit / incr de boucle          */
    struct Node *cond;      /* condition (if / while / for)             */
    struct Node *body;      /* corps (if-then / while / for / fonction) */
    struct Node *elsebody;  /* corps du sinon                           */

    struct Node **args;     /* NODE_FUNC_DECL (params), NODE_FUNC_CALL  */
                            /* (arguments), NODE_BLOCK (instructions)   */
    int nargs;              /* nombre d'elements dans args              */

    int line;               /* ligne source (pour les messages d'erreur)*/
} Node;

/* ---- Utilitaires sur les valeurs (definis dans ast.c) ---- */

Value make_int(long i);
Value make_float(double f);
int value_is_true(Value v);     /* 0 = faux, tout le reste = vrai */
void value_print(Value v);      /* affiche la valeur + retour ligne */

/* ---- Constructeurs (definis dans ast.c) ---- */

Node *new_num(long value);      /* litteral entier   */
Node *new_fnum(double value);   /* litteral flottant */
Node *new_var(char *name);
Node *new_binop(int op, Node *left, Node *right);
Node *new_unop(int op, Node *operand);
Node *new_assign(char *name, Node *expr);
Node *new_if(Node *cond, Node *body, Node *elsebody);
Node *new_while(Node *cond, Node *body);
Node *new_for(Node *init, Node *cond, Node *incr, Node *body);
Node *new_func_decl(char *name, Node **params, int nparams, Node *body);
Node *new_func_call(char *name, Node **args, int nargs);
Node *new_print(Node *expr);
Node *new_read(void);
Node *new_return(Node *expr);
Node *new_break(void);
Node *new_continue(void);
Node *new_block(void);

/* Ajoute une instruction a un noeud NODE_BLOCK (croissance dynamique). */
void block_add(Node *block, Node *stmt);

/* ---- Liste temporaire de noeuds (parametres / arguments) ----
 * Utilisee uniquement par le parseur pour accumuler les listes de
 * parametres et d'arguments avant de les transferer dans un noeud. */
typedef struct NodeList {
    Node **items;
    int count;
} NodeList;

NodeList *nodelist_new(void);
void nodelist_add(NodeList *list, Node *node);

/* Libere recursivement l'AST. */
void free_node(Node *node);

#endif /* AST_H */
