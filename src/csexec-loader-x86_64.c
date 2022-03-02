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

#include "csexec-loader-common.c"

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

    // fill exec_args[]
    prepare_exec_args(exec_args, argv, env);

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
