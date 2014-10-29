# Copyright (C) 2014 Red Hat, Inc.
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

cmake_minimum_required(VERSION 2.8)
project(cswrap C)
enable_testing()

# initialize pthreads
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)
if(CMAKE_USE_PTHREADS_INIT)
    add_definitions("-pthread")
endif()

# compile the executable and link with pthreads
add_executable(cswrap cswrap.c)
target_link_libraries(cswrap ${CMAKE_THREAD_LIBS_INIT})

# build the man page
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1
    COMMAND a2x -f manpage ${CMAKE_CURRENT_SOURCE_DIR}/cswrap.txt
    -D ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cswrap.txt
    COMMENT "Generating cswrap.1 man page...")
add_custom_target(doc ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1)

# install the exectuable and man page
install(TARGETS cswrap DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1 DESTINATION share/man/man1)

# run test-cases
file(GLOB runtests "${CMAKE_CURRENT_SOURCE_DIR}/tests/*/runtest.sh")
foreach(runtest ${runtests})
    get_filename_component(test_src_dir "${runtest}" PATH)
    get_filename_component(test_name "${test_src_dir}" NAME)

    # for each of them run: runtest.sh PATH_TO_CSWRAP TEST_DST_DIR TEST_SRC_DIR
    add_test("${test_name}" "${runtest}" "${test_src_dir}"
        "${CMAKE_CURRENT_BINARY_DIR}/tests/${test_name}/"
        "${CMAKE_CURRENT_BINARY_DIR}/cswrap")
endforeach()
