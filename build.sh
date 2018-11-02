#!/usr/bin/env bash

mkdir -p build
cd build
export LLVM_DIR="/opt/llvm/5.0/lib/cmake"
cmake ..
make 

