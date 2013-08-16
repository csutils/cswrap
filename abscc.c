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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>           /* for O_* constants */
#include <libgen.h>
#include <semaphore.h>       /* for named semaphores */
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

static FILE *cap_file;
const char *cap_file_name;

sem_t *cap_file_lock;
static const char cap_file_lock_name[] = "/abscc_cap_file_lock";

void init_cap_file_name(void)
{
    char *name = getenv("ABSCC_CAP_FILE");
    if (name && name[0])
        cap_file_name = name;
}

bool unlock_cap_file(void)
{
    if (!sem_post(cap_file_lock))
        return true;

    fail("failed to unlock %s (%s)", cap_file_lock_name, strerror(errno));
    return false;
}

void close_cap_file_lock(void)
{
    assert(cap_file_lock);
    sem_close(cap_file_lock);
    cap_file_lock = NULL;
}

bool init_cap_file_once(void)
{
    if (cap_file)
        /* already initialized */
        return true;

    assert(!cap_file_lock);
    if (!cap_file_name)
        /* capture file not enabled */
        return false;

    /* TODO: check there are no interferences between mock chroots */
    cap_file_lock = sem_open(cap_file_lock_name, O_CREAT, 0660, 1);
    if (!cap_file_lock) {
        fail("failed to open %s (%s)", cap_file_lock_name, strerror(errno));
        return false;
    }

    /* wait for other processes to release the capture file lock */
    while (-1 == sem_wait(cap_file_lock)) {
        if (EAGAIN == errno)
            continue;

        fail("failed to lock %s (%s)", cap_file_lock_name, strerror(errno));
        close_cap_file_lock();
        return false;
    }

    /* open/create the capture file */
    cap_file = fopen(cap_file_name, "a");
    if (!cap_file) {
        fail("failed to open %s (%s)", cap_file_name, strerror(errno));
        unlock_cap_file();
        close_cap_file_lock();
        return false;
    }

    /* capture file successfully initialized */
    return true;
}

void release_cap_file(void)
{
    if (!cap_file)
        /* nothing to close */
        return;

    if (ferror(cap_file))
        fail("error writing to %s", cap_file_name);

    /* close the capture file */
    if (fclose(cap_file))
        fail("error closing %s (%s)", cap_file_name, strerror(errno));
    cap_file = NULL;

    /* unlock and release the lock */
    unlock_cap_file();
    close_cap_file_lock();
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

char** clone_argv(int argc, char *argv[])
{
    size_t size = (argc + 1) * sizeof(*argv);
    char **dup = malloc(size);
    if (!dup)
        return NULL;

    return memcpy(dup, argv, size);
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
bool handle_flag(char ***pargv, const enum flag_op op, const char *flag)
{
    int argc = 0;
    char **argv = *pargv;
    while (*argv) {
        if (!!strcmp(*argv, flag)) {
            /* not the flag we are looking for */
            ++argc;
            ++argv;
            continue;
        }

        switch (op) {
            case FO_ADD:
                /* the flag is already there, this will be a no-op */
                return true;

            case FO_DEL:
                del_arg(argv);
        }
    }

    if (FO_ADD != op)
        /* unless we are adding a new flag, we are done */
        return true;

    argv = realloc(*pargv, (argc + 2) * sizeof(*argv));
    if (argv)
        *pargv = argv;
    else {
        /* out of memory */
        free(*pargv);
        return false;
    }

    char *flag_dup = strdup(flag);
    if (!flag_dup)
        /* out of memory */
        return false;

    argv[argc] = flag_dup;
    argv[argc + 1] = NULL;
    return true;
}

bool handle_cvar(char ***pargv, const enum flag_op op, const char *env_var_name)
{
    char *slist = getenv(env_var_name);
    if (!slist || !slist[0])
        return true;

    /* go through all flags separated by ':' */
    for (;;) {
        char *term = strchr(slist, ':');
        if (term)
            /* temporarily replace the separator by zero */
            *term = '\0';

        /* go through the argument list */
        const bool ok = handle_flag(pargv, op, slist);

        if (term)
            /* restore the original separator */
            *term = ':';

        if (!ok)
            /* propagate the error back to the caller */
            return false;

        if (!term)
            /* this was the last flag */
            return true;

        /* jump to the next flag */
        slist = term + 1;
    }
}

bool translate_args(char ***pargv, const char *base_name)
{
    /* branch by C/C++ based on base_name */
    const bool is_c = !strstr(base_name, "++");
    const char *del_env = (is_c) ? "ABSCC_DEL_CFLAGS" : "ABSCC_DEL_CXXFLAGS";
    const char *add_env = (is_c) ? "ABSCC_ADD_CFLAGS" : "ABSCC_ADD_CXXFLAGS";

    /* del/add flags as requested */
    return handle_cvar(pargv, FO_DEL, del_env)
        && handle_cvar(pargv, FO_ADD, add_env);
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

    if (init_cap_file_once()) {
        /* write the message also to capture file if the feature is enabled */
        fputs(abs_path, cap_file);
        fputs(colon,    cap_file);
    }

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
    /* check whether we should capture diagnostic messages to a file */
    init_cap_file_name();

    /* handle the input from stdin line by line */
    char *buf = NULL;
    size_t buf_size = 0;
    while (0 < getline(&buf, &buf_size, stdin))
        if (!handle_line(buf, exclude))
            fputs(buf, stderr);

    /* release line buffer */
    free(buf);

    /* close the capture file and release the lock in case it has been open */
    release_cap_file();
}

int main(int argc, char *argv[])
{
    if (argc < 1)
        return fail("argc < 1");

    /* clone argv[] */
    argv = clone_argv(argc, argv);
    if (!argv)
        return fail("insufficient memory to clone argv[]");

    /* obtain base name of the executable being run */
    char *base_name = strdup(basename(argv[0]));
    if (!base_name)
        return fail("basename() failed");

    /* add/del C{,XX}FLAGS per $ABSCC_C{,XX}FLAGS_{ADD,DEL} */
    if (!translate_args(&argv, base_name)) {
        free(base_name);
        return fail("insufficient memory to append a flag");
    }

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
                    goto cleanup;
                }
            }

            if (WIFEXITED(status))
                /* propagate the exit status of the child */
                status = WEXITSTATUS(status);
            else
                status = fail("unexpected child status: %s", status);
    }

cleanup:
    free(exec_path);
    free(base_name);
    return status;
}
