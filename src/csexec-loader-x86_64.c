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

// path to csexec
#ifndef CSEXEC_BIN
#   define CSEXEC_BIN "/usr/bin/csexec"
#endif

// query real path of the executable
static const char* dig_execfn(const char **argv, const char **env_ptr)
{
    // auxiliary vector entries (System V AMD64 ABI: Process Initialization)
    // NOTE: if LD_SHOW_AUXV is exported, ld.so displays the auxiliary vector
    enum {
        AT_NULL     = 0,
        AT_EXECFN   = 31
    };
    struct aux {
        int         a_type;
        const char *a_value;
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
                return paux->a_value;

            default:
                // keep searching
                ++paux;
                continue;
        }
    }
}

// we do not start from main() to stay glibc-independent
void _start(void)
{
    // read argc/argv from stack (System V AMD64 ABI: Process Initialization)
    int argc;
    const char **argv;
    asm volatile (
        "mov 0x08(%%rbp), %0;"      // argc
        "lea 0x10(%%rbp), %1;"      // argv
        : "=r" (argc)               // %0
        , "=r" (argv)               // %1
        ::);

    // pointer to a NULL-terminated list of environment variables
    const char **env = argc + argv + 1;

    // allocate exec_args[] on stack
    const char *exec_args[/* execfn */ 1 + argc + /* term */ 1];
    int idx_dst = 0;

    // query real path of the executable
    exec_args[idx_dst++] = dig_execfn(argv, env);

    // shallow copy of argv[] at the end of exec_args[]
    int idx_src = 0;
    while (idx_src < argc)
        exec_args[idx_dst++] = argv[idx_src++];

    // terminate exec_args[]
    exec_args[idx_dst] = (void *) 0;

    // execute syscall execve(CSEXEC_BIN, exec_args, env)
    // doc: https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux
    // and https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64
    asm volatile (
        "mov $59, %%rax;"           // execve()
        "mov %0, %%rdi;"            // CSEXEC_BIN
        "mov %1, %%rsi;"            // exec_args
        "mov %2, %%rdx;"            // env
        "syscall;"                  // x86_64 syscall insn
        "mov $0x7F, %%rdi;"         // execve() has failed, handle it as ENOENT
        "mov $60, %%rax;"           // exit()
        "syscall" :
        : "r" (CSEXEC_BIN)          // %0
        , "r" (exec_args)           // %1
        , "r" (env)                 // %2
        : "rax", "rdi", "rsi", "rdx",
        // needed for optimizer to see the contents of exec_args as still alive
        "memory");
}
