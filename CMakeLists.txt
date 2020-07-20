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
add_definitions("-std=c99")

# compile the executable, link with pthreads, and install
add_executable(cswrap cswrap.c cswrap-util.c)
target_link_libraries(cswrap ${CMAKE_THREAD_LIBS_INIT})
if(CMAKE_USE_PTHREADS_INIT)
    set_target_properties(cswrap PROPERTIES COMPILE_FLAGS "-pthread")
endif()
install(TARGETS cswrap DESTINATION bin)

# build and install the man page (if asciidoc is available)
find_program(A2X a2x)
if(A2X)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1
        COMMAND a2x -f manpage ${CMAKE_CURRENT_SOURCE_DIR}/cswrap.txt
        -D ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cswrap.txt
        COMMENT "Generating cswrap.1 man page...")
    add_custom_target(doc ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cswrap.1
        DESTINATION share/man/man1)
endif()

# run test-cases
file(GLOB runtests "${CMAKE_CURRENT_SOURCE_DIR}/tests/*/runtest.sh")
foreach(runtest ${runtests})
    get_filename_component(test_src_dir "${runtest}" PATH)
    get_filename_component(test_name "${test_src_dir}" NAME)

    # for each of them run: runtest.sh TEST_SRC_DIR TEST_DST_DIR PATH_TO_CSWRAP
    add_test("${test_name}" "${runtest}" "${test_src_dir}"
        "${CMAKE_CURRENT_BINARY_DIR}/tests/${test_name}/"
        "${CMAKE_CURRENT_BINARY_DIR}/cswrap")
endforeach()

# provide the 'check' target of make
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})
add_dependencies(check cswrap)
