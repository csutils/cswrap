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

# run test-cases
file(GLOB runtests "${CMAKE_CURRENT_SOURCE_DIR}/*/runtest.sh")
foreach(runtest ${runtests})
    get_filename_component(test_src_dir "${runtest}" PATH)
    get_filename_component(test_name "${test_src_dir}" NAME)

    if(NOT TARGET csexec AND "${test_name}" MATCHES "csexec")
        continue()
    endif()

    # for each of them run: runtest.sh TEST_SRC_DIR TEST_DST_DIR PATH_TO_CSWRAP
    add_test("${test_name}" "${runtest}" "${test_src_dir}"
        "${CMAKE_CURRENT_BINARY_DIR}/${test_name}/"
        "${CMAKE_BINARY_DIR}/src/cswrap")
    set_tests_properties("${test_name}" PROPERTIES SKIP_RETURN_CODE 42)

    # TODO: we should use configure_file instead
    set_tests_properties("${test_name}" PROPERTIES ENVIRONMENT
                         "CC=${CMAKE_C_COMPILER};CC_ID=${CMAKE_C_COMPILER_ID}")
endforeach()

# provide the 'check' target of make
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} USES_TERMINAL)
add_dependencies(check cswrap)
if(TARGET csexec)
    add_dependencies(check csexec csexec-loader-tgt csexec-loader-test-tgt
                           csexec-preload)
endif()
