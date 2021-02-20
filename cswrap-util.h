/*
 * Copyright (C) 2013-2017 Red Hat, Inc.
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

#ifndef CSWRAP_UTIL_H
#define CSWRAP_UTIL_H

#include <stdbool.h>

#define STREQ(a, b) (!strcmp(a, b))

#define MATCH_PREFIX(str, pref) (!strncmp(str, pref, sizeof(pref) - 1U))

/* delete the given argument from the argv array */
void del_arg_from_argv(char **argv);

/* insert PREFIX at the begin ARGC/ARGV array to appear in process listing */
void tag_process_name(const char *prefix, const int argc, char *argv[]);

/* remove all $PATH items where TOOL can be found after symlink dereference */
bool remove_self_from_path(const char *tool, char *path, const char *wrap);

/* return true if the given name of an input file is black-listed */
bool is_black_listed_file(const char *name);

/* return true if the arg vector indicates we are invoked by an LTO wrapper */
bool invoked_by_lto_wrapper(char **argv);

#endif /* CSWRAP_UTIL_H */
