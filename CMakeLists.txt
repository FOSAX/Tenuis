cmake_minimum_required(VERSION 3.16)
project(Tenuis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Runtime interpreter (tenuisr) ───────────────────────────────────────────
add_executable(tenuisr
    src/vm/main.cpp
    src/vm/loader.cpp
    src/vm/vm.cpp
    src/vm/io.cpp
    src/vm/crc.cpp
    src/vm/decompress.cpp
)
target_include_directories(tenuisr PRIVATE src/vm)
target_compile_options(tenuisr PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Os
        -ffunction-sections
        -fdata-sections
        -fno-exceptions
        -fno-rtti
        -Wall
        -Wextra
        -Wno-unused-result   # I/O errors on bare-metal stdout/stderr are not recoverable
    >
)
target_link_options(tenuisr PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--gc-sections>
)

# Size check runs on every build — fails if over budget.
add_custom_command(TARGET tenuisr POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DBINARY=$<TARGET_FILE:tenuisr>
            -DBUDGET=10240
            -P ${CMAKE_SOURCE_DIR}/cmake/check_size.cmake
    COMMENT "Checking interpreter size budget (10 240 B)"
)

# ── Host tool: mktenb ───────────────────────────────────────────────────────
add_executable(mktenb tools/mktenb.cpp)
target_compile_options(mktenb PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2 -Wall>
)

# ── Host tool: tenuisc (compiler) ───────────────────────────────────────────
add_executable(tenuisc
    tools/tenuisc.cpp
    tools/compress.cpp
    src/vm/crc.cpp
    src/vm/decompress.cpp
)
target_include_directories(tenuisc PRIVATE src/vm tools)
target_compile_options(tenuisc PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2 -Wall -Wextra>
)

# ── Host tool: tenuispack (binary embedder) ──────────────────────────────────
add_executable(tenuispack tools/tenuispack.cpp)
target_compile_options(tenuispack PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2 -Wall -Wextra>
)

# ── Host tool: tcf_roundtrip (compression round-trip tester) ─────────────────
add_executable(tcf_roundtrip
    tools/tcf_roundtrip.cpp
    tools/compress.cpp
    src/vm/decompress.cpp
)
target_include_directories(tcf_roundtrip PRIVATE src/vm tools)
target_compile_options(tcf_roundtrip PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2 -Wall -Wextra>
)

# ── Helper: build a self-contained packed binary from a .ten source ──────────
# tenuis_add_packed(<target> <ten_source_absolute_path>)
#   1. Compiles .ten → .tenb  (via tenuisc)
#   2. Embeds .tenb → _embedded.cpp  (via tenuispack)
#   3. Links main_embedded.cpp + embedded data + full VM into a single executable.
function(tenuis_add_packed target ten_source)
    get_filename_component(base "${ten_source}" NAME_WE)
    set(packed_dir "${CMAKE_CURRENT_BINARY_DIR}/packed")
    set(tenb_file  "${packed_dir}/${base}.tenb")
    set(embed_file "${packed_dir}/${base}_embedded.cpp")

    file(MAKE_DIRECTORY "${packed_dir}")

    add_custom_command(
        OUTPUT  "${tenb_file}"
        COMMAND $<TARGET_FILE:tenuisc> "${ten_source}" "${tenb_file}"
        DEPENDS tenuisc "${ten_source}"
        COMMENT "Compiling ${base}.ten → ${base}.tenb (packed)"
    )
    add_custom_command(
        OUTPUT  "${embed_file}"
        COMMAND $<TARGET_FILE:tenuispack> "${tenb_file}" "${embed_file}"
        DEPENDS tenuispack "${tenb_file}"
        COMMENT "Embedding ${base}.tenb → ${base}_embedded.cpp"
    )

    add_executable(${target}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/main_embedded.cpp"
        "${embed_file}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/vm.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/io.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/crc.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/decompress.cpp"
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm"
    )
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Os
            -ffunction-sections
            -fdata-sections
            -fno-exceptions
            -fno-rtti
            -Wall
            -Wextra
            -Wno-unused-result
        >
    )
    target_link_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--gc-sections>
    )
endfunction()

