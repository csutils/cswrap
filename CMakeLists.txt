# Copyright (C) 2014 - 2025 Red Hat, Inc.
#
# This file is part of cswrap.
#
# cswrap is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.
#
# cswrap is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with cswrap.  If not, see <http://www.gnu.org/licenses/>.

# using VERSION 3.3 or lower here causes cmake to link with -rdynamic
cmake_minimum_required(VERSION 3.5)
project(cswrap C)
enable_testing()

# build C code
add_subdirectory(src)

# build and install the man page (if asciidoc is available)
add_subdirectory(doc)

# run test-cases
add_subdirectory(tests)
