#!/usr/bin/env sh

clang++ main.cpp -O3 -frandom-seed=constant_seed -Wno-attributes -fuse-ld=lld -flto -mllvm -inline-threshold=10000
