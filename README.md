# ILANG — Interpréteur

Projet final du cours **Interprétation & Compilation** (L3, IED Paris 8).
AIT BENAISSA Ilias — n° 82508880.

ILANG est un mini-langage impératif (variables, expressions, conditions,
boucles, fonctions récursives, entrées/sorties) interprété via un AST.
La chaîne suit le cours : **Flex** (analyse lexicale) → **Bison** (analyse
syntaxique LALR(1), construction de l'AST) → optimisations → évaluation
récursive.

```
source ILANG ──▶ [Flex] tokens ──▶ [Bison] AST ──▶ [optim] AST ──▶ [eval] résultat
```

## Dépendances

```bash
sudo apt install bison flex gcc make
```

(Développé et testé sous Ubuntu, GCC 15.2 / Bison 3.8.2 / Flex 2.6.4 / Make 4.4.1 ;
code C portable, compatible GCC 13 / Ubuntu 24.04.)

## Compilation

```bash
make          # produit le binaire ./ilang  (zéro warning, zéro conflit Bison)
make clean    # supprime les fichiers générés
make test     # compile puis lance tous les programmes de démonstration
```

## Utilisation

```bash
./ilang programmes/fibonacci.ilang   # exécute un fichier .ilang
./ilang                              # mode interactif (programme lu sur stdin, Ctrl-D)
echo 7 | ./ilang programmes/interactif.ilang   # alimenter lire() via un pipe
./ilang -s programmes/pgcd.ilang     # génère l'assembleur i386 (extension hors-scope)
```

## Le langage en bref

| Élément          | Syntaxe                                              |
|------------------|-----------------------------------------------------|
| Affectation      | `x = 42`  (déclaration implicite)                   |
| Types            | entiers et **flottants** (`3.14`)                   |
| Arithmétique     | `+ - * / %`  (et `-x`), parenthèses                 |
| Comparaisons     | `== != < > <= >=`  (renvoient 0 ou 1)               |
| Condition        | `si (c) { ... } sinon { ... }`                      |
| Boucles          | `tant que (c) { ... }` , `pour (i=0; i<n; i=i+1) { ... }` |
| Rupture de boucle| `arrêter` (break) , `continuer` (continue)         |
| Fonctions        | `fonction f(a, b) { ... retourner(expr) }`          |
| Affichage        | `afficher(expr)`                                    |
| Saisie           | `lire()`                                            |
| Commentaires     | `// ligne`  et  `/* bloc */`                         |

### Sémantique numérique

- `entier op entier` → résultat entier (donc `10 / 4` = `2`, division entière).
- dès qu'un flottant intervient → résultat flottant (`10.0 / 4` = `2.5`).
- le `%` (modulo) n'est défini que sur les entiers.

## Programmes de démonstration (`programmes/`)

| Fichier            | Démontre                                          |
|--------------------|---------------------------------------------------|
| `fibonacci.ilang`  | fonction récursive + boucle `pour`                |
| `pgcd.ilang`       | algorithme d'Euclide (boucle `tant que`)          |
| `interactif.ilang` | `lire()` + condition `si/sinon`                   |
| `flottants.ilang`  | nombres flottants (extension)                     |
| `boucles.ilang`    | `arrêter` / `continuer` (extension)               |

## Gestion des erreurs

Tous les messages indiquent la ligne (`Erreur ligne N : ...`). Cas couverts :
caractère invalide (lexical), erreur de syntaxe (Bison, token inattendu),
variable non définie, fonction non définie, mauvais nombre d'arguments,
division/modulo par zéro (message + le programme continue), modulo sur
flottants, et `arrêter`/`continuer` utilisé hors d'une boucle.

Les cas de test sont dans `tests/` (`bash tests/run_errors.sh`).

## Extension hors-périmètre : backend assembleur i386

Par curiosité (et parce que le cahier des charges l'évoque dans ses « extensions
possibles »), un générateur de code assembleur a été ajouté dans `codegen.c`. Il
**n'altère pas l'interpréteur** : c'est un mode séparé qui émet du `.s` 32 bits
(syntaxe AT&T) au lieu d'évaluer l'AST.

```bash
./ilang -s programmes/pgcd.ilang > pgcd.s   # génère l'assembleur
gcc -m32 -no-pie pgcd.s -o pgcd             # assemble (nécessite gcc-multilib)
./pgcd                                       # exécute -> même résultat que l'interpréteur
make asm                                     # raccourci : génère + assemble + exécute pgcd
```

Limites assumées (documentées dans `JOURNAL.md` §16) : **entiers uniquement**
(les flottants sont refusés proprement), et la division par zéro provoque un
`SIGFPE` (pas la gestion « douce » de l'interpréteur). Les sorties du binaire
assemblé sont identiques à celles de l'interpréteur sur tous les programmes
entiers.

## Organisation des fichiers

| Fichier               | Rôle                                               |
|-----------------------|----------------------------------------------------|
| `ilang.l`             | analyseur lexical (Flex)                            |
| `ilang.y`             | grammaire + construction de l'AST (Bison)          |
| `ast.h` / `ast.c`     | définition et manipulation de l'AST, type `Value`  |
| `symtab.h` / `symtab.c` | table des symboles (pile de frames + hachage)    |
| `eval.c`              | évaluateur récursif de l'AST                        |
| `optim.c`             | pliage de constantes + élimination du code mort    |
| `codegen.c`           | backend assembleur i386 (extension hors-périmètre) |
| `main.c`              | point d'entrée (fichier, interactif ou `-s`)       |
| `Makefile`            | compilation automatisée                            |
| `JOURNAL.md`          | journal de bord (difficultés, choix de conception) |
