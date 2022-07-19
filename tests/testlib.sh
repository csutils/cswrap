# shellcheck shell=bash

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

# sanitizer build is used
grep -q "SANITIZERS:BOOL=ON" "$PATH_TO_CSEXEC_LIBS/../CMakeCache.txt"
HAS_SANITIZERS="$?"

if [[ "$HAS_SANITIZERS" -eq 0 ]]; then
    # make UBSan print whole stack traces
    export UBSAN_OPTIONS="print_stacktrace=1"

    # enable LSan suppresions for known leaks
    LSAN_OPTIONS="print_suppressions=0"
    LSAN_OPTIONS="$LSAN_OPTIONS,suppressions=$PATH_TO_CSEXEC_LIBS/../../src/lsan.supp"
    export LSAN_OPTIONS

    case "$CC_ID" in
        GNU)
            LIBASAN_PATH="$("$CC" -print-file-name=libasan.so)"

            # Fedora/RHEL's libasan.so is not a (symlink to a) dynamic library
            # but a linker script, so let's try to parse it instead.
            # https://bugzilla.redhat.com/show_bug.cgi?id=1923196
            if file -L "$LIBASAN_PATH" | grep 'ASCII text'; then
                LIBASAN_PATH="$(grep -oP '/.*\d' "$LIBASAN_PATH")"
            fi

            ;;
        Clang)
            LIBASAN_PATH="$("$CC" -print-file-name=libclang_rt.asan-"$(uname -m)".so)"
            ;;
        *)
            echo "Unknown compiler $CC_ID"
            exit 1
    esac

    export LIBASAN_PATH
fi

check_ld_so_flag() {
    LD_PRELOAD="$LIBASAN_PATH" \
    "$PATH_TO_CSEXEC_LIBS/csexec" --print-ld-exec-cmd argv0 | grep -qv -- "$1"
}

# ld.so takes --preload
check_ld_so_flag --preload
LD_LINUX_SO_TAKES_PRELOAD="$?"

# ld.so takes --argv0
check_ld_so_flag --argv0
LD_LINUX_SO_TAKES_ARGV0="$?"

# create $TEST_DST_DIR (if it does not exist already)
mkdir -p "$TEST_DST_DIR" || exit $?

# enter $TEST_DST_DIR
cd "$TEST_DST_DIR" || exit $?

# increase the possibility to catch use-after-free bugs
export MALLOC_PERTURB_=170
