/*
 * Copyright (C) 2022 Red Hat, Inc.
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
    // FIXME:
    // According to Power Architecture 64-Bit ELF V2 ABI Specification
    // 4.1.2 Process Initialization the location and relative order of
    // argc, argv, envp and auvx are not specified.
    //
    // However, the Linux kernel seems to use a similar layout as for x86_64.

    // read argc/argv from stack
    int argc;
    const char **argv;

    asm volatile (
        "lwz %0, 48(%%r31);"        // argc
        "addi %1, %%r31, 56;"       // argv
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
    // doc:
    //   * Power Architecture 64-Bit ELF V2 ABI Specification 2.2
    //   * syscall(2) https://man7.org/linux/man-pages/man2/syscall.2.html
    //   * https://marcin.juszkiewicz.com.pl/download/tables/syscalls.html
    asm volatile (
        "li %%r0, 11;"              // execve()
        "mr %%r3, %0;"              // CSEXEC_BIN
        "mr %%r4, %1;"              // exec_args
        "mr %%r5, %2;"              // env
        "sc;"                       // ppc64 syscall insn
        "li %%r3, $0x7F;"           // execve() has failed, handle it as ENOENT
        "mr %%r0, 1;"               // exit()
        "sc" :
        : "r" (CSEXEC_BIN)          // %0
        , "r" (exec_args)           // %1
        , "r" (env)                 // %2
        : "r0", "r3", "r4", "r5",
        // needed for optimizer to see the contents of exec_args as still alive
        "memory");
}
