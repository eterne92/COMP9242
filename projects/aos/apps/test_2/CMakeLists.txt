#
# Copyright 2018, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the GNU General Public License version 2. Note that NO WARRANTY is provided.
# See "LICENSE_GPLv2.txt" for details.
#
# @TAG(DATA61_GPL)
#
cmake_minimum_required(VERSION 3.7.2)

project(test_2 C)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -u __vsyscall_ptr")

add_executable(test_2 EXCLUDE_FROM_ALL src/test_2.c)
target_include_directories(test_2 PRIVATE include)
target_link_libraries(test_2 muslc sel4 sosapi utils)

# warn about everything
add_compile_options(-Wall -Werror -W -Wextra)

set_property(GLOBAL APPEND PROPERTY apps_property "$<TARGET_FILE:test_2>")
