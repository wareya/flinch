#!/usr/bin/env sh

clang++ -DCOLLECT_STATS -DCUSTOM_GC -DGC_CUSTOM_MALLOC -DGC_NO_PREFIX -g -ggdb -Wall -Wextra -Wno-attributes main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000 "$@"