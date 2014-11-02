#!/bin/bash
source "$1/../testlib.sh"
set -x

COMPILERS="cc-ok cc-fail cc-slow"

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
printf 'echo $$ > cc-slow.pid\nsleep 64 &\ntrap "kill $!" EXIT\nwait $!\n' \
    >> compiler/cc-slow

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
cc-slow 2>cc-slow.out
test 143 = "$?" || exit 1
wait "$pid" || exit 1
grep '^cswrap: error: child .* terminated by signal 15$' cc-slow.out || exit 1


# kill the wrapper, which should forward the signal to the compiler
cc-slow 2>cc-slow.out &
pid="$!"
while sleep .1; do
    kill "$pid" && break
done
wait "$pid"
test 143 = "$?" || exit 1
grep '^cswrap: error: child .* terminated by signal 15$' cc-slow.out || exit 1

# set a timeout using the wrapper
export CSWRAP_TIMEOUT=1
for i in "" cc-slow foo:cc-slow:bar :::cc-slow:::; do
    export CSWRAP_TIMEOUT_FOR="$i"
    cc-ok || exit $?
    cc-fail && exit 1
    cc-slow 2>cc-slow.out
    test 143 = "$?" || exit 1
    grep '^cswrap: error: child .* terminated by signal 15 (timed out)$' \
        cc-slow.out || exit 1

    # there should be a valid PID file containing PID of the terminated process
    pid="$(<cc-slow.pid)"
    test 0 -lt "$pid" || exit 1
    kill "$pid" 2>/dev/null && exit 1
done

# all OK
exit 0
