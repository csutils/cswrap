#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char path[PATH_MAX + 1] = { 0 };
    assert(readlink("/proc/self/exe", path, PATH_MAX) != -1);
    puts(path);
}
