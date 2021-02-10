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

#include "cswrap-util.h"

#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// environment variable to read wrap_cmd from
#ifndef CSEXEC_WRAP_CMD_ENV_VAR_NAME
#   define CSEXEC_WRAP_CMD_ENV_VAR_NAME "CSEXEC_WRAP_CMD"
#endif

// executable of the dynamic linker (used as ELF interpreter)
#ifndef LD_LINUX_SO
#   define LD_LINUX_SO "/lib64/ld-linux-x86-64.so.2"
#endif

#ifndef LIBCSEXEC_PRELOAD_SO
#   define LIBCSEXEC_PRELOAD_SO "libcsexec-preload.so"
#endif

// set to 1 if dynamic linker supports the --argv0 option, as implemented with:
// https://sourceware.org/git/?p=glibc.git;a=commitdiff;h=c6702789
#ifndef LD_LINUX_SO_TAKES_ARGV0
#   define LD_LINUX_SO_TAKES_ARGV0 0
#endif

// count BEL-separated arguments in wrap_cmd (including the command name)
static int count_wrap_argc(const char *wrap_cmd)
{
    if (!wrap_cmd || !wrap_cmd[0])
        // wrap_cmd unset or empty
        return 0;

    // count non-empty command
    int wrap_argc = 1;

    // count BEL-separated args
    for (;;) {
        switch (*wrap_cmd) {
            case '\0':
                // end of string
                return wrap_argc;

            case '\a':
                // arg separator
                ++wrap_argc;
                // fall through!
            default:
                ++wrap_cmd;
                continue;
        }
    }
}

// read BEL-separated arguments from wrap_cmd (including the command name)
static bool read_wrap_cmd(char **exec_args, int *pidx, char *wrap_cmd)
{
    if (!wrap_cmd || !wrap_cmd[0])
        // wrap_cmd unset or empty
        return true;

    for (;;) {
        // seek BEL separator
        char *const sep = strchr(wrap_cmd, '\a');
        if (sep)
            // temporarily replace BEL by NUL
            *sep = '\0';

        // the memory is going to be leaked on the subsequent call of exec()
        char *const arg = strdup(wrap_cmd);
        if (!arg)
            return false;
        exec_args[(*pidx)++] = arg;

        if (!sep)
            // all args have been processed
            return true;

        // restore the temporarily overwritten BEL char
        *sep = '\a';

        // move behind the separator
        wrap_cmd = sep + 1;
    }
}

static void handle_shebang_exec(char *argv[])
{
    const char *exec_fn = argv[0];
    FILE *f = fopen(exec_fn, "r");
    if (!f)
        // exec_fn not readable
        return;

    char *buf;
    bool matched = !!fscanf(f, "#! %ms ", &buf);
    fclose(f);
    if (!matched)
        // exec_fn does not start with shebang
        return;

    // compare the interpreter specified with the shebang with exec_op
    char *exec_op = argv[1];
    matched = STREQ(buf, exec_op);
    free(buf);
    if (!matched)
        return;

    // use path to the real binary as exec_fn to keep ld.so functional
    argv[0] = exec_op;
}

// should be invoked as execve("/usr/bin/csexec", [EXECFN, ARG0, ...])
int main(int argc, char *argv[])
{
    // we require at least EXECFN (given as ARG0) and ARG0 (given as ARG1)
    if (argc < 2) {
        fprintf(stderr, "csexec: error: insufficient count of arguments\n");
        return /* command not executable */ 0x7E;
    }

    // if csexec-loader is invoked directly, do not pass it to LD_LINUX_SO
    const char *execfn_base = basename(argv[0]);
    if (STREQ("csexec-loader", execfn_base)) {
        fprintf(stderr, "csexec: error: refusing to execute %s\n", argv[0]);
        return /* command not executable */ 0x7E;
    }

    // canonicalize argv[] in case we are called via shebang
    handle_shebang_exec(argv);

    // compute the size of exec_args[]
    char *wrap_cmd = getenv(CSEXEC_WRAP_CMD_ENV_VAR_NAME);
    const int wrap_argc = count_wrap_argc(wrap_cmd);
    const int exec_args_size = wrap_argc + (/* LD_LINUX_SO */ 1 + 2 + 2) + argc;

    // allocate exec_args[] on stack
    char *exec_args[exec_args_size];
    int exec_args_offset = 0;
    int idx_dst = 0;

    // start with wrap_cmd if given
    if (!read_wrap_cmd(exec_args, &idx_dst, wrap_cmd)) {
        fprintf(stderr, "csexec: error: out of memory\n");
        return /* command not executable */ 0x7E;
    }

    // file to execute with execvp()
    const char *exec_file;

    // if ${CSEXEC_WRAP_CMD} starts with "--skip-ld-linux\a",
    // skip LD_LINUX_SO and directly run the command that follows
    if (1 < wrap_argc && STREQ("--skip-ld-linux", exec_args[0])) {
        exec_file = exec_args[/* real wrap_cmd */ 1];
        exec_args[++exec_args_offset] = argv[/* ARG0 */ 1];
    }
    else {
        // explicitly invoke dynamic linker
        exec_args[idx_dst++] = (char *) LD_LINUX_SO;
        exec_args[idx_dst++] = (char *) "--preload";
        exec_args[idx_dst++] = (char *) LIBCSEXEC_PRELOAD_SO;
#if LD_LINUX_SO_TAKES_ARGV0
        exec_args[idx_dst++] = (char *) "--argv0";
        exec_args[idx_dst++] = argv[/* ARG0 */ 1];
#endif
        // path to execute (either wrap_cmd or LD_LINUX_SO)
        exec_file = exec_args[0];
    }

    // path to the original binary
    exec_args[idx_dst++] = argv[/* EXECFN */ 0];

    // shallow copy of other command-line arguments
    int idx_src = 2;
    while (idx_src < argc)
        exec_args[idx_dst++] = argv[idx_src++];

    // terminate exec_args[]
    exec_args[idx_dst] = NULL;
    assert(idx_dst < exec_args_size);

    // execute exec_file passing it (exec_args[] + exec_args_offset) as argv[]
    execvp(exec_file, exec_args + exec_args_offset);

    // handle failure of execvp()
    const int errno_execvp = errno;
    fprintf(stderr, "cexec: error: failed to execute: %s (%s)\n",
            exec_file, strerror(errno));

    return (ENOENT == errno_execvp)
        ? /* command not found      */ 0x7F
        : /* command not executable */ 0x7E;
}
