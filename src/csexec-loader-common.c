/*
 * Copyright (C) 2020 - 2022 Red Hat, Inc.
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

#define NULL ((void *) 0)

// path to csexec
#ifndef CSEXEC_BIN
#   define CSEXEC_BIN "/usr/bin/csexec"
#endif

// query real path of the executable
static const char* dig_execfn(const char **argv, const char **env_ptr)
{
    // auxiliary vector entries
    // NOTE: if LD_SHOW_AUXV is exported, ld.so displays the auxiliary vector
    enum {
        AT_NULL     = 0,
        AT_EXECFN   = 31
    };

    // definition of Elf64_auxv_t in glibc's elf/elf.h
    struct aux {
        unsigned long     a_type;
        union {
            unsigned long a_val;
        };
    };

    // skip environment variables, the auxiliary vector is just behind them
    while ((*env_ptr++));
    const struct aux *paux = (const void *) env_ptr;

    // go through auxiliary vector entries in the initial process stack
    for (;;) {
        switch (paux->a_type) {
            case AT_NULL:
                // not found --> fall back to argv[0]
                return argv[0];

            case AT_EXECFN:
                // found!
                return (const char *) paux->a_val;

            default:
                // keep searching
                ++paux;
                continue;
        }
    }
}


// fill exec_args for csexec
static void prepare_exec_args(const char **exec_args,
                              const char **argv,
                              const char **env_ptr)
{
    int idx_dst = 0;

    // query real path of the executable
    exec_args[idx_dst++] = dig_execfn(argv, env_ptr);

    // shallow copy of argv[] at the end of exec_args[]
    int idx_src = 0;
    while (argv[idx_src] != NULL)
        exec_args[idx_dst++] = argv[idx_src++];

    // terminate exec_args[]
    exec_args[idx_dst] = NULL;
}
