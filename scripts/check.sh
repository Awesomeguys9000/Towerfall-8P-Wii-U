#!/bin/sh
# syntax-check all generated sources, write unique errors
cd "$(dirname "$0")/../gen"
clang++ -std=c++20 -x c++-header -I../runtime -I. tf_all.h -o ../build/check/tf_all.h.pch 2>&1 | grep error: | head -20
ls src/*.cpp | xargs -P 8 -I{} sh -c 'clang++ -std=c++20 -fsyntax-only -include-pch ../build/check/tf_all.h.pch -I../runtime -I. -ferror-limit=100 -w "{}" 2>&1 | grep "error:"' > ../build/check/errors.txt
wc -l < ../build/check/errors.txt
sed 's/^[^:]*:[0-9]*:[0-9]*: //' ../build/check/errors.txt | sort | uniq -c | sort -rn | head -${1:-40}
