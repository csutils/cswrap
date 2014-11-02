#!/bin/bash
source "$1/../testlib.sh"
set -x

# create hashed directories for compilers and wrappers
mkdir -p compiler wrapper
export PATH="$PWD/wrapper:$PWD/compiler:$PATH"

# create faked compilers and the corresponding symlinks to wrappers
for i in $(seq 8); do 
    cc="cc$i"
    data="$(seq -s ' ' 256)"
    printf '#!/bin/bash\nfor i in $(seq 8); do
    sleep .01 && printf "%%s: %%s\n" "%s" "%s" >&2
done\n' "$cc" "$data" > compiler/$cc
    chmod 0755 compiler/$cc || exit $?
    ln -fsv "$PATH_TO_CSWRAP" "wrapper/$cc" || exit $?
done

# set capture file
export CSWRAP_CAP_FILE="$PWD/cswrap.out"

# purge old capture file if any
rm -fv "$CSWRAP_CAP_FILE"

# run all (wrapped) compilers
for i in $(seq 8); do 
    cc="cc$i"
    "$cc" &
done

# wait for all (wrapped) compilers
for i in $(seq 8); do
    wait
done

# check that no diagnostic message has been missed
test 64 = "$(wc -l < "$CSWRAP_CAP_FILE")" || exit 1

# check that there is no interleaving of the error output
test 8 = "$(uniq "$CSWRAP_CAP_FILE" | wc -l)" || exit 1
