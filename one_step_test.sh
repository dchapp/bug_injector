#!/usr/bin/env bash
CC=clang
error_lib="./error_lib/error_lib.c"

# First build the LLVM pass 
./build.sh
pass_lib="./build/bug_injector/libBugInjectorPass.so"
pass_options="-Xclang -load -Xclang $pass_lib"

# Compile the error library code
$CC -c $error_lib -o "./error_lib/error_lib.o"

# Compile demo code with error injection pass
$CC $pass_options -c "./test/demo.c" -o "./test/demo.o"

# Link 
$CC "./test/demo.o" "./error_lib/error_lib.o" -o "./test/demo.exe"

# Run
"./test/demo.exe"


