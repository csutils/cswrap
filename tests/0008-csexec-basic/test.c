#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const char *val = getenv("CSEXEC_WRAP_CMD");
    assert(val);
    puts("binary: CSEXEC_WRAP_CMD exists");
}
