# check_size.cmake — called post-build with -DBINARY=<path> -DBUDGET=<bytes>
# Reports interpreter size and fails if it exceeds the budget.

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

# 'size' output (GNU binutils):
#   text    data     bss     dec     hex filename
#   1234     456       0    1690     69a tenuisr
string(REPLACE "\n" ";" LINES "${SIZE_OUT}")
list(GET LINES 1 LINE2)
string(REGEX MATCH "^[ \t]*([0-9]+)[ \t]+([0-9]+)" _ "${LINE2}")
set(TEXT_B "${CMAKE_MATCH_1}")
set(DATA_B "${CMAKE_MATCH_2}")
math(EXPR TOTAL "${TEXT_B} + ${DATA_B}")
math(EXPR HEADROOM "${BUDGET} - ${TOTAL}")

if(TOTAL GREATER BUDGET)
    math(EXPR OVER "${TOTAL} - ${BUDGET}")
    message(FATAL_ERROR
        "SIZE BUDGET EXCEEDED: ${TOTAL} B  (budget ${BUDGET} B, over by ${OVER} B)")
else()
    message(STATUS
        "Interpreter size: ${TOTAL} B  (budget ${BUDGET} B, headroom ${HEADROOM} B)  OK")
endif()
