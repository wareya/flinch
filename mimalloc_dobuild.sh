#!/usr/bin/env sh

clang++ -DCOLLECT_STATS -DCUSTOM_GC -lmimalloc -g -ggdb -Wall -Wextra -Wno-attributes main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000 "$@"