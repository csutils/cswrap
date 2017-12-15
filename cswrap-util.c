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

#include "cswrap-util.h"

#include <stddef.h>
#include <string.h>

void tag_process_name(const char *prefix, const int argc, char *argv[])
{
    /* obtain bounds of the array pointed by argv[] */
    char *beg = argv[0];
    char *end = argv[argc - 1];
    while (*end++)
        ;

    const size_t total = end - beg;
    const size_t prefix_len = strlen(prefix);
    if (total <= prefix_len)
        /* not enough space to insert the prefix */
        return;

    /* shift the contents by prefix_len to right and insert the prefix */
    memmove(beg + prefix_len, beg, total - prefix_len - 1U);
    memcpy(beg, prefix, prefix_len);
}
