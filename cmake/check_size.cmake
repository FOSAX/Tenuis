# check_size.cmake — called post-build with -DBINARY=<path> -DBUDGET=<bytes>
# On Linux (ELF): parses GNU binutils 'size' text+data columns.
# On macOS (Mach-O): 'size' gives page-aligned segment totals. Re-runs with
# 'size -A' (SysV format) to sum actual per-section sizes in __TEXT instead.

if(NOT DEFINED BINARY OR NOT DEFINED BUDGET)
    message(FATAL_ERROR "check_size: BINARY and BUDGET must be defined")
endif()

execute_process(
    COMMAND size "${BINARY}"
    OUTPUT_VARIABLE SIZE_OUT
    RESULT_VARIABLE SIZE_RC
)

if(NOT SIZE_RC EQUAL 0)
    message(WARNING "check_size: 'size' failed — skipping size check")
    return()
endif()

string(REPLACE "\n" ";" LINES "${SIZE_OUT}")
list(GET LINES 0 HEADER)
list(GET LINES 1 LINE2)
string(STRIP "${HEADER}" HEADER)

if(HEADER MATCHES "__TEXT")
    # macOS BSD format detected. Page-aligned __TEXT is not useful here.
    # Use 'size -A' to get the actual size of each section in __TEXT.
    execute_process(
        COMMAND size -A "${BINARY}"
        OUTPUT_VARIABLE SIZE_A_OUT
        RESULT_VARIABLE SIZE_A_RC
    )
    if(NOT SIZE_A_RC EQUAL 0)
        message(WARNING "check_size: 'size -A' failed — skipping size check")
        return()
    endif()
    set(TOTAL 0)
    string(REPLACE "\n" ";" SA_LINES "${SIZE_A_OUT}")
    foreach(SL IN LISTS SA_LINES)
        if(SL MATCHES "^__TEXT/[^ \t]+[ \t]+([0-9]+)")
            math(EXPR TOTAL "${TOTAL} + ${CMAKE_MATCH_1}")
        endif()
    endforeach()
else()
    # GNU binutils format: text  data  bss  dec  hex  filename
    string(REGEX MATCH "^[ \t]*([0-9]+)[ \t]+([0-9]+)" _ "${LINE2}")
    set(TEXT_B "${CMAKE_MATCH_1}")
    set(DATA_B "${CMAKE_MATCH_2}")
    math(EXPR TOTAL "${TEXT_B} + ${DATA_B}")
endif()

math(EXPR HEADROOM "${BUDGET} - ${TOTAL}")

if(TOTAL GREATER BUDGET)
    math(EXPR OVER "${TOTAL} - ${BUDGET}")
    message(FATAL_ERROR
        "SIZE BUDGET EXCEEDED: ${TOTAL} B  (budget ${BUDGET} B, over by ${OVER} B)")
else()
    message(STATUS
        "Interpreter size: ${TOTAL} B  (budget ${BUDGET} B, headroom ${HEADROOM} B)  OK")
endif()
