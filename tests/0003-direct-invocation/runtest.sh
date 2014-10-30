#!/bin/bash
source "$1/../testlib.sh"

export PATH="$(dirname "$PATH_TO_CSWRAP"):$PATH"
set -x

# direct invocation without valid arguments should fail
cswrap                                  && exit 1
cswrap foo bar                          && exit 1
"$PATH_TO_CSWRAP"                       && exit 1
"$PATH_TO_CSWRAP" foo bar               && exit 1

# valid arguments --help and --print-path-to-wrap should succeed
cswrap --help                           || exit $?
cswrap --print-path-to-wrap             || exit $?
"$PATH_TO_CSWRAP" --help                || exit $?
"$PATH_TO_CSWRAP" --print-path-to-wrap  || exit $?

# this should not cause an infinite loop
ln -s "$PATH_TO_CSWRAP" ./cswrap
PATH=. cswrap && exit 1

# symlink to a tool not available in $PATH should fail with a diagnostic message
ln -s "$PATH_TO_CSWRAP" ./invalid
PATH= ./invalid 2> cswrap-error-otuput && exit 1
echo 'cswrap: error: executable not found: invalid (./invalid)' \
    | diff -u - cswrap-error-otuput || exit $?
PATH=.  invalid 2> cswrap-error-otuput && exit 1
echo 'cswrap: error: executable not found: invalid (invalid)' \
    | diff -u - cswrap-error-otuput || exit $?
