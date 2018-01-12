/*
 * Copyright (C) 2013-2014 Red Hat, Inc.
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

/* for sem_timedwait() */
#define _POSIX_C_SOURCE 200112L

#include "cswrap-util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>           /* for O_* constants */
#include <fnmatch.h>
#include <libgen.h>
#include <limits.h>
#include <semaphore.h>       /* for named semaphores */
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>            /* for time() */
#include <unistd.h>

#ifdef PATH_TO_WRAP
const char *path_to_wrap = PATH_TO_WRAP;
#else
const char *path_to_wrap = "";
#endif

static const char prog_name[] = "cswrap";

static unsigned tool_timeout;

struct strlist {
    const char          *str;
    struct strlist      *next;
};

/* true if we are wrapping clang invoked with --analyze */
static bool clang_analyzer;

/* how many non-translated lines should not be written to stderr */
static int suppress_plain_lines;

static struct strlist *file_list;

/* print a warning/error message */
static void print_msg(const char *what, const char *fmt, va_list ap)
{
    fprintf(stderr, "%s: %s: ", prog_name, what);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

/* print error and return EXIT_FAILURE */
static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_msg("error", fmt, ap);
    va_end(ap);
    return EXIT_FAILURE;
}

/* print a warning message */
static void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_msg("warning", fmt, ap);
    va_end(ap);
}

static int usage(char *argv[])
{
    fprintf(stderr, "Usage:\n\
    export PATH=\"`%s --print-path-to-wrap`:$PATH\"\n\n\
    %s is a generic compiler wrapper that translates relative paths to\n\
    absolute paths in diagnostic messages.  Create a symbolic link to %s\n\
    named as your compiler (gcc, g++, ...) and put it to your $PATH.\n\
    \n\
    %s --help prints this text to standard error output.\n\
    \n\
    %s --force-cap-file-unlock releases a stray lock (e.g. after crash)\n",
    prog_name, prog_name, prog_name, prog_name, prog_name);

    for (; *argv; ++argv)
        if (STREQ("--help", *argv))
            /* if the user really asks for --help, we have succeeded */
            return EXIT_SUCCESS;

    /* wrapper called directly, no argument matched */
    return EXIT_FAILURE;
}

static FILE *cap_file;
const char *cap_file_name;

sem_t *cap_file_lock;
static const char cap_file_lock_name[] = "/cswrap_cap_file_lock";

void init_cap_file_name(void)
{
    char *name = getenv("CSWRAP_CAP_FILE");
    if (name && name[0])
        cap_file_name = name;
}

