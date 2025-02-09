#!/usr/bin/env sh

clang++ -DCOLLECT_STATS -DCUSTOM_GC -DGC_SYSTEM_MALLOC -D "GC_SYSTEM_MALLOC_HEADER=<mimalloc.h>" -D "GC_SYSTEM_MALLOC_PREFIX(X)=mi_##X" -lmimalloc -g -ggdb -Wall -Wextra -Wno-attributes main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000 "$@"