#!/bin/bash
# Compile le rapport et affiche le nombre de pages avec et sans annexes.
cd /mnt/c/Users/ilias/Downloads/ilang/rapport || exit 1
latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex > /tmp/build.log 2>&1
code=$?
echo "=== build exit=$code ==="
grep -E '^!|Fatal error|Emergency' /tmp/build.log | head -15
tot=$(pdfinfo main.pdf 2>/dev/null | awk '/Pages/{print $2}')
# page de debut des annexes = premier chapitre \appendix (numberline {A})
ann=$(grep -m1 'numberline {A}' main.toc 2>/dev/null | grep -oE '\{[0-9]+\}\{' | head -1 | tr -dc '0-9')
if [ -n "$ann" ]; then
    echo ">>> PAGES total = $tot   |   sans annexes = $((ann-1))   (annexes a partir de p.$ann)"
else
    echo ">>> PAGES total = $tot   |   (annexe A non localisee dans le .toc)"
fi
