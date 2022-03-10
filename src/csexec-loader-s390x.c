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
    // read argc/argv from stack (ELF Application Binary Interface s390x
    // Supplement 1.4. Process Initialization)
    // https://github.com/IBM/s390x-abi/releases/
    int argc;
    const char **argv;
    asm volatile (
        "lg %0, 160(%%r15);"        // argc
        "la %1, 168(%%r15);"        // argv
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
    // docs:
    //   * ELF Application Binary Interface s390x Supplement Parameter Passing
    //   * syscall(2) https://man7.org/linux/man-pages/man2/syscall.2.html
    //   * https://marcin.juszkiewicz.com.pl/download/tables/syscalls.html
    asm volatile (
        "lgfi %%r1, 11;"            // execve()
        "lgr %%r2, %0;"             // CSEXEC_BIN
        "lgr %%r3, %1;"             // exec_args
        "lgr %%r4, %2;"             // env
        "svc 0;"                    // s390{,x} syscall insn
        "lgfi %%r2, 0x7F;"          // execve() has failed, handle it as ENOENT
        "lgfi %%r1, 1;"             // exit()
        "svc 0" :
        : "r" (CSEXEC_BIN)          // %0
        , "r" (exec_args)           // %1
        , "r" (env)                 // %2
        : "r1", "r2", "r3", "r4",
        // needed for optimizer to see the contents of exec_args as still alive
        "memory");
}
