# Journal de bord — Interpréteur ILANG

AIT BENAISSA Ilias — n° 82508880
Cours : Interprétation & Compilation (L3, IED Paris 8) — Projet final (chap. 10)

Je note ici, au fil de l'eau, les problèmes que j'ai rencontrés, les choix que
j'ai faits et pourquoi. C'est volontairement écrit dans le désordre
chronologique du développement, pas dans l'ordre « propre » du rapport.

---

## 0. Mise en place de l'environnement

Première surprise : j'ai voulu compiler et `gcc`, `bison`, `flex` n'étaient pas
là. J'ai installé la chaîne avec :

```
sudo apt update
sudo apt install gcc bison flex make
```

Au final je tourne sur un GCC 15.2, Bison 3.8.2, Flex 2.6.4, GNU Make 4.4.1.
Le cahier des charges parlait de GCC 13.3 / Ubuntu 24.04 mais le code est du C
standard, ça compile pareil — j'ai juste vérifié que je n'utilisais rien de
spécifique à une version.

---

## 1. La grammaire : faut-il des points-virgules ?

Premier vrai casse-tête, et il vient du cahier des charges lui-même. L'extrait
BNF (section 3.3) écrit les instructions avec un `;` final :

```
instruction ::= affectation ';' | ...
```

…mais AUCUN des programmes de démonstration (section 4) n'utilise de
point-virgule, sauf à l'intérieur de l'en-tête du `pour` :

```
x = 42
y = x + 8
```

Comme les trois programmes de démo **doivent** tourner, j'ai tranché : pas de
terminateur d'instruction. Les instructions sont simplement juxtaposées
(séparées par des espaces/retours à la ligne), et le `;` ne sert que de
séparateur explicite dans la règle du `pour`. J'ai donc écrit :

```
liste_instructions
    : /* vide */
    | liste_instructions instruction
```

J'étais un peu inquiet que ça crée des ambiguïtés (comment savoir où finit une
instruction sans séparateur ?), mais comme chaque instruction se termine soit
par un `)` (afficher, retourner, appel), soit par un `}` (bloc), soit par une
expression complète, l'analyseur LALR(1) s'en sort très bien avec un seul token
d'avance.

---

## 2. `IDENTIFIANT` : variable, affectation ou appel ?

Quand l'analyseur voit un `IDENTIFIANT` en début d'instruction, ça peut être :

- une affectation : `x = ...`
- un appel de fonction : `f(...)`

J'avais peur d'un conflit. En fait non : avec un token d'avance, `=` mène à
l'affectation et `(` à l'appel. Bison construit deux états distincts, aucun
conflit. Pareil pour `appel_fonction` qui apparaît à la fois comme instruction
et dans une expression : le contexte (ce qui peut suivre) suffit à les
distinguer, donc pas de conflit reduce/reduce non plus.

---

## 3. Le « dangling else » qui n'en était pas un

Là j'ai appris quelque chose. Dans le cours, le `si … sinon` est l'exemple
classique de conflit shift/reduce (à quel `si` rattacher le `sinon` ?). Je
m'étais préparé à le résoudre par précédence, et j'avais effectivement mis :

```
%nonassoc THEN
%nonassoc SINON
...
si_alors : SI '(' expression ')' bloc %prec THEN | ... SINON bloc
```

Puis, par acquit de conscience, j'ai **retiré** toute cette résolution pour voir
si le conflit apparaissait vraiment. Et… Bison est resté muet : **0 conflit**.

J'ai compris pourquoi : dans le cours le corps du `si` est une instruction nue,
alors qu'ici un corps est **obligatoirement entre accolades** (règle `bloc`).
Du coup un `sinon` est toujours précédé d'un `}`. Au moment où l'analyseur
pourrait hésiter (réduire « si sans sinon » ou décaler `SINON`), le token
d'avance n'est jamais `SINON` mais `}` ou autre chose : `SINON` n'appartient pas
au FOLLOW de la réduction. L'ambiguïté n'existe tout simplement pas.

