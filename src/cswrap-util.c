/*
 * Copyright (C) 2013-2021 Red Hat, Inc.
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

#include "cswrap-util.h"

#include <limits.h>                 /* for PATH_MAX */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>                 /* for getcwd() */

/* return true if `str` ends with `suffix` */
static bool endsWith(const char *str, const char *suffix)
{
    const size_t len = strlen(str);
    const size_t suffixLen = strlen(suffix);
    if (len < suffixLen)
        return false;

    str += (len - suffixLen);
    return STREQ(str, suffix);
}

/* delete the given argument from the argv array */
void del_arg_from_argv(char **argv)
{
    int i;
    for (i = 0; argv[i]; ++i)
        argv[i] = argv[i + 1];
}

void tag_process_name(const char *prefix, const int argc, char *argv[])
{
    /* obtain bounds of the array pointed by argv[] */
    char *beg = argv[0];
    char *end = argv[argc - 1];
    while (*end++)
        ;

    const size_t total = end - beg;
    const size_t prefix_len = strlen(prefix);
    if (total <= prefix_len)
        /* not enough space to insert the prefix */
        return;

    /* shift the contents by prefix_len to right and insert the prefix */
    memmove(beg + prefix_len, beg, total - prefix_len - 1U);
    memcpy(beg, prefix, prefix_len);
}

/* return true if something was found/removed from path */
bool remove_self_from_path(const char *tool, char *path, const char *wrap)
{
    if (!path)
        return false;

    char *const path_orig = path;
    bool found = false;

    /* go through all paths in $PATH */
    char *term = path;
    while (term) {
        term = strchr(path, ':');
        if (term)
            /* temporarily replace the separator by zero */
            *term = '\0';

        /* concatenate dirname and basename */
        char *raw_path;
        if (!path[0])
            /* PATH="" is interpreted as PATH="." */
            raw_path = strdup(tool);
        else if (-1 == asprintf(&raw_path, "%s/%s", path, tool))
            raw_path = NULL;
        if (!raw_path)
            /* OOM */
            abort();

        /* compare the canonicalized basename with wrapper_name */
        char *exec_path = canonicalize_file_name(raw_path);
        const bool self = exec_path && STREQ(wrap, basename(exec_path));
        free(exec_path);
        free(raw_path);

        if ((path_orig != path) && !path[0])
            /* PATH=":foo" is interpreted as PATH=".:foo" */
            --path;

        /* jump to the next path in $PATH */
        char *const next = (term)
            ? (term + 1)
            : (path + strlen(path));

        if (self) {
            /* remove self from $PATH */
            memmove(path, next, 1U + strlen(next));
            found = true;
            continue;
        }

        if (term)
            /* restore the original separator */
            *term = ':';

        /* move the cursor */
        path = next;
    }

    return found;
}

static bool is_input_file_suffix(const char *suffix, const bool enable_cxx)
{
    if (STREQ(suffix, "c"))
        return true;

    if (!enable_cxx)
        return false;

    return STREQ(suffix, "C")
        || STREQ(suffix, "cc")
        || STREQ(suffix, "cpp")
        || STREQ(suffix, "cxx");
}

bool is_input_file(const char *arg, const bool enable_cxx)
{
    const char *suffix = strrchr(arg, '.');
    if (!suffix)
        /* we require the file name to contain at least one dot */
        return false;

    /* skip behind the dot */
    ++suffix;

    /* check for a known input file suffix */
    return is_input_file_suffix(suffix, enable_cxx);
}

bool is_ignored_file(const char *name)
{
    /* used by autoconf */
    if (STREQ(name, "conftest.c") || STREQ(name, "conftest.adb"))
        return true;

    /* used by cmake */
    if (strstr(name, "/CMakeTmp/") || strstr(name, "CMakeFiles/cmTC_"))
        return true;

    /* used by meson */
    if (strstr(name, "/meson-private/"))
        return true;

    /* used by waf */
    if (STREQ(name, "../test.c") || STREQ(name, "../../test.c"))
        return true;

    /* used by numpy */
    if (STREQ(name, "_configtest.c"))
        return true;

    /* used by qemu-guest-agent */
    if (STREQ(name, "config-temp/qemu-conf.c"))
        return true;

    /* used by kernel */
    if (STREQ(name, "scripts/kconfig/conf.c")
            || STREQ(name, "scripts/kconfig/zconf.tab.c"))
        return true;

    /* used by cov-build on first invocation of CC/CXX */
    if (MATCH_PREFIX(name, "/tmp/cov-mockbuild/"))
        return true;

    /* used by cov-build while instrumenting nvcc */
    if (endsWith(name, ".cudafe1.cpp"))
        return true;

    /* used by librdkafka-1.6.0 */
    if (MATCH_PREFIX(name, "_mkltmp"))
        return true;

    /* used by zlib */
    if (MATCH_PREFIX(name, "ztest")) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd) && MATCH_PREFIX(basename(cwd), "zlib"))
            return true;
    }

    /* config.(...)/ used by iproute */
    if (MATCH_PREFIX(name, "config.") && strchr(name, '/'))
        return true;

    /* try.c in UU/ - used by perl-5.26.2 */
    if (STREQ(name, "try.c")) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd) && STREQ("UU", basename(cwd)))
            return true;
    }

    /* linktest.c in exports/ - used by openblas-0.3.15 */
    if (STREQ(name, "linktest.c")) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd) && STREQ("exports", basename(cwd)))
            return true;
    }

    /* /config/auto-aux/... used by ocaml-4.07.0-4.el8 */
    if (STREQ(name, "async_io.c")
            || STREQ(name, "getgroups.c")
            || STREQ(name, "gethostbyaddr.c")
            || STREQ(name, "gethostbyname.c")
            || STREQ(name, "hasgot.c"))
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd) && strstr(cwd, "/config/auto-aux"))
            return true;
    }

    /* .conf_check_.../... used by libldb-1.5.4 */
    if (STREQ(name, "test.c.1.o") || STREQ(name, "../../main.c")) {
        char *abs_path = canonicalize_file_name(name);
        const bool matched = abs_path && strstr(abs_path, ".conf_check_");
        free(abs_path);
        if (matched)
            return true;
    }

    /* no match */
    return false;
}

/* check that the only argument is @/tmp/... */
bool invoked_by_lto_wrapper(char **argv)
{
    if (!argv || !argv[0] || !argv[1])
        return false;

    if (MATCH_PREFIX(argv[1], "@/tmp/"))
        /* is @/tmp/... the only arg? */
        return !argv[2];

    for (; *argv; ++argv)
        if (STREQ(*argv, "-xlto"))
            /* -xlto found in argv[] */
            return true;

    return false;
}

/* install signal handler hdl for signals in sig_list list terminated by 0 */
bool install_signal_handler(void (*hdl)(int), const int sig_list[])
{
    const struct sigaction sa = {
        .sa_handler = hdl
    };

    int i = 0;
    int signum;
    while ((signum = sig_list[i++]))
        if (0 != sigaction(signum, &sa, NULL))
            return false;

    return true;
}

