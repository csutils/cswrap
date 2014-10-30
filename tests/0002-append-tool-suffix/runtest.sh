#!/bin/bash
source "$1/../testlib.sh"

export COMPILERS="gcc g++ clang clang++"
export MSG="/usr/include/stdlib.h:563:14: note: getenv() gets declared"

# create hashed directories for the compilers and wrappers
mkdir -p bin/{compiler,wrapper}
export PATH="$PWD/bin/wrapper:$PWD/bin/compiler:$PATH"

# create faked compilers
printf "%s\n" "$MSG" > "cswrap-input.txt" || exit $?
(
    cd bin/compiler || exit $?
    printf "#!/bin/sh\ncat '%s' >&2\n" "cswrap-input.txt" \
        | tee $COMPILERS \
        || exit $?
    chmod 0755 $COMPILERS || exit $?
)

# create symlinks to compiler wrapper for all compilers
for i in $COMPILERS; do
    ln -fsv "$PATH_TO_CSWRAP" "bin/wrapper/$i"
done

# run compilers one by one and check that the tool suffix was correctly appended
for i in $COMPILERS; do
    "$i" 2> "${i}-output.txt" || exit $?
    printf "%s <--[%s]\n" "$MSG" "$i" > "${i}-expected-output.txt" || exit $?
    diff -u ${i}{-expected,}-output.txt || exit $?
done
