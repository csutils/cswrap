#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

int main(int argc, char **argv)
{
    /* ld.so ./executable corrupts auxiliary vector on armv7 and aarch64
       See https://sourceware.org/bugzilla/show_bug.cgi?id=23293
     */
#if !__arm__ && !__aarch64__
    const char *at_execfn = (const char *) getauxval(AT_EXECFN);
    assert(strcmp(at_execfn, argv[0]) != 0);
#endif

    puts(argv[0]);
}
