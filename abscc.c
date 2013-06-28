/*
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * This file is part of abscc.
 *
 * abscc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * abscc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with abscc.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE 

#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char prog_name[] = "abscc";

/* print error and return EXIT_FAILURE */
static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    fprintf(stderr, "%s: error: ", prog_name);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    va_end(ap);
    return EXIT_FAILURE;
}

/* return heap-allocated canonicalized tool name, NULL if not found */
static char* find_exe_in_dir(const char *base_name, const char *dir, bool *self)
{
    /* concatenate dirname and basename */
    char *raw_path;
    if (-1 == asprintf(&raw_path, "%s/%s", dir, base_name))
        return NULL;

    /* canonicalize the resulting path */
    char *exec_path = realpath(raw_path, NULL);
    free(raw_path);
    if (!exec_path)
        return NULL;

    /* check whether the file exists and we can execute it */
    if (-1 == access(exec_path, X_OK))
        goto fail;

    if (!!strcmp(prog_name, basename(exec_path)))
        /* found! */
        return exec_path;

    /* avoid executing self so that we do not end up in an infinite loop */
    *self = true;

fail:
    free(exec_path);
    return NULL;
}

/* return heap-allocated canonicalized tool name, NULL if not found */
static char* find_tool_in_path(const char *base_name)
{
    /* read $PATH */
    char *path = getenv("PATH");
    if (!path)
        return NULL;

    /* go through all paths in $PATH */
    for (;;) {
        char *term = strchr(path, ':');
        if (term)
            /* temporarily replace the separator by zero */
            *term = '\0';

        bool self = false;
        char *exec_path = find_exe_in_dir(base_name, /* dir */ path, &self);

        if (term)
            /* restore the original separator */
            *term = ':';

        if (exec_path)
            /* found! */
            return exec_path;

        if (!term)
            /* this was the last path in $PATH */
            return NULL;

        /* jump to the next path in $PATH */
        char *const next = term + 1;

        if (self)
            /* remove self from $PATH */
            memmove(path, next, strlen(next));

        /* move the cursor */
        path = next;
    }
}

enum flag_op {
    FO_ADD,
    FO_DEL
};

/* delete an argument from the argv array */
void del_arg(char *argv[])
{
    int i;
    for (i = 0; argv[i]; ++i)
        argv[i] = argv[i + 1];
}

/* add/del a single flag from the argv array */
void handle_flag(char *argv[], const enum flag_op op, const char *flag)
{
    while (*argv) {
        if (!!strcmp(*argv, flag)) {
            /* not the flag we are looking for */
            ++argv;
            continue;
        }

        switch (op) {
            case FO_ADD:
                /* the flag is already there, this will be a no-op */
                return;

            case FO_DEL:
                del_arg(argv);
        }
    }

    if (FO_ADD == op)
        /* FIXME: this may cause invalid write or break termination of argv! */
        *argv = strdup(flag);
}

void handle_cvar(char *argv[], const enum flag_op op, const char *env_var_name)
{
    char *slist = getenv(env_var_name);
    if (!slist || !slist[0])
        return;

    /* go through all flags separated by ':' */
    for (;;) {
        char *term = strchr(slist, ':');
        if (term)
            /* temporarily replace the separator by zero */
            *term = '\0';

        /* go through the argument list */
        handle_flag(argv, op, slist);

        if (term)
            /* restore the original separator */
            *term = ':';

        if (!term)
            /* this was the last flag */
            break;

        /* jump to the next flag */
        slist = term + 1;
    }
}

void translate_args(char *argv[], const char *base_name)
{
    /* branch by C/C++ based on base_name */
    const bool is_c = !strstr(base_name, "++");
    handle_cvar(argv, FO_DEL, is_c ? "ABSCC_DEL_CFLAGS" : "ABSCC_DEL_CXXFLAGS");
    handle_cvar(argv, FO_ADD, is_c ? "ABSCC_ADD_CFLAGS" : "ABSCC_ADD_CXXFLAGS");
}

