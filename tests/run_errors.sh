#!/bin/bash
# Lance les six cas d'erreur geres par l'interpreteur ILANG.
cd "$(dirname "$0")/.." || exit 1
for f in variable divzero fonction arguments lexical syntaxe; do
    echo "===== $f ====="
    ./ilang "tests/err_$f.ilang"
    echo "(code de sortie : $?)"
    echo
done
