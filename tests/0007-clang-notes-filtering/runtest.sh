#!/bin/bash
source "$1/../testlib.sh"
set -x

# create hashed directories for compilers and wrappers
mkdir -p compiler wrapper
export PATH="$PWD/wrapper:$PWD/compiler:$PATH"

for cc in gcc clang{,++} ; do
    printf "#!/bin/sh\ncat '${TEST_SRC_DIR}/input.err' >&2\n" > "compiler/$cc"
    chmod 0755 "compiler/$cc"
    ln -fsv "$PATH_TO_CSWRAP" "wrapper/$cc" || exit $?
done

# create fake source files
mkdir -p src
touch src/{browser,files}.c

# set capture file
export CSWRAP_CAP_FILE="$PWD/cswrap.out"

# try all compilers
for cc in gcc clang{,++} ; do

    # try them with -c and --analyze
    for arg in -c --analyze ; do
        # purge old capture file if any
        rm -fv "$CSWRAP_CAP_FILE"

        base="${cc}${arg}"
        "$cc" "$arg" 2> "stderr.txt"

        # translate absolute paths back to relative ones
        sed -e "s|^${PWD}/||" -i cswrap.out stderr.txt

        diff -u cswrap.out "${TEST_SRC_DIR}/${base}-cswrap.out" || exit 1
        diff -u stderr.txt "${TEST_SRC_DIR}/${base}-stderr.txt" || exit 1
    done
done
