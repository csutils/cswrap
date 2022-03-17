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

# run test-cases
file(GLOB runtests "${CMAKE_CURRENT_SOURCE_DIR}/*/runtest.sh")
foreach(runtest ${runtests})
    get_filename_component(test_src_dir "${runtest}" PATH)
    get_filename_component(test_name "${test_src_dir}" NAME)

    # for each of them run: runtest.sh TEST_SRC_DIR TEST_DST_DIR PATH_TO_CSWRAP
    add_test("${test_name}" "${runtest}" "${test_src_dir}"
        "${CMAKE_CURRENT_BINARY_DIR}/${test_name}/"
        "${CMAKE_BINARY_DIR}/src/cswrap")
endforeach()

# provide the 'check' target of make
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})
add_dependencies(check cswrap)
if(TARGET csexec)
    add_dependencies(check csexec csexec-loader csexec-preload)
endif()
