#!/bin/bash
source "$1/../testlib.sh"
set -x

# compile
gcc -Wl,--dynamic-linker="${PATH_TO_CSEXEC_LOADER}" -o out \
    "${TEST_SRC_DIR}/test.c"

# run
CSEXEC_WRAP_CMD="${TEST_SRC_DIR}/wrap.sh"     \
    LD_LIBRARY_PATH="${PATH_TO_CSEXEC_LIBS}"  \
    LD_PRELOAD="${LIBASAN_PATH}"              \
    ./out > stdout.txt 2> stderr.txt

# check
diff -u stderr.txt "${TEST_SRC_DIR}/stderr.txt" || exit 1
diff -u stdout.txt "${TEST_SRC_DIR}/stdout.txt" || exit 1
