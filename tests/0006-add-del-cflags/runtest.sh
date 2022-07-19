#!/bin/bash
source "$1/../testlib.sh"
set -x

COMPILERS="gcc g++"

# create hashed directories for compilers and wrappers
mkdir -p compiler wrapper
export PATH="$PWD/wrapper:$PWD/compiler:$PATH"

# create faked compilers and the corresponding symlinks to wrappers
for i in $COMPILERS; do 
    printf '#!/bin/bash\nprintf "%%s\\n" "$*"\n' > "compiler/$i"
    chmod 0755 "compiler/$i" || exit $?
    ln -fsv "$PATH_TO_CSWRAP" "wrapper/$i" || exit $?
done

DEL_COMMON_FLAGS="-Werror:-fdiagnostics-color:-fdiagnostics-color=always"
ADD_COMMON_FLAGS="-Wall:-Wextra"

export CSWRAP_ADD_CFLAGS="::$ADD_COMMON_FLAGS:-Wno-unknown-pragmas:-Wstrict-prototypes"
export CSWRAP_ADD_CXXFLAGS="$ADD_COMMON_FLAGS:-Wctor-dtor-privacy:-Woverloaded-virtual"
export CSWRAP_DEL_CFLAGS="::$DEL_COMMON_FLAGS"
export CSWRAP_DEL_CXXFLAGS="$DEL_COMMON_FLAGS::"

# run cswrap through valgrind if available
EXEC_PREFIX=
if [[ "$HAS_SANITIZERS" -eq 1 ]] && valgrind --version; then
    EXEC_PREFIX="valgrind -q --undef-value-errors=no --error-exitcode=7"
fi

do_check() {
    printf "%s\n" "$3" > expected-flags.txt     || exit $?
    $EXEC_PREFIX "$1" $2 > actual-flags.txt     || exit $?
    diff -u expected-flags.txt actual-flags.txt || exit $?
}

# adding flags to empty argv
do_check gcc "" "-Wall -Wextra -Wno-unknown-pragmas -Wstrict-prototypes"
do_check g++ "" "-Wall -Wextra -Wctor-dtor-privacy -Woverloaded-virtual"

# removing all flags from argv
do_check gcc "-Werror -fdiagnostics-color=always" "-Wall -Wextra -Wno-unknown-pragmas -Wstrict-prototypes"
do_check g++ "-Werror -fdiagnostics-color"        "-Wall -Wextra -Wctor-dtor-privacy -Woverloaded-virtual"

# preserving an original flag
do_check gcc "-Wxxx -Werror" "-Wxxx -Wall -Wextra -Wno-unknown-pragmas -Wstrict-prototypes"
do_check g++ "-Werror -Wxxx" "-Wxxx -Wall -Wextra -Wctor-dtor-privacy -Woverloaded-virtual"

# removing multiple occurrences of a flag
do_check gcc "-Werror -g -O0 -Werror" "-g -O0 -Wall -Wextra -Wno-unknown-pragmas -Wstrict-prototypes"

# removing compiler flags using a wildcard pattern
CSWRAP_DEL_CFLAGS="-Werror*" do_check gcc "-Werror=format-security -g -O0 -Werror=deprecated-declarations" "-g -O0 -Wall -Wextra -Wno-unknown-pragmas -Wstrict-prototypes"

# adding a flag that is already there
do_check g++ "-Wextra -g -O0 -Wno-extra" "-Wextra -g -O0 -Wno-extra -Wall -Wextra -Wctor-dtor-privacy -Woverloaded-virtual"

# preserving flags for conftest.c
do_check gcc "conftest.c -Werror" "conftest.c -Werror"
