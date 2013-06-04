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
