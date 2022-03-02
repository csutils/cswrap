# Copyright (C) 2014 - 2022 Red Hat, Inc.
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

# set standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# link to libc statically
option(STATIC_LINKING "Link to libc statically" OFF)
if(STATIC_LINKING)
    # libcsexec-loader.so will STILL be linked dynamically
    set(CMAKE_EXE_LINKER_FLAGS "-static ${CMAKE_EXE_LINKER_FLAGS}")
endif()

# initialize pthreads
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
if(NOT CMAKE_USE_PTHREADS_INIT)
    message(FATAL_ERROR "${CMAKE_THREAD_LIBS_INIT} is not pthread compatible!")
endif()

include(GNUInstallDirs)

# compile the executable, link with pthreads, and install
add_executable(cswrap cswrap.c cswrap-util.c)
if(PATH_TO_WRAP)
    target_compile_definitions(cswrap PRIVATE -DPATH_TO_WRAP=${PATH_TO_WRAP})
endif()
target_link_libraries(cswrap PRIVATE Threads::Threads)
install(TARGETS cswrap DESTINATION ${CMAKE_INSTALL_BINDIR})

# build C unit with manually specified compiler/linker flags
macro(build_custom_target dst src deps flags)
    set(cmd ${CMAKE_C_COMPILER} ${flags} -o
        ${CMAKE_CURRENT_BINARY_DIR}/${dst}
        ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    string(REPLACE ";" " " flags_string "${flags}")
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${dst}
        COMMAND ${cmd}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${deps}
        COMMENT "Building ${dst} with flags: ${flags_string}"
        VERBATIM)
    add_custom_target(${dst} ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${dst})
endmacro()

# check if ld-linux.so takes the given option and define corresponding macros
macro(check_ld_flag cmd varname)
    string(TOUPPER "${varname}" VARNAME)
    execute_process(COMMAND sh -c "${cmd}"
        RESULT_VARIABLE LD_${VARNAME}_RESULT
        OUTPUT_QUIET ERROR_QUIET)
    if("${LD_${VARNAME}_RESULT}" EQUAL 0)
        message(STATUS "${LD_LINUX_SO} takes --${varname}")
        set(LD_${VARNAME} ON)
    else()
        message(STATUS "${LD_LINUX_SO} does NOT take --${varname}")
        set(LD_${VARNAME} OFF)
    endif()
    option(LD_LINUX_SO_TAKES_${VARNAME}
        "Set to ON if dynamic linker takes --${varname}"
        "${LD_${VARNAME}}")
    if(${LD_LINUX_SO_TAKES_${VARNAME}})
        target_compile_definitions(csexec
            PRIVATE "-DLD_LINUX_SO_TAKES_${VARNAME}")
    endif()
endmacro()

# build csexec only if supported by target (=host) architecture
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")

# based on https://sourceware.org/glibc/wiki/ABIList
if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    # 64-bit, hard-float, LE: /lib64/ld-linux-x86-64.so.2
    set(LD_LINUX_SO "/lib64/ld-linux-x86-64.so.2")
endif()

if(LD_LINUX_SO)
    message(STATUS "csexec for ${CMAKE_SYSTEM_PROCESSOR} will be built")
    message(STATUS "Dynamic linker: ${LD_LINUX_SO}")

    if(NOT EXISTS "${LD_LINUX_SO}")
        message(FATAL_ERROR "${LD_LINUX_SO} is not a valid path to dynamic \
                             linker on this system!")
    endif()

    add_executable(csexec csexec.c)
    target_compile_definitions(csexec PRIVATE LD_LINUX_SO="${LD_LINUX_SO}")
    install(TARGETS csexec DESTINATION ${CMAKE_INSTALL_BINDIR})

    # readlink() wrapper
    add_library(csexec-preload SHARED csexec-preload.c)
    target_compile_definitions(csexec-preload PRIVATE LD_LINUX_SO="${LD_LINUX_SO}")
    target_link_libraries(csexec-preload PRIVATE ${CMAKE_DL_LIBS}
                                                 Threads::Threads)
    install(TARGETS csexec-preload DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # check whether ${LD_LINUX_SO} takes --preload, introduced with:
    # https://sourceware.org/git/?p=glibc.git;a=commitdiff;h=8692ebdb
    set(cmd "${LD_LINUX_SO} --preload ${LD_LINUX_SO} /usr/bin/echo")
    check_ld_flag("${cmd}" "preload")

    # check whether ${LD_LINUX_SO} takes --argv0, introduced with:
    # https://sourceware.org/git/?p=glibc.git;a=commitdiff;h=c6702789
    set(cmd "${LD_LINUX_SO} --argv0 XXX")
    set(cmd "${cmd} /usr/sbin/iconvconfig --usage")
    set(cmd "${cmd} | grep 'Usage: XXX'")
    check_ld_flag("${cmd}" "argv0")

    # csexec-loader (custom ELF interpreter) does not use any C run-time libs
    set(loader_flags -Wall -Wextra -g -O2 -fPIC -shared -nostdlib)

    # main build
    build_custom_target(csexec-loader csexec-loader-${CMAKE_SYSTEM_PROCESSOR}.c
        ${CMAKE_CURRENT_SOURCE_DIR}/csexec-loader-common.c
        "${loader_flags};-DCSEXEC_BIN=\"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/csexec\"")
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/csexec-loader"
            DESTINATION ${CMAKE_INSTALL_BINDIR})

    # testing build
    build_custom_target(csexec-loader-test csexec-loader-${CMAKE_SYSTEM_PROCESSOR}.c
        ${CMAKE_CURRENT_SOURCE_DIR}/csexec-loader-common.c
        "${loader_flags};-DCSEXEC_BIN=\"${CMAKE_CURRENT_BINARY_DIR}/csexec\"")
endif()
