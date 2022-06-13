#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

int main(int argc, char **argv)
{
    /* ld.so version 2.33 or older corrupts auxiliary vector on armv7
     * or aarch64.  See https://sourceware.org/bugzilla/show_bug.cgi?id=23293
     */
#if (!__arm__ && !__aarch64__) || __GLIBC__ > 2 || \
        (__GLIBC__ == 2 && __GLIBC_MINOR__ > 33)
    const char *at_execfn = (const char *) getauxval(AT_EXECFN);
    assert(strcmp(at_execfn, argv[0]) != 0);
#endif

    puts(argv[0]);
}
