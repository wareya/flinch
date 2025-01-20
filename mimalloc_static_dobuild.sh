#!/usr/bin/env sh

clang++ -g -ggdb -Wall -Wextra -Wno-attributes -DUSE_MIMALLOC -static -lmimalloc-static main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000