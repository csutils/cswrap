#!/bin/bash
source "$1/../testlib.sh"
set -x

# decide whether a static build was used
IS_STATIC="$(grep STATIC_LINKING:BOOL=ON "${PATH_TO_CSEXEC_LIBS}/../CMakeCache.txt")"

# libcsexec-preload.so will NEVER be linked statically
FIND_WHAT="-not -name libcsexec-preload.so"

if [[ -z "${IS_STATIC}" ]]; then
    FIND_WHAT="$FIND_WHAT -and -name csexec-loader"
fi

while IFS= read -r file; do
    ldd "$file" 2>&1 | grep -E "statically linked|not a dynamic executable" \
        || { ldd "$file"; exit 1; }
done < <(find "${PATH_TO_CSEXEC_LIBS}" -maxdepth 1 -type f -executable ${FIND_WHAT})
