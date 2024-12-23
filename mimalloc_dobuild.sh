#!/usr/bin/env sh

clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes -DUSE_MIMALLOC -lmimalloc main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000