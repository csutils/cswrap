/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * This file is part of cswrap.
 *
 * cswrap is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * cswrap is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cswrap.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE 

#include <dlfcn.h>
#include <errno.h>
#include <error.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// environment variable to read the pretended target of /proc/self/exe from
#ifndef CSEXEC_REAL_EXE_ENV_VAR_NAME
#   define CSEXEC_REAL_EXE_ENV_VAR_NAME "CSEXEC_REAL_EXE"
#endif

// executable of the dynamic linker (used as ELF interpreter)
#ifndef LD_LINUX_SO
#   define LD_LINUX_SO "/lib64/ld-linux-x86-64.so.2"
#endif

// address of the original canonicalize_file_name()
static char* (*orig_cfn)(const char *path);
static void init_orig_cfn(void)
{
    // resolve the original symbol of canonicalize_file_name()
    orig_cfn = dlsym(RTLD_NEXT, "canonicalize_file_name");
    if (orig_cfn)
        return;

    error(1, 0, "csexec-preload: failed to bind canonicalize_file_name(): %s",
            dlerror());
}

// address of the original readlink() we are going to wrap
static ssize_t (*orig_readlink)(const char *, char *, size_t);
static void init_orig_readlink(void)
{
    // resolve the original symbol of readlink()
    orig_readlink = dlsym(RTLD_NEXT, "readlink");
    if (orig_readlink)
        return;

    error(1, 0, "csexec-preload: failed to bind readlink(): %s", dlerror());
}

// real path of LD_LINUX_SO (which /proc/self/exe points at in case the ELF
// interpreter is invoked explicitly)
static char ld_so_real[0x100];
static void init_ld_so_real(void)
{
    // resolve real path of LD_LINUX_SO
    char *tmp = orig_cfn(LD_LINUX_SO);
    if (!tmp || sizeof ld_so_real <= strlen(tmp)) {
        error(1, errno, "csexec-preload: failed to canonicalize %s",
                LD_LINUX_SO);
    }

    // store the result to our static buffer and release the heap object
    strcpy(ld_so_real, tmp);
    free(tmp);
}

// initialize global variables on first call
static void init_once(void) {
    if (!orig_cfn) {
        // first call -> initialize global state
        init_orig_cfn();
        init_orig_readlink();
        init_ld_so_real();
    }
}

// return true if path is effectively /proc/self/exe
static bool is_self_exe(const char *path)
{
    if (!strcmp("/proc/self/exe", path))
        return true;

    char *str;
    if (-1 == asprintf(&str, "/proc/%d/exe", getpid()))
        // OOM
        return false;

    const bool matched = !strcmp(str, path);
    free(str);
    return matched;
}

// our wrapper of the original canonicalize_file_name()
char *canonicalize_file_name(const char *path)
{
    init_once();

    // call the real canonicalize_file_name() using the code pointer
    char *rv = orig_cfn(path);

    if (!rv || !!strcmp(ld_so_real, rv))
        // either failed or unrelated call to canonicalize_file_name()
        return rv;

    // check whether we should override the return value
    const char *real_exe = getenv(CSEXEC_REAL_EXE_ENV_VAR_NAME);
    if (!real_exe)
        return rv;

    // check whether path is something we should override
    if (!is_self_exe(path))
        return rv;

    // free the originally returned value and duplicate the pretended one
    free(rv);
    return strdup(real_exe);
}

// our wrapper of the original readlink()
ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    init_once();

    // call the real readlink() using the code pointer
    const ssize_t rv = orig_readlink(path, buf, bufsiz);

    if (rv < 0 || !!strncmp(ld_so_real, buf, rv))
        // either failed or unrelated call to readlink()
        return rv;

    // check whether we should override the return value
    const char *real_exe = getenv(CSEXEC_REAL_EXE_ENV_VAR_NAME);
    if (!real_exe)
        return rv;

    // check whether path is something we should override
    if (!is_self_exe(path))
        return rv;

    // copy the pretended value into the specified buffer and return its length
    strncpy(buf, real_exe, bufsiz);
    return strnlen(buf, bufsiz);
}