Décision : j'ai **supprimé** la précédence `THEN`/`SINON`, qui était du
cargo-culting inutile, et je l'ai documenté en commentaire dans `ilang.y`. Je
trouvais plus honnête de garder une grammaire propre que de laisser une
directive « au cas où » qui ne sert à rien.

Pour vérifier, j'ai relancé `bison -d` sur une copie sans aucune précédence : la
sortie est vide (aucun avertissement de conflit). C'est la preuve que la
grammaire est non ambiguë par construction.

---

## 4. Les listes de paramètres et d'arguments

Le `Node` du cahier des charges a un tableau `args` + `nargs`, mais pas de
structure pratique pour **accumuler** une liste pendant le parsing. J'ai donc
ajouté un petit type jetable `NodeList` (juste `Node** items` + `int count`)
utilisé uniquement par les actions de Bison.

Subtilité mémoire : quand je construis le nœud final (appel ou déclaration de
fonction), je **transfère** le tableau `items` dans `node->args` puis je libère
seulement la coquille `NodeList` avec `free($3)` — pas `items`, qui appartient
maintenant au nœud. Au début j'avais un doute (est-ce que je vais double-free ?)
donc j'ai tracé ça à la main, et c'est confirmé par Valgrind plus tard : 0 fuite.

---

## 5. Le mécanisme de `retourner`

Pas évident. `retourner(expr)` doit interrompre l'exécution de la fonction et
remonter une valeur, même si on est au fond de plusieurs blocs imbriqués.

J'ai choisi deux variables globales : `return_flag` (un retour est en cours) et
`return_value` (la valeur). Dès qu'un bloc voit `return_flag` levé, il arrête
d'exécuter ses instructions suivantes. La boucle d'appel de fonction
(`eval_func_call`) « consomme » le drapeau : elle le remet à 0 avant d'exécuter
le corps, récupère la valeur après, puis le remet à 0.

J'ai dû penser à propager l'arrêt dans **toutes** les structures qui contiennent
un bloc : `NODE_BLOCK` bien sûr, mais aussi `NODE_WHILE` et `NODE_FOR` (sinon une
boucle continuait de tourner après un `retourner`). Premier test de Fibonacci
récursif : ça a marché du premier coup une fois ces `if (return_flag) break;`
ajoutés. Avant ça, sans le break dans les boucles, j'avais des valeurs fausses.

---

## 6. La portée des variables (le piège de l'ordre)

La table des symboles est une **pile de frames**, chaque frame étant une table
de hachage (hachage djb2, 211 buckets). À l'entrée d'une fonction je `push`, à
la sortie je `pop`. Au début, la recherche d'une variable se faisait du sommet
vers la base (comme suggéré par le cahier des charges) — mais je reviens sur ce
point en **§15(a)** : ce parcours complet de la pile est en réalité de la portée
*dynamique*, que j'ai corrigée ensuite en portée *lexicale*.

Le piège sur lequel j'ai failli me faire avoir : **l'ordre** dans
`eval_func_call`. Il faut évaluer les arguments dans l'environnement de
**l'appelant**, AVANT de faire le `push` du nouveau frame. Si on push d'abord,
les arguments comme `fibonacci(n - 1)` iraient chercher `n` dans le frame vide
de la fonction appelée au lieu du frame courant → valeurs fausses ou « variable
non définie ». J'ai donc bien séparé :

1. évaluer les args (frame appelant) → tableau temporaire
2. `env_push`
3. lier les paramètres dans le nouveau frame
4. évaluer le corps
5. `env_pop`