# ── Helper: build a hybrid binary (baked-in safe-mode + uplinkable override) ──
# tenuis_add_hybrid(<target> <ten_source_absolute_path>)
#   Same pipeline as tenuis_add_packed but links main_hybrid.cpp instead.
#   At runtime: tries argv[1] as uplink .tenb; falls back to embedded if absent/invalid.
function(tenuis_add_hybrid target ten_source)
    get_filename_component(base "${ten_source}" NAME_WE)
    set(packed_dir "${CMAKE_CURRENT_BINARY_DIR}/packed")
    set(tenb_file  "${packed_dir}/${base}.tenb")
    set(embed_file "${packed_dir}/${base}_embedded.cpp")

    file(MAKE_DIRECTORY "${packed_dir}")

    add_custom_command(
        OUTPUT  "${tenb_file}"
        COMMAND $<TARGET_FILE:tenuisc> "${ten_source}" "${tenb_file}"
        DEPENDS tenuisc "${ten_source}"
        COMMENT "Compiling ${base}.ten → ${base}.tenb (hybrid safe-mode)"
    )
    add_custom_command(
        OUTPUT  "${embed_file}"
        COMMAND $<TARGET_FILE:tenuispack> "${tenb_file}" "${embed_file}"
        DEPENDS tenuispack "${tenb_file}"
        COMMENT "Embedding ${base}.tenb → ${base}_embedded.cpp (hybrid safe-mode)"
    )

    add_executable(${target}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/main_hybrid.cpp"
        "${embed_file}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/vm.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/io.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/crc.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/decompress.cpp"
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vm"
    )
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Os
            -ffunction-sections
            -fdata-sections
            -fno-exceptions
            -fno-rtti
            -Wall
            -Wextra
            -Wno-unused-result
        >
    )
    target_link_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--gc-sections>
    )
endfunction()

# ── Packed test binaries (one per fixture) ───────────────────────────────────
tenuis_add_packed(arith_packed     "${CMAKE_SOURCE_DIR}/tests/fixtures/arith.ten")
tenuis_add_packed(branch_packed    "${CMAKE_SOURCE_DIR}/tests/fixtures/branch.ten")
tenuis_add_packed(loop_packed      "${CMAKE_SOURCE_DIR}/tests/fixtures/loop.ten")
tenuis_add_packed(sumto_packed     "${CMAKE_SOURCE_DIR}/tests/fixtures/sumto.ten")
tenuis_add_packed(triangles_packed "${CMAKE_SOURCE_DIR}/tests/fixtures/triangles.ten")

# ── Hybrid test binary (safe-mode = arith, uplink = any .tenb) ───────────────
tenuis_add_hybrid(arith_hybrid "${CMAKE_SOURCE_DIR}/tests/fixtures/arith.ten")

# ── Tests ───────────────────────────────────────────────────────────────────
enable_testing()

add_test(NAME phase1_tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_tests.sh ${CMAKE_BINARY_DIR}
)
set_tests_properties(phase1_tests PROPERTIES
    DEPENDS "tenuisr;mktenb"
)

add_test(NAME phase2_tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_compiler_tests.sh ${CMAKE_BINARY_DIR}
)
set_tests_properties(phase2_tests PROPERTIES
    DEPENDS "tenuisr;tenuisc"
)

add_test(NAME phase3_tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_phase3_tests.sh ${CMAKE_BINARY_DIR}
)
set_tests_properties(phase3_tests PROPERTIES
    DEPENDS "tenuisr;tenuisc;tcf_roundtrip"
)

add_test(NAME phase4_tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_phase4_tests.sh ${CMAKE_BINARY_DIR}
)
set_tests_properties(phase4_tests PROPERTIES
    DEPENDS "arith_packed;branch_packed;loop_packed;sumto_packed;triangles_packed"
)

add_test(NAME phase5_tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_phase5_tests.sh ${CMAKE_BINARY_DIR}
)
set_tests_properties(phase5_tests PROPERTIES
    DEPENDS "tenuisr;tenuisc"
)

add_test(NAME phase6_tests
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_phase6_tests.sh ${CMAKE_BINARY_DIR}
)
set_tests_properties(phase6_tests PROPERTIES
    DEPENDS "arith_hybrid;tenuisc"
)
