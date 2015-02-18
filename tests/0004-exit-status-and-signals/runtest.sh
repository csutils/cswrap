#!/bin/bash
source "$1/../testlib.sh"
set -x

COMPILERS="cc-ok cc-fail cc-slow clang not-clang"

# create hashed directories for compilers and wrappers
mkdir -p compiler wrapper
export PATH="$PWD/wrapper:$PWD/compiler:$PATH"

# create faked compilers
for i in $COMPILERS; do
    printf '#!/bin/bash\n' > compiler/$i
    chmod 0755 compiler/$i || exit $?
done
echo "true"     >> compiler/cc-ok
echo "false"    >> compiler/cc-fail
printf 'echo $$ > cc-slow.pid
$1 $2 &
trap "kill $! 2>/dev/null" EXIT
wait $!\n' \
    >> compiler/cc-slow
echo "$PWD/compiler/cc-slow \"\$@\"" | tee -a compiler/{,not-}clang >/dev/null

# create symlinks to compiler wrapper for all compilers
for i in $COMPILERS; do
    ln -fsv "$PATH_TO_CSWRAP" "wrapper/$i" || exit $?
done

# test propagation of zero exit code
cc-ok || exit $?

# test propagation of non-zero exit code
cc-fail && exit 1

# test invocation of a compiler not in $PATH
PATH="$PWD/wrapper" cc-ok && exit 1

install_killer() {
    while sleep .1; do
        pid="$(<cc-slow.pid)"
        test 0 -lt "$pid" || continue
        kill "$pid"
        exit $?
    done
}

# remove old pid file (if exists)
rm -f cc-slow.pid

# kill the compiler directly
install_killer &
pid="$!"
touch cscppc.c
cc-slow sleep 64 cscppc.c 2>cc-slow.out
test 143 = "$?" || exit 1
wait "$pid" || exit 1
grep '^cswrap: error: child .* terminated by signal 15$' cc-slow.out || exit 1
grep '^.*/cscppc.c: internal warning: child .* by signal 15 <--\[cc-slow\]$' \
    cc-slow.out || exit 1

# kill the wrapper, which should forward the signal to the compiler
not-clang sleep 64 csdiff.cc 2>cc-slow.out &
pid="$!"
while sleep .1; do
    kill "$pid" && break
done
wait "$pid"
test 143 = "$?" || exit 1
grep '^cswrap: error: child .* terminated by signal 15$' cc-slow.out || exit 1
grep '^csdiff.cc: internal warning: child.* terminated by signal 15$' \
    cc-slow.out || exit 1
sleep 1
kill "$(<cc-slow.pid)" || exit 1

# kill the wrapper of clang, which should kill its process group
clang sleep 64 main.cpp 2>clang.out &
pid="$!"
while sleep .1; do
    kill "$pid" && break
done
wait "$pid"
test 143 = "$?" || exit 1
grep '^cswrap: error: child .* terminated by signal 15$' clang.out || exit 1
grep '^main.cpp: internal warning: child .* terminated by signal 15$' \
    clang.out || exit 1
sleep 1
kill "$(<cc-slow.pid)" && exit 1

# set a timeout using the wrapper
export CSWRAP_TIMEOUT=1
for i in "" cc-slow foo:cc-slow:bar :::cc-slow:::; do
    export CSWRAP_TIMEOUT_FOR="$i"
    cc-ok || exit $?
    cc-fail && exit 1
    cc-slow sleep 64 file.C 2>cc-slow.out
    test 143 = "$?" || exit 1
    grep '^cswrap: error: child .* terminated by signal 15 (timed out)$' \
        cc-slow.out || exit 1
    grep '^file.C: internal warning: child .* timed out after 1s$' \
        cc-slow.out || exit 1

    # there should be a valid PID file containing PID of the terminated process
    pid="$(<cc-slow.pid)"
    test 0 -lt "$pid" || exit 1
    kill "$pid" 2>/dev/null && exit 1
done

export CSWRAP_TIMEOUT_FOR="clang++:clang:cppcheck"
cc-slow sleep 2 || exit 1

# all OK
exit 0