Autre choix de conception : `env_set` écrit toujours dans le frame **courant**
(création si la variable n'existe pas). Ça donne exactement la sémantique
demandée : variables globales pour le programme principal (dont le frame courant
EST le frame global), variables locales dans les fonctions. Les locales ne
fuient pas et n'écrasent pas les globales.

---

## 7. Optimisations : le piège de la division par zéro

Pliage de constantes : si les deux fils d'un `BINOP` sont des littéraux, je
remplace le nœud par le résultat calculé. Facile… sauf que `10 / 0` est aussi
« deux littéraux ». Si je le pliais bêtement, soit je plantais à la compilation
(division entière par 0 = SIGFPE !), soit je calculais n'importe quoi, et SURTOUT
je court-circuitais le message d'erreur d'exécution que le cahier des charges
demande (« division par zéro, le programme continue »).

J'ai donc ajouté un garde `can_fold()` : on ne plie jamais une division/modulo
par zéro (ni un modulo sur flottants après l'extension). Ces cas restent dans
l'AST et c'est l'évaluateur qui émet le message à l'exécution. Vérifié avec
`tests/err_divzero.ilang`.

Élimination du code mort : un `si` dont la condition est le littéral 0 voit sa
branche « alors » supprimée (on garde le « sinon » s'il existe). J'ai géré le cas
où le nœud entier disparaît (un `si (0) { … }` sans `sinon` devient `NULL`) :
`optimize()` renvoie alors `NULL`, et dans `NODE_BLOCK` je **compacte** le
tableau d'instructions pour retirer ces trous. Au début j'avais oublié ce
compactage et je me retrouvais avec des `NULL` dans la liste → `eval` recevait un
nœud nul. Heureusement `eval(NULL)` renvoie 0 proprement (garde en début de
fonction), donc pas de crash, mais c'était plus propre de les retirer vraiment.

Pour vérifier que le pliage se produit réellement (et pas seulement que le
résultat est correct), j'ai temporairement mis un `printf` dans la branche de
pliage : il se déclenche bien sur `2 + 3 * 4` → il ne reste qu'un seul
`NODE_NUM` valant 14 dans l'AST. Je l'ai retiré ensuite.

---

## 8. Le lexer : « tant que » et les mots-clés

Deux détails :

- `tant que` s'écrit en deux mots avec un espace. J'ai donc un motif
  `"tant"[ \t]+"que"` placé AVANT la règle des identifiants, sinon `tant` serait
  lu comme un identifiant.
- Pour les mots-clés en général, je me suis appuyé sur la règle du **plus long
  match** de Flex : `pourcentage` est lu comme un identifiant (11 caractères) et
  pas comme le mot-clé `pour` (4 caractères) suivi de `centage`. Et pour `pour`
  exactement, à longueur égale, c'est la règle écrite en premier (le mot-clé)
  qui gagne. J'ai mis tous les mots-clés avant la règle `{ID}`.

Les commentaires de bloc `/* … */` sont gérés par un état exclusif `%x COMMENT`
plutôt qu'une grosse regex, parce que c'est plus lisible et que ça me permet de
compter les retours à la ligne dedans (via `%option yylineno`) et de détecter un
commentaire non terminé en fin de fichier.

---

## 9. Le warning `strdup` (perdu 20 minutes dessus)

À la première compilation, erreur :

```
implicit declaration of function 'strdup'
assignment to 'char *' from 'int' makes pointer from integer without a cast
```

J'avais compilé en `-std=c11`. Or `strdup` n'est PAS dans le C standard, c'est
une extension POSIX. En `-std=c11` strict, la glibc cache son prototype, donc
GCC suppose un `int` de retour → le fameux warning de conversion pointeur/entier
(et un comportement potentiellement faux sur 64 bits, le pointeur tronqué).

Deux solutions : définir `_POSIX_C_SOURCE 200809L`, ou passer en `-std=gnu11`.
J'ai choisi `-std=gnu11` (le standard + les extensions GNU, qui est de toute
façon le mode par défaut de GCC). Réglé.

---

## 10. Une coquille bête dans le Makefile

J'avais écrit une ligne de dépendance avec un **espace** en tête au lieu de
rien :

```
 symtab.o: symtab.c symtab.h ast.h
```

Make est très susceptible sur l'indentation (un vrai TAB = recette, autre chose
= confusion). J'ai retiré l'espace. Petit rappel que dans un Makefile chaque
caractère blanc compte.

Pour le « zéro warning » demandé, j'ai séparé deux jeux de flags : mon code à moi
est compilé avec `-Wall -Wextra`, mais le code **généré** par Flex et Bison
(`lex.yy.c`, `ilang.tab.c`) est compilé sans `-Wextra`. C'est volontaire : le
code machine-généré contient parfois des constructions qui déclenchent des
avertissements dont je ne suis pas responsable et que je ne peux pas corriger
sans éditer du code généré (mauvaise idée). Comme ça, zéro warning de mon côté.

---

## == EXTENSIONS (deuxième phase) ==

Après avoir rendu la version de base fonctionnelle, j'ai ajouté trois
fonctionnalités. Objectif : ne RIEN casser (Fibonacci et PGCD doivent donner
exactement les mêmes sorties qu'avant).

## 11. Nombres flottants

C'est l'extension la plus invasive parce qu'elle change le **type des valeurs**
dans tout l'interpréteur. Avant, tout était `int`. Maintenant une valeur peut
être entière OU flottante.

J'ai introduit une union étiquetée (« tagged union ») :

```c
typedef enum { VAL_INT, VAL_FLOAT } ValType;
typedef struct Value { ValType type; long ival; double fval; } Value;
```

Le point CLÉ pour ne rien casser : garder la sémantique entière quand les deux
opérandes sont des entiers.

- `entier op entier` → reste entier. Donc `10 / 4` vaut toujours `2` (division
  entière), et `%` (modulo) continue de marcher → PGCD inchangé.
- dès qu'un flottant intervient → on bascule en `double`. Donc `10.0 / 4` vaut
  `2.5`, et `3.14 * 2` vaut `6.28`.
- les comparaisons renvoient toujours un entier 0/1.
- le **modulo sur flottants n'a pas de sens** : je renvoie une erreur
  (« modulo non défini sur les flottants ») au lieu d'utiliser `fmod`, parce que
  le langage est censé rester simple et que ça révèle une erreur de l'auteur du
  programme.

Ça a touché : `ast.h`/`ast.c` (le champ `value` d'un `NODE_NUM` devient une
`Value num`, ajout de `new_fnum`), `symtab` (les variables stockent des `Value`),
`eval.c` (l'évaluateur renvoie maintenant `Value` partout — c'est le plus gros
changement), `optim.c` (le pliage manipule des `Value`), et le lexer/parseur
(nouveau token `FLOTTANT`, motif `{CHIFFRE}+"."{CHIFFRE}+`, placé AVANT l'entier).

Pour l'affichage j'utilise `%g` pour les flottants : `6.28` reste `6.28` et `3.0`
s'affiche `3` (notation compacte), et `%ld` pour les entiers.

Difficulté : après ce refactor, un warning « `as_long` defined but not used ».
J'avais écrit deux helpers de conversion (`as_long` et `as_double`) mais je ne me
servais que de `as_double`. Supprimé.

Vérification de non-régression : Fibonacci et PGCD redonnent EXACTEMENT les mêmes
sorties qu'avant l'extension. Ouf.

## 12. `arrêter` et `continuer` (break / continue)

Même logique que `retourner`, mais pour les boucles. J'ai ajouté un signal
global :

```c
typedef enum { SIG_NONE, SIG_BREAK, SIG_CONTINUE } LoopSignal;
```

`arrêter` lève `SIG_BREAK`, `continuer` lève `SIG_CONTINUE`. Les boucles
`tant que` et `pour` consultent ce signal après chaque exécution du corps :

- `SIG_BREAK` → on remet à `SIG_NONE` et on sort de la boucle.
- `SIG_CONTINUE` → on remet à `SIG_NONE` et on passe à l'itération suivante.

Détail qui m'a fait réfléchir : dans une boucle `pour`, un `continuer` doit
quand même exécuter l'incrément (le `i = i + 1`). En C, `continue` dans un
`for(...)` saute bien à la partie incrément. Comme j'ai implémenté `NODE_FOR`
avec un vrai `for` C, le `continue;` C fait exactement ce qu'il faut. J'ai
vérifié avec `programmes/boucles.ilang` : la version « nombres impairs » affiche
bien `1 3 5 7 9` (donc l'incrément a lieu malgré le `continuer`).

Il a aussi fallu que `NODE_BLOCK` s'arrête quand un signal de boucle est levé
(pas seulement sur `return_flag`), sinon les instructions après un `arrêter` dans
le même bloc continuaient de s'exécuter. La condition d'arrêt d'un bloc est donc
maintenant : `if (return_flag || loop_signal != SIG_NONE) break;`.

Pour le lexer, `arrêter` contient un `ê` (accent circonflexe). Le fichier source
et le `.l` sont en UTF-8, donc Flex matche la séquence d'octets. Par sécurité
j'ai écrit le motif `"arr"("ê"|"e")"ter"` pour accepter aussi `arreter` sans
accent, au cas où quelqu'un tape sans l'accent.

## 13. Numéro de ligne dans les erreurs

C'était en partie déjà fait dès la version de base, mais j'ai uniformisé le
format sur l'exemple demandé : `Erreur ligne 12 : variable 'x' non définie`.

Le point important de conception : je stocke le numéro de ligne **dans chaque
nœud au moment du parsing** (`node->line = yylineno;` dans le constructeur), et
PAS au moment de l'évaluation. Pourquoi ? Parce qu'à l'évaluation, `yylineno`
vaut la dernière ligne lue par le lexer, c'est-à-dire la FIN du fichier (tout a
déjà été lu avant que l'évaluation commence). Si j'utilisais `yylineno` dans
`eval`, toutes les erreurs afficheraient la même ligne (la dernière). En figeant
la ligne dans le nœud à la construction, chaque erreur pointe la bonne ligne.
C'est exactement ce qu'on voit dans le test « mauvais nombre d'arguments » :
l'erreur indique `ligne 5`, là où l'appel fautif est écrit, pas la fin du
fichier.

---

## 14. Vérification mémoire (Valgrind)

J'ai installé Valgrind et lancé `valgrind --leak-check=full` sur les programmes.
Premier passage : « definitely lost: 0 » mais « still reachable: 16 458 bytes ».
Ces blocs « encore atteignables » venaient du buffer interne de Flex, jamais
libéré. J'ai ajouté un appel à `yylex_destroy()` à la fin de `main`. Résultat
final : **in use at exit: 0 bytes / ERROR SUMMARY: 0 errors**. Propre.

---

## 15. Passe de vérification systématique (et corrections)

Une fois tout « fini », je me suis forcé à faire une vraie campagne de tests aux
limites (fichier vide, EOF, grands nombres, appels mal formés, portée, etc.) au
lieu de me contenter des trois démos. J'ai trouvé cinq comportements à revoir.
Aucun n'était un crash, mais cinq méritaient correction. Je les liste avec le
diagnostic et le correctif.

**(a) Portée dynamique au lieu de lexicale.** Mon `env_get` parcourait toute la
pile d'appel du sommet vers la base. Conséquence : une fonction `g` appelée par
`f` pouvait lire une variable locale de `f` ! Test reproducteur :

```
fonction g() { retourner(secret) }
fonction f() { secret = 123 retourner(g()) }
afficher(f())     // affichait 123 (!)
```

C'est de la portée DYNAMIQUE, alors que le cahier des charges parle de portée
« lexicale » (§3.6). Le cahier se contredit d'ailleurs lui-même : il décrit
l'algorithme « parcours du sommet vers la base », qui est justement dynamique.
J'ai tranché en faveur du terme employé (« lexicale »), qui est aussi le
comportement attendu de la quasi-totalité des langages.

Correctif : j'ai séparé les deux rôles du lien `parent`. Il reste le lien de
contrôle (pour restaurer le frame courant à la sortie de fonction), mais la
résolution d'une variable ne regarde plus que **le frame courant puis
l'environnement global** (mémorisé dans `global_env`), sans passer par les
frames intermédiaires. Maintenant le test ci-dessus affiche bien
« variable 'secret' non définie ». Les trois démos ne sont pas affectées (aucune
fonction n'y lit une variable d'une autre).

**(b) Débordement des littéraux entiers.** `afficher(9999999999)` affichait
`1410065407`. La cause : le lexer convertissait avec `atoi` (un `int` 32 bits)
alors que mes valeurs sont stockées en `long`. Correctif : `atoi` → `strtol`, et
le type `num` de l'union Bison passe de `int` à `long`. Maintenant
`9999999999` s'affiche correctement.

**(c) Appel d'une fonction définie plus loin.** `f(3)` avant `fonction f(...)`
échouait, parce que les fonctions étaient enregistrées au fil de l'évaluation.
Correctif : j'ai ajouté une **pré-passe** dans `eval_program` qui enregistre
toutes les fonctions de premier niveau AVANT d'exécuter le programme. Ça autorise
la référence avant déclaration et la récursion croisée sans contrainte d'ordre.
(L'évaluation de `NODE_FUNC_DECL` ré-enregistre la fonction, mais `func_define`
est idempotent, donc pas de souci.)

**(d) `arrêter`/`continuer` hors d'une boucle.** Au niveau global, un `arrêter`
levait silencieusement le signal de rupture et stoppait le reste du programme
sans rien dire. Correctif : un compteur `loop_depth`, incrémenté/décrémenté
autour de chaque boucle, et remis à zéro à l'entrée d'une fonction (pour qu'une
boucle de l'appelant ne « couvre » pas l'appelé). Si `arrêter`/`continuer` est
évalué avec `loop_depth == 0`, j'émets une erreur claire. Note : j'ai laissé
`retourner` au niveau global terminer le programme sans erreur — c'est un usage
défendable (fin de script), contrairement à un `arrêter` qui n'a vraiment aucun
sens hors boucle.

**(e) Littéraux flottants `3.` et `.5`.** Mon motif n'acceptait que
`chiffres.chiffres`, donc `3.` et `.5` provoquaient une erreur lexicale.
Correctif : motif élargi à `({CHIFFRE}+"."{CHIFFRE}*)|("."{CHIFFRE}+)` et
`atof` → `strtod`. `3.` donne `3`, `.5` donne `0.5`.

Après ces cinq correctifs : recompilation toujours sans warning ni conflit,
non-régression OK sur les trois démos (sorties identiques au bit près), et
Valgrind toujours à zéro fuite / zéro erreur. J'ai gardé mon script de
vérification pour pouvoir le relancer.

## 16. (HORS PÉRIMÈTRE — par curiosité) Génération d'assembleur i386

⚠️ **Attention : cette partie ne fait PAS partie du cahier des charges.** Le
projet demandait un *interpréteur*. Mais le cahier des charges mentionnait, dans
les « extensions possibles » (§8), la génération de code assembleur i386 « au
lieu d'évaluer l'AST, générer du `.s` compilable avec `gcc -m32`, comme ppcm ».
Comme le tableau de positionnement (§1.2) insiste sur le lien avec le chapitre 2
(assembleur, pile `%ebp`), j'ai eu envie d'essayer, pour voir si je comprenais
vraiment la chaîne jusqu'au bout. C'est donc un bonus personnel, fait après que
tout le reste fonctionnait, et **isolé** dans un fichier `codegen.c` qui ne
touche pas du tout à l'interpréteur (nouveau mode `./ilang -s prog.ilang`).

**Le principe.** Au lieu de parcourir l'AST pour *calculer*, je le parcours pour
*émettre des instructions* x86 32 bits (syntaxe AT&T), exactement le même AST,
un backend différent. C'est exactement l'idée d'un compilateur : front-end commun
(lexer/parseur/AST), back-ends interchangeables (évaluateur OU générateur de code).

**Le modèle mémoire** (le vrai morceau, et le lien direct avec le chapitre 2) :
- convention d'appel **cdecl** : l'appelant empile les arguments de droite à
  gauche, `call`, récupère le résultat dans `%eax`, puis nettoie la pile ;
- chaque fonction a son **cadre de pile** : `pushl %ebp ; movl %esp, %ebp`, les
  paramètres en `+8(%ebp)`, `+12(%ebp)`…, les variables locales en `-4(%ebp)`,
  `-8(%ebp)`… exactement comme le `%ebp` du cours ;
- les variables du programme principal sont des **globales** en `.bss` ;
- les expressions sont compilées en **machine à pile** : résultat courant dans
  `%eax`, opérandes intermédiaires `pushl`/`popl`. Pour un `a op b` : on évalue
  `a` dans `%eax`, on l'empile, on évalue `b` dans `%eax`, on le met dans `%ecx`,
  on dépile `a` dans `%eax`, et on applique l'opération.

Les structures de contrôle deviennent des labels + sauts (`cmpl`, `jz`, `jmp`),
et — détail sympa — `arrêter`/`continuer` se traduisent naturellement par un
`jmp` vers le label de fin / de continuation de la boucle (je maintiens une pile
de labels de boucles, comme la pile de boucles de l'évaluateur).

**Les galères rencontrées :**

1. **`gcc -m32` manquant.** Il a fallu installer `gcc-multilib` pour compiler en
   32 bits sur un système 64 bits.
2. **Warning de l'éditeur de liens (`DT_TEXTREL in a PIE`).** Mon code utilise de
   l'adressage absolu (`movl gv_x, %eax`), incompatible avec le PIE
   (position-independent executable) activé par défaut. Solution : assembler avec
   `gcc -m32 -no-pie`. (Faire du vrai PIC avec une GOT aurait été beaucoup plus
   lourd et hors sujet pour un exercice.)
3. **Alignement de pile.** `printf` peut planter si `%esp` n'est pas aligné. J'ai
   ajouté `andl $-16, %esp` au début de `main` et j'arrondis la taille des cadres
   à 16 octets. Avec ça, plus de crash.
4. **Les flottants.** Le faire en assembleur voudrait dire gérer le x87/SSE, ce
   qui doublerait la complexité de chaque opération. J'ai assumé la limite : si
   le générateur rencontre un littéral flottant, il **refuse** avec un message
   clair plutôt que de produire du code faux. Le backend est donc entiers
   uniquement (comme `ppcm`).
5. **Division par zéro.** Contrairement à l'interpréteur (message + on continue),
   un `idivl` par zéro lève un `SIGFPE` à l'exécution, comme en C. Reproduire le
   comportement « doux » aurait demandé d'émettre un test avant chaque division ;
   je ne l'ai pas fait (hors sujet).

**Le résultat.** Pour chaque programme **entier**, j'ai comparé la sortie de
l'interpréteur et celle du binaire assemblé : elles sont **identiques**
(Fibonacci, PGCD, les boucles avec `arrêter`/`continuer`, et même le programme
interactif qui passe par `scanf`/`printf`). Exemple de ce que ça génère pour
`fonction carre(x){ retourner(x*x) }` :

```asm
il_carre:
    pushl %ebp
    movl %esp, %ebp
    movl 8(%ebp), %eax     # x
    pushl %eax
    movl 8(%ebp), %eax     # x
    movl %eax, %ecx
    popl %eax
    imull %ecx, %eax       # x * x
    leave
    ret
```

C'était la partie la plus formatrice du projet : voir le même AST se transformer
soit en valeurs (interprétation), soit en instructions machine (compilation),
ça rend concret tout ce que le cours raconte sur la chaîne de compilation. Mais
je le redis : **c'est un hors-sujet assumé, pas une exigence du projet.**

## 17. Bilan / ce que je referais autrement

- Je suis content d'avoir testé le dangling-else « pour de vrai » au lieu de
  recopier la solution du cours : j'ai compris POURQUOI il n'y a pas de conflit
  ici, ce qui est plus formateur.
- Le passage de `int` à `Value` partout aurait été beaucoup plus douloureux si
  j'avais commencé par là. Faire d'abord la version entière, la faire marcher,
  PUIS généraliser, c'était la bonne stratégie.
- Les variables globales pour `return_flag` / `loop_signal`, c'est un peu sale
  (état global), mais pour un interpréteur récursif mono-thread c'est simple et
  ça marche. Une vraie alternative serait `setjmp`/`longjmp` ou faire remonter un
  code de statut par valeur de retour — j'ai préféré la lisibilité.
