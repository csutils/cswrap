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
    // read argc/argv from stack
    int argc;
    const char **argv;
    asm volatile (
        "ldr %w0, [x29, #16];"      // argc
        "add %1, x29, #24;"         // argv
        : "=r" (argc)               // %w0
        , "=r" (argv)               // %1
        ::);

    // pointer to a NULL-terminated list of environment variables
    const char **env = argc + argv + 1;

    // allocate exec_args[] on stack
    const char *exec_args[/* execfn */ 1 + argc + /* term */ 1];

    // fill exec_args[]
    prepare_exec_args(exec_args, argv, env);

    // execute syscall execve(CSEXEC_BIN, exec_args, env)
    // doc: syscall(2) https://man7.org/linux/man-pages/man2/syscall.2.html
    // and https://marcin.juszkiewicz.com.pl/download/tables/syscalls.html
    asm volatile (
        "mov w8, #221;"             // execve()
        "mov x0, %0;"               // CSEXEC_BIN
        "mov x1, %1;"               // exec_args
        "mov x2, %2;"               // env
        "svc #0;"                   // aarch64 syscall insn
        "mov x0, #0x7F;"            // execve() has failed, handle it as ENOENT
        "mov w8, #93;"              // exit()
        "svc #0" :
        : "r" (CSEXEC_BIN)          // %0
        , "r" (exec_args)           // %1
        , "r" (env)                 // %2
        : "x0", "x1", "x2", "w8",
        // needed for optimizer to see the contents of exec_args as still alive
        "memory");
}
