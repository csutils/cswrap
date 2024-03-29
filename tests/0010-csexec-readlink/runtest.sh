#!/bin/bash
source "$1/../testlib.sh"
set -x

# skip if ld.so does NOT take --preload
[[ "${LD_LINUX_SO_TAKES_PRELOAD}" -eq 1 ]] || exit 42

# compile
gcc -Wl,--dynamic-linker="${PATH_TO_CSEXEC_LOADER}" -o out \
    "${TEST_SRC_DIR}/test.c"

# run
LD_LIBRARY_PATH="${PATH_TO_CSEXEC_LIBS}"  \
    LD_PRELOAD="${LIBASAN_PATH}"          \
    ./out > stdout.txt 2> stderr.txt

# check
diff -u stderr.txt "${TEST_SRC_DIR}/stderr.txt" || exit 1
grep -vsE '\/ld[^\/]*\.so\.[[:digit:]]$' stdout.txt || { cat stdout.txt; exit 1; }