bool lock_cap_file(void)
{
    static const char fail_msg[] = "failed to lock %s (%s)";

    for (;;) {
        /* obtain current time */
        const time_t t_beg = time(NULL);
        if ((time_t) -1 == t_beg) {
            fail(fail_msg, cap_file_lock_name, "time() failed");
            return false;
        }

        /* time-bounded lock */
        const time_t t_end = t_beg + /* block for [s] */ 15;
        const struct timespec timeout = {
            /* tv_sec   */ t_end,
            /* tv_nsec  */ 0L
        };
        if (0 == sem_timedwait(cap_file_lock, &timeout))
            /* semaphore successfully locked */
            return true;

        switch (errno) {
            case EINTR:
                /* interrupted by a signal ... try again! */
                continue;

            case ETIMEDOUT:
                /* we are waiting for someone eles to release it... */
                break;

            default:
                /* non-recoverable error ... giving up */
                fail(fail_msg, cap_file_lock_name, strerror(errno));
                return false;
        }

        /* be verbose in case we are waiting for the lock too long */
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        localtime_r(&t_end, &tm);
        warn("%04d-%02d-%02d %02d:%02d:%02d %s %s",
                /* date */ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                /* time */ tm.tm_hour, tm.tm_min, tm.tm_sec,
                "still trying to lock", cap_file_lock_name);
    }
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
    if (!lock_cap_file()) {
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

int force_cap_file_unlock(void)
{
    /* attempt to open an _existing_ semaphore */
    cap_file_lock = sem_open(cap_file_lock_name, 0);
    if (!cap_file_lock) {
        warn("failed to open %s (%s), retrying with O_CREAT...",
                cap_file_lock_name, strerror(errno));
        cap_file_lock = sem_open(cap_file_lock_name, O_CREAT, 0660, 0);
    }

    if (!cap_file_lock) {
        fail("failed to open %s (%s)", cap_file_lock_name, strerror(errno));
        return EXIT_FAILURE;
    }

    /* read the original value of the semaphore */
    int value_before = -1;
    sem_getvalue(cap_file_lock, &value_before);

    int status = EXIT_FAILURE;
    if (0 == value_before && unlock_cap_file())
        status = EXIT_SUCCESS;

    /* read the resulting value of the semaphore */
    int value_after = -1;
    sem_getvalue(cap_file_lock, &value_after);

    warn("attempted to unlock cap file: %d -> %d", value_before, value_after);

    /* close the semaphore and exit */
    close_cap_file_lock();
    return status;
}

/* return heap-allocated canonicalized tool name, NULL if not found */
static char* find_exe_in_dir(const char *base_name, const char *dir, bool *self)
{
    /* concatenate dirname and basename */
    char *raw_path;
    if (-1 == asprintf(&raw_path, "%s/%s", dir, base_name))
        return NULL;

    /* canonicalize the resulting path */
    char *exec_path = canonicalize_file_name(raw_path);
    if (exec_path) {
        /* check whether the file exists and we can execute it */
        if (-1 == access(exec_path, X_OK))
            goto fail;

        if (STREQ(prog_name, basename(exec_path))) {
            /* do not execute self in order not to end up in an infinite loop */
            *self = true;
            goto fail;
        }

        free(exec_path);
        return raw_path;
    }

fail:
    free(exec_path);
    free(raw_path);
    return NULL;
}

/* return heap-allocated canonicalized tool name, NULL if not found */
static char* find_tool_in_path(const char *base_name, char *path)
{
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
        else
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
        if (FO_DEL == op && !fnmatch(flag, *argv, /* flags */ 0))
            del_arg(argv);
        else {
            ++argc;
            ++argv;
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
    char *term;
    for (;; /* jump to the next flag */ slist = term + 1) {
        term = strchr(slist, ':');
        if (term == slist)
            /* flag resolved to an empty string, skip it! */
            continue;

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
    }
}

bool translate_args(char ***pargv, const char *base_name)
{
    if (STREQ(base_name, "cppcheck"))
        /* do not translate args for cppcheck */
        return true;

    /* branch by C/C++ based on base_name */
    const bool is_c = !strstr(base_name, "++");
    const char *del_env = (is_c) ? "CSWRAP_DEL_CFLAGS" : "CSWRAP_DEL_CXXFLAGS";
    const char *add_env = (is_c) ? "CSWRAP_ADD_CFLAGS" : "CSWRAP_ADD_CXXFLAGS";

    /* del/add flags as requested */
    return handle_cvar(pargv, FO_DEL, del_env)
        && handle_cvar(pargv, FO_ADD, add_env);
}

/* return true if cmd-line args suggest we are called by a configure script */
bool find_conftest_in_args(char **argv)
{
    for (; *argv; ++argv) {
        const char *arg = *argv;

        /* used by autoconf */
        if (STREQ(arg, "conftest.c"))
            return true;

        /* used by waf */
        if (STREQ(arg, "../test.c"))
            return true;
    }

    return false;
}

/* return true if there is a cmd-line argument equal to "--analyze" */
bool find_analyze_in_args(char **argv)
{
    for (; *argv; ++argv)
        if (STREQ(*argv, "--analyze"))
            return true;

    return false;
}

/* return true if msg matches "[0-9:] note: " and clang_analyzer is true */
bool clang_analyzer_note(const char *msg)
{
    if (!clang_analyzer)
        return false;

    for (; *msg; ++msg) {
        const unsigned char c = *msg;
        if (':' == c || isdigit(c))
            continue;

        if (' ' == c) {
            static const char note_prefix[] = " note: ";
            return !strncmp(msg, note_prefix, sizeof(note_prefix) - 1U);
        }
    }

    return false;
}

struct str_item {
    const char *str;
    size_t      len;
};

#define STR_ITEM(str) { str, sizeof(str) - 1U }

struct str_item msg_prefixes[] = {
    STR_ITEM("In file included from "),
    STR_ITEM("                 from "),
    { NULL, 0U }
};

void write_out(
        FILE                       *fp,
        char                       *buf_orig,
        char                       *buf,
        char                       *abs_path,
        char                       *colon,
        const char                 *tool)
{
    /* write the prefix if any */
    const char stash = buf[0];
    buf[0] = '\0';
    fputs(buf_orig, fp);
    buf[0] = stash;

    /* write absolute path */
    fputs(abs_path, fp);

    const size_t len = strlen(colon);
    if (!len || colon[len - 1U] != '\n')
        /* nothing useful remains to be printed */
        return;

    /* write the rest of the message without the trailing new-line */
    colon[len - 1U] = '\0';
    fputs(colon, fp);
    colon[len - 1U] = '\n';

    /* write the " <--[tool]" suffix */
    fprintf(fp, " <--[%s]\n", tool);
}

/* translate one line of a diagnostic message if an input file is matched */
bool translate_line(char *buf, const char *exclude)
{
    char *const buf_orig = buf;
    struct str_item *item;
    for (item = msg_prefixes; item->str; ++item) {
        if (!strncmp(buf, item->str, item->len)) {
            buf += item->len;
            break;
        }
    }

    char *colon = strchr(buf, ':');
    if (!colon)
        /* no colon in this line, skip it! */
        return false;

    /* temporarily replace the colon by zero */
    *colon = '\0';

    if (STREQ(buf, exclude)) {
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
    char *abs_path = canonicalize_file_name(buf);
    if (!abs_path && (ENOENT == errno) && (0 == access(buf, R_OK)))
        /* work around glibc/valgrind bug that causes realpath() to occasionally
         * fail with ENOENT for no reason */
        abs_path = canonicalize_file_name(buf);

    *colon = ':';
    if (!abs_path)
        return false;

    if (clang_analyzer_note(colon))
        /* suppress the code snippet that follows immediately after the note */
        suppress_plain_lines = 2;
    else {
        /* write the translated message to stderr */
        write_out(stderr, buf_orig, buf, abs_path, colon, /* tool */ exclude);
        suppress_plain_lines = 0;
    }

    if (init_cap_file_once())
        /* write the message also to capture file if the feature is enabled */
        write_out(cap_file, buf_orig, buf, abs_path, colon, /* tool */ exclude);

    free(abs_path);
    return true;
}

/* per-line handler of trans_paths_to_abs() */
void handle_line(char *buf, const char *exclude)
{
    if (translate_line(buf, exclude))
        return;

    if (0 < suppress_plain_lines)
        --suppress_plain_lines;
    else
        fputs(buf, stderr);

    if (init_cap_file_once())
        /* write the message also to capture file if the feature is enabled */
        fputs(buf, cap_file);
}

/* canonicalize paths the lines from stdin start with, write them to stderr */
void trans_paths_to_abs(const char *exclude)
{
    /* handle the input from stdin line by line */
    char *buf = NULL;
    size_t buf_size = 0;
    while (0 < getline(&buf, &buf_size, stdin))
        handle_line(buf, exclude);

    /* release line buffer */
    free(buf);
}

static volatile pid_t tool_pid;
static volatile sig_atomic_t use_pg;
static volatile sig_atomic_t timed_out;

void signal_handler(int signum)
{
    if (SIGCHLD == signum)
        // SIGCHLD handler is only set for getline() to be interrupted by EINTR
        return;

    if (!tool_pid)
        return;

    if (SIGALRM == signum) {
        /* timed out */
        signum = SIGTERM;
        timed_out = 1;
    }

    const pid_t kill_pid = (use_pg)
        ? /* clang process group */ -tool_pid
        : tool_pid;

    /* time elapsed, kill the tool now! */
    const int saved_errno = errno;
    kill(kill_pid, signum);
    errno = saved_errno;
}

static const struct sigaction sa = {
    .sa_handler = signal_handler
};

/* FIXME: copy/pasted from cppcheck-gcc */
bool install_signal_forwarder(void)
{
    static int forwarded_signals[] = {
        SIGCHLD,
        SIGINT,
        SIGPIPE,
        SIGQUIT,
        SIGTERM
    };

    static int forwarded_signals_cnt =
        sizeof(forwarded_signals)/
        sizeof(forwarded_signals[0]);

    /* install the handler for all forwarded signals */
    int i;
    for (i = 0; i < forwarded_signals_cnt; ++i)
        if (0 != sigaction(forwarded_signals[i], &sa, NULL))
            return false;

    return true;
}

bool timeout_disabled_for(const char *base_name, char *argv[])
{
    const char *str_list = getenv("CSWRAP_TIMEOUT_FOR");
    if (!str_list || !str_list[0])
        /* CSWRAP_TIMEOUT_FOR is unset or empty */
        return false;

    if (STREQ(base_name, "clang") || STREQ(base_name, "clang++")) {
        /* XXX: If $CSWRAP_TIMEOUT_FOR is set and we are wrapping clang or
         * clang++, do not set the timeout unless it runs as the analyzer.
         * Otherwise, we could unintententionally kill compiler or linker.
         * FIXME: Implement the timeout in cscppc/csclng instead. */
        for (; *argv; ++argv)
            if (STREQ(*argv, "--analyze"))
                break;
        if (!*argv)
            /* --analyze not found in argv[] --> assume compiler/linker */
            return true;
    }

    const size_t len = strlen(base_name);

    /* go through colon-separated list of programs in $CSWRAP_TIMEOUT_FOR */
    const char *prog = str_list;
    for (;;) {
        const char *term = strchr(prog, ':');
        if (!term)
            /* compare the last item in the list */
            return !STREQ(prog, base_name);

        if ((prog + len == term) && !strncmp(prog, base_name, len))
            /* timeout explicitly enabled for base_name */
            return false;

        prog = term + 1;
    }
}

int /* status */ install_timeout_handler(const char *base_name, char *argv[])
{
    const char *str_time = getenv("CSWRAP_TIMEOUT");
    if (!str_time || !str_time[0] || timeout_disabled_for(base_name, argv))
        /* no timeout requested */
        return EXIT_SUCCESS;

    /* parse the timeout value */
    long timeout;
    char c;
    if (1 != sscanf(str_time, "%li%c", &timeout, &c))
        return fail("unable to parse the value of $CSWRAP_TIMEOUT");

    if (UINT_MAX < timeout || timeout <= 0L)
        return fail("the value of $CSWRAP_TIMEOUT is out of range");

    /* install SIGALRM handler */
    if (0 != sigaction(SIGALRM, &sa, NULL))
        return fail("unable to install timeout handler");

    /* activate the timeout! */
    tool_timeout = timeout;
    alarm(tool_timeout);
    return EXIT_SUCCESS;
}

/* FIXME: copy/pasted from cscppc */
bool is_input_file_suffix(const char *suffix)
{
    return STREQ(suffix, "c")
        || STREQ(suffix, "C")
        || STREQ(suffix, "cc")
        || STREQ(suffix, "cpp")
        || STREQ(suffix, "cxx");
}

void emit_kill_msg(int signum, const char *base_name, const char *file)
{
    char *msg;
    static const char event[] = "internal warning";
    const int rv = (timed_out)
        ? asprintf(&msg, "%s: %s: child %d timed out after %us\n",
                file, event, tool_pid, tool_timeout)
        : asprintf(&msg, "%s: %s: child %d terminated by signal %d\n",
                file, event, tool_pid, signum);

    if (-1 == rv)
        /* OOM */
        return;

    handle_line(msg, base_name);
    free(msg);
}

void emit_kill_diagnostic(const int signum, const char *const base_name)
{
    struct strlist *it;
    for (it = file_list; it; it = it->next)
        emit_kill_msg(signum, base_name, it->str);
}

void collect_file_list(char **argv)
{
    /* iterate over input files and emit diagnost messages */
    const char *arg;
    for (; (arg = *argv); ++argv) {
        /* FIXME: copy/pasted from cscppc */
        const char *suffix = strrchr(arg, '.');
        if (!suffix)
            /* we require the file name to contain at least one dot */
            continue;

        /* skip behind the dot */
        ++suffix;

        /* check for a known input file suffix */
        if (!is_input_file_suffix(suffix))
            continue;

        /* allocate a new list item for the input file name */
        struct strlist *item = malloc(sizeof *item);
        if (!item)
            /* OOM */
            continue;

        /* allocate a new string for the input file name */
        item->str = strdup(arg);
        if (!item->str) {
            /* OOM */
            free(item);
            continue;
        }

        /* insert the item into the list */
        item->next = file_list;
        file_list = item;
    }
}

void destroy_file_list(void)
{
    while (file_list) {
        struct strlist *next = file_list->next;
        free((char *) file_list->str);
        free(file_list);
        file_list = next;
    }
}

static int handle_args(const int argc, char *argv[])
{
    if (argc != 2)
        /* unsupported count of args for a direct invocation */
        return usage(argv);

    if (STREQ("--force-cap-file-unlock", argv[1]))
        return force_cap_file_unlock();

    if (STREQ("--print-path-to-wrap", argv[1])) {
        printf("%s\n", path_to_wrap);
        return EXIT_SUCCESS;
    }

    /* arg not matched */
    return usage(argv);
}

int main(int argc, char *argv[])
{
    if (argc < 1)
        return fail("argc < 1");

    /* obtain base name of the executable being run */
    char *base_name = basename(argv[0]);
    if (STREQ(base_name, prog_name))
        return handle_args(argc, argv);

    /* duplicate the string as basename() return value is not valid forever */
    base_name = strdup(base_name);
    if (!base_name)
        return fail("strdup() failed");

    if (!install_signal_forwarder())
        return fail("unable to install signal forwarder");

    /* remove self from $PATH */
    char *path = getenv("PATH");
    remove_self_from_path(base_name, path, "cswrap");

    /* find the requested tool in $PATH */
    char *exec_path = find_tool_in_path(base_name, path);
    if (!exec_path) {
        fail("executable not found: %s (%s)", base_name, argv[0]);
        free(base_name);
        return EXIT_FAILURE;
    }

    if (find_conftest_in_args(argv)) {
        /* do not change anything when compiling conftest.c */
        execv(exec_path, argv);
        return fail("execv() failed: %s", strerror(errno));
    }

    /* create a process group for clang so that we can kill it later on */
    use_pg = STREQ(base_name, "clang")
        || STREQ(base_name, "clang++");
    if (use_pg)
        /* check whether clang is ivoked with --analyze */
        clang_analyzer = find_analyze_in_args(argv);

    /* collect all input file names and create a list of them */
    collect_file_list(argv);

    /* clone argv[] */
    char **argv_dup = clone_argv(argc, argv);
    if (!argv_dup)
        return fail("insufficient memory to clone argv[]");

    /* add/del C{,XX}FLAGS per $CSWRAP_C{,XX}FLAGS_{ADD,DEL} */
    if (!translate_args(&argv_dup, base_name)) {
        free(base_name);
        return fail("insufficient memory to append a flag");
    }

    /* create a pipe from stderr of the compiler to stdin of the filter */
    int pipefd[2];
    if (-1 == pipe(pipefd)) {
        free(exec_path);
        free(base_name);
        return fail("pipe() failed: %s", strerror(errno));
    }

    int status;
    tool_pid = fork();
    switch (tool_pid) {
        case -1:
            status = fail("fork() failed: %s", strerror(errno));
            break;

        case 0:
            if (use_pg)
                /* create a new process group for clang and its children */
                setpgid(0, 0);

            /* run the compiler and redirect its stderr to the pipe */
            close(pipefd[/* rd */ 0]);
            if (-1 == dup2(pipefd[/* wr */ 1], STDERR_FILENO)) {
                status = fail("unable to redirect stderr: %s", strerror(errno));
                break;
            }
            execv(exec_path, argv_dup);
            status = fail("execv() failed: %s", strerror(errno));
            break;

        default:
            /* run the filter and redirect its stdin from the pipe */
            close(pipefd[/* wr */ 1]);
            if (-1 == dup2(pipefd[/* rd */ 0], STDIN_FILENO)) {
                status = fail("unable to redirect stdin: %s", strerror(errno));
                break;
            }

            status = install_timeout_handler(base_name, argv);
            if (EXIT_SUCCESS != status)
                break;

            tag_process_name("[cswrap] ", argc, argv);

            /* check whether we should capture diagnostic messages to a file */
            init_cap_file_name();

            trans_paths_to_abs(/* exclude */ base_name);

            /* wait for the child to exit */
            while (-1 == wait(&status)) {
                if (EINTR != errno) {
                    status = fail("wait() failed: %s", strerror(errno));
                    goto cleanup;
                }
            }

            /* deactivate alarm (if any) */
            alarm(0U);

            if (WIFEXITED(status))
                /* propagate the exit status of the child */
                status = WEXITSTATUS(status);
            else if WIFSIGNALED(status) {
                const int signum = WTERMSIG(status);
                emit_kill_diagnostic(signum, base_name);
                const char *msg = "";
                if (timed_out)
                    msg = " (timed out)";
                fail("child %d (%s) terminated by signal %d%s", tool_pid,
                        exec_path, signum, msg);
                status = 0x80 + signum;
            }
            else
                status = fail("unexpected child status: %d", status);
    }

cleanup:
    /* close the capture file and release the lock in case it has been open */
    release_cap_file();

    destroy_file_list();
    free(exec_path);
    free(base_name);
    return status;
}
