# Copyright (C) 2014 - 2021 Red Hat, Inc.
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

# build and install the man page (if asciidoc is available)
find_program(A2X a2x)
if(A2X)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1
        COMMAND a2x -f manpage ${CMAKE_CURRENT_SOURCE_DIR}/cswrap.adoc
        -D ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cswrap.adoc
        COMMENT "Generating cswrap.1 man page...")
    add_custom_target(doc ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1
        DESTINATION share/man/man1)
endif()
