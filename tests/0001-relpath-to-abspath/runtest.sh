#!/bin/bash
source "$1/../testlib.sh"

# prepare the input for cswrap
sed -e "s|/builddir/build/BUILD/||" \
    "$TEST_SRC_DIR/test-data.txt" \
    > cswrap-input.txt

# prepare the expected output of cswrap
sed -e "s|/builddir/build/BUILD|$PWD|" \
    "$TEST_SRC_DIR/test-data.txt" \
    > cswrap-expected-output.txt

# create a dummy directory tree simulating the files being compiled
mkdir -p curl-7.15.5/{include/curl,lib,src}
touch curl-7.15.5/conftest.c{,c}
touch curl-7.15.5/include/curl/mprintf.h
touch curl-7.15.5/lib/{setup.h,{base64,connect,cookie,dict,easy,escape}.c}
touch curl-7.15.5/lib/{file,formdata,ftp,inet_ntop,ldap,netrc,progress,sendf}.c
touch curl-7.15.5/lib/host{ares,asyn,ip{,4,6},syn,thre}.c
touch curl-7.15.5/lib/http{,_{chunks,digest,negotiate,ntlm}}.c
touch curl-7.15.5/lib/{ssluse,strerror,telnet,transfer,url,version,tftp}.c
touch curl-7.15.5/src/{{main,urlglob,writeout}.c,{inet_htop,setup}.h}

# create hashed directories for the compiler and wrapper
mkdir -p bin/{compiler,wrapper}
export PATH="$PWD/bin/wrapper:$PWD/bin/compiler:$PATH"

# create fake compiler that lists cswrap-input.txt to stderr
printf "#!/bin/sh\ncat '%s' >&2\n" "cswrap-input.txt" > bin/compiler/gcc
chmod 0755 "bin/compiler/gcc"

# symlink compiler wrapper
ln -fsv "$PATH_TO_CSWRAP" bin/wrapper/gcc

do_test()
(
    "$@" gcc 2>&1 | sed 's| <--\[gcc\]$||' > cswrap-output.txt || exit $?
    diff -u cswrap{-expected,}-output.txt
)

# run the fake compiler through the wrapper and compare its output
do_test || exit $?

if valgrind --version; then
    # run the wrapper through valgrind if available
    # NOTE: we suppers uses of uninitialized values caused by glibc-static
    do_test valgrind -q --undef-value-errors=no --error-exitcode=7 || exit $?
fi
