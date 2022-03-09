#!/bin/bash
source "$1/../testlib.sh"
set -x

# skip if ld.so does NOT take --argv0
[[ "${LD_LINUX_SO_TAKES_ARGV0}" -eq 1 ]] || exit 42

# compile
gcc -Wl,--dynamic-linker="${PATH_TO_CSEXEC_LOADER}" -o out \
    "${TEST_SRC_DIR}/test.c"

# run
ARGVO='hopefully, nothing like this is in $PATH'
command -v "$ARGVO" && exit 1
export LD_LIBRARY_PATH="${PATH_TO_CSEXEC_LIBS}"
(exec -a "$ARGVO" ./out > stdout.txt 2> stderr.txt)

diff -u stdout.txt "${TEST_SRC_DIR}/stdout.txt" || exit 1
diff -u stderr.txt "${TEST_SRC_DIR}/stderr.txt" || exit 1
