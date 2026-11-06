# Part of the Reflex project.
# Copyright © Reflex Project Contributors.
# SPDX-License-Identifier: Apache-2.0

function(target_reflex_headers TARGET_NAME)
endfunction()

function(reflex_generate_cpp_metadata)
    set(options USE_INTERFACE_PROPERTIES)
    set(oneValueArgs TARGET OUTPUT_VARIABLE METADATA_FILE_LIST_VARIABLE)
    set(multiValueArgs HEADERS EXTRA_COMPILE_DEFINITIONS)
    cmake_parse_arguments(_ "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
endfunction()
