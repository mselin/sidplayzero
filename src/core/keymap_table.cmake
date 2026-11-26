# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes src/core/keymap_table.cpp, the key map compiled into `core`, from docs/keymap.tsv (SPEC-094): one row
# of the table for each row of the file, in its order, "-" written as "". After a change to the file, run
# from the repository's root
#     cmake -DIN=docs/keymap.tsv -DOUT=src/core/keymap_table.cpp -P src/core/keymap_table.cmake
# The unit test "SPEC-094 the key map compiled into core has exactly the rows of docs/keymap.tsv" fails
# until it has been run. A row that cannot be read stops the script rather than being left out.
if(NOT IN OR NOT OUT)
    message(FATAL_ERROR "keymap_table.cmake needs -DIN=FILE and -DOUT=FILE")
endif()

file(STRINGS "${IN}" _lines ENCODING UTF-8)
set(_text "// SPDX-License-Identifier: GPL-3.0-or-later\n// The key map (SPEC-094), made from docs/keymap.tsv by src/core/keymap_table.cmake. Do not edit: change\n// the file and run the script again.\n#include \"spz/keymap.h\"\n\nnamespace spz {\n\nconst std::vector<KeyMapRow>& key_map() {\n    static const std::vector<KeyMapRow> rows = {\n")
set(_header_seen FALSE)
set(_count 0)
foreach(_line IN LISTS _lines)
    if(_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    if(NOT _header_seen)
        set(_header_seen TRUE)
        continue()
    endif()
    if(_line MATCHES "[;\"\\\\]")
        message(FATAL_ERROR "docs/keymap.tsv: a row the table cannot hold as it is: ${_line}")
    endif()
    string(REPLACE "\t" ";" _cells "${_line}")
    list(LENGTH _cells _n)
    if(NOT _n EQUAL 6)
        message(FATAL_ERROR "docs/keymap.tsv: a row without six columns: ${_line}")
    endif()
    set(_row "        {")
    set(_first TRUE)
    foreach(_cell IN LISTS _cells)
        if(_cell STREQUAL "-")
            set(_cell "")
        endif()
        if(NOT _first)
            string(APPEND _row ", ")
        endif()
        set(_first FALSE)
        string(APPEND _row "\"${_cell}\"")
    endforeach()
    string(APPEND _row "},\n")
    string(APPEND _text "${_row}")
    math(EXPR _count "${_count} + 1")
endforeach()
if(_count EQUAL 0)
    message(FATAL_ERROR "docs/keymap.tsv has no rows")
endif()

string(APPEND _text "    };\n    return rows;\n}\n\n}  // namespace spz\n")
file(WRITE "${OUT}" "${_text}")
