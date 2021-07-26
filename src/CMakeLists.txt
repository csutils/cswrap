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

if(NOT DEFINED LIB_INSTALL_DIR)
  set(LIB_INSTALL_DIR lib)
endif()

# build C unit with manually specified compiler/linker flags
macro(build_custom_target dst src flags)
    set(cmd ${CMAKE_C_COMPILER} ${flags} -o
        ${CMAKE_CURRENT_BINARY_DIR}/${dst}
        ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    string(REPLACE ";" " " flags_string "${flags}")
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${dst}
        COMMAND ${cmd}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
        COMMENT "Building ${dst} with flags: ${flags_string}")
    add_custom_target(${dst}-target ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${dst})
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${dst} DESTINATION bin)
endmacro()

# build csexec only if supported by target (=host) architecture
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    message(STATUS "csexec for x86_64 will be built")
    add_executable(csexec csexec.c)
    install(TARGETS csexec DESTINATION bin)

    # readlink() wrapper
    add_library(csexec-preload SHARED csexec-preload.c)
    set_target_properties(csexec-preload PROPERTIES COMPILE_FLAGS "-pthread")
    set_target_properties(csexec-preload PROPERTIES LINK_FLAGS "-nostdlib")
    target_link_libraries(csexec-preload dl pthread)
    install(TARGETS csexec-preload DESTINATION ${LIB_INSTALL_DIR})

    # check whether /lib64/ld-linux-x86-64.so.2 takes --argv0, introduced with:
    # https://sourceware.org/git/?p=glibc.git;a=commitdiff;h=c6702789
    set(cmd "/lib64/ld-linux-x86-64.so.2 --argv0 XXX")
    set(cmd "${cmd} /usr/sbin/iconvconfig --usage")
    set(cmd "${cmd} | grep 'Usage: XXX' > /dev/null")
    execute_process(COMMAND sh -c "${cmd}"
        RESULT_VARIABLE LD_ARGV0_RESULT
        ERROR_QUIET)
    if("${LD_ARGV0_RESULT}" EQUAL 0)
        message(STATUS "/lib64/ld-linux-x86-64.so.2 takes --argv0")
        set(LD_ARGV0 ON)
    else()
        message(STATUS "/lib64/ld-linux-x86-64.so.2 does NOT take --argv0")
        set(LD_ARGV0 OFF)
    endif()
    option(LD_LINUX_SO_TAKES_ARGV0
        "Set to ON if ld-linux.so takes --argv0"
        "${LD_ARGV0}")
    if(${LD_LINUX_SO_TAKES_ARGV0})
        set_target_properties(csexec PROPERTIES
            COMPILE_FLAGS "-DLD_LINUX_SO_TAKES_ARGV0")
    endif()

    # csexec-loader (custom ELF interpreter) does not use any C run-time libs
    set(loader_flags -Wall -Wextra -g -O2 -fPIC -shared -nostdlib)
    build_custom_target(csexec-loader csexec-loader.c "${loader_flags}")
endif()
