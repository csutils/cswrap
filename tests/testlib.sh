# test-case library code (needs to be sourced at the beginning of runtest.sh)

# path to the script itself
PATH_TO_SELF="$0"

# path to the directory containing runtest.sh
TEST_SRC_DIR="$1"

# path to a read-write directory that can be used for testing
TEST_DST_DIR="$2"

# path to the cswrap binary
PATH_TO_CSWRAP="$3"

# path to a directory containing libraries and executables
PATH_TO_CSEXEC_LIBS="$(dirname "$3")"

# path to the csexec-loader binary
PATH_TO_CSEXEC_LOADER="$(dirname "$3")/csexec-loader-test"

# ld.so takes --preload
("${PATH_TO_CSEXEC_LIBS}/csexec" --print-ld-exec-cmd argv0 | grep -qv -- --preload)
LD_LINUX_SO_TAKES_PRELOAD="$?"

# ld.so takes --argv0
("${PATH_TO_CSEXEC_LIBS}/csexec" --print-ld-exec-cmd argv0 | grep -qv -- --argv0)
LD_LINUX_SO_TAKES_ARGV0="$?"

# create $TEST_DST_DIR (if it does not exist already)
mkdir -p "$TEST_DST_DIR" || exit $?

# enter $TEST_DST_DIR
cd "$TEST_DST_DIR" || exit $?

# increase the possibility to catch use-after-free bugs
export MALLOC_PERTURB_=170