/* per-line handler of trans_paths_to_abs() */
bool handle_line(char *buf, const char *exclude)
{
    char *colon = strchr(buf, ':');
    if (!colon)
        /* no colon in this line, skip it! */
        return false;

    /* temporarily replace the colon by zero */
    *colon = '\0';

    if (!strcmp(buf, exclude)) {
        /* explicitly excluded, skip this! */
        *colon = ':';
        return false;
    }

    if (-1 == access(buf, R_OK)) {
        /* the part up to the colon does not specify a file we can access */
        *colon = ':';
        return false;
    }

    /* canonicalize the file name and restore the previously replaced colon */
    char *abs_path = realpath(buf, NULL);
    *colon = ':';
    if (!abs_path)
        return false;

    /* first write the canonicalized file name */
    fputs(abs_path, stderr);
    free(abs_path);

    /* then write the colon and the rest of the line unchanged */
    fputs(colon, stderr);
    return true;
}

/* canonicalize paths the lines from stdin start with, write them to stderr */
void trans_paths_to_abs(const char *exclude)
{
    /* handle the input from stdin line by line */
    char *buf = NULL;
    size_t buf_size = 0;
    while (0 < getline(&buf, &buf_size, stdin))
        if (!handle_line(buf, exclude))
            fputs(buf, stderr);

    /* release line buffer */
    free(buf);
}

int main(int argc, char *argv[])
{
    if (argc < 1)
        return fail("argc < 1");

    char *base_name = strdup(basename(argv[0]));
    if (!base_name)
        return fail("basename() failed");

    /* find the requested tool in $PATH */
    char *exec_path = find_tool_in_path(base_name);
    if (!exec_path) {
        fail("executable not found: %s (%s)", base_name, argv[0]);
        free(base_name);
        return EXIT_FAILURE;
    }

    /* FIXME: replace this by something useful */
    char **argv_dst = malloc(/* FIXME */ 0x1000 * sizeof(*argv));
    int i;
    for (i = 0; argv[i]; ++i)
        argv_dst[i] = argv[i];
    argv_dst[i] = NULL;
    argv = argv_dst;

    /* add/del C{,XX}FLAGS per $ABSCC_C{,XX}FLAGS_{ADD,DEL} */
    translate_args(argv, base_name);

    /* create a pipe from stderr of the compiler to stdin of the filter */
    int pipefd[2];
    if (-1 == pipe(pipefd)) {
        free(exec_path);
        free(base_name);
        return fail("pipe() failed: %s", strerror(errno));
    }

    int status;
    const pid_t pid = fork();
    switch (pid) {
        case -1:
            status = fail("fork() failed: %s", strerror(errno));
            break;

        case 0:
            /* run the compiler and redirect its stderr to the pipe */
            close(pipefd[/* rd */ 0]);
            if (-1 == dup2(pipefd[/* wr */ 1], STDERR_FILENO)) {
                status = fail("unable to redirect stderr: %s", strerror(errno));
                break;
            }
            execv(exec_path, argv);
            status = fail("execv() failed: %s", strerror(errno));
            break;

        default:
            /* run the filter and redirect its stdin from the pipe */
            close(pipefd[/* wr */ 1]);
            if (-1 == dup2(pipefd[/* rd */ 0], STDIN_FILENO)) {
                status = fail("unable to redirect stdin: %s", strerror(errno));
                break;
            }
            trans_paths_to_abs(/* exclude */ base_name);

            /* wait for the child to exit */
            while (-1 == wait(&status)) {
                if (EINTR != errno) {
                    status = fail("wait() failed: %s", strerror(errno));
                    break;
                }
            }

            if (WIFEXITED(status))
                /* propagate the exit status of the child */
                status = WEXITSTATUS(status);
            else
                status = fail("unexpected child status: %s", status);
    }

    free(exec_path);
    free(base_name);
    return status;
}
