# Contributing to Tenuis

Tenuis targets environments where a bug cannot be patched after deployment. Correctness and determinism are not optional. This document describes the standards that all contributions must meet.

## Before You Start

Read `docs/INSTRUCTION_SET.md` and `docs/SPACE_PROFILE.md`. Every change to the VM, the compiler, or the binary format has consequences for deployed systems. Understanding the existing contracts is a prerequisite for changing them.

## What Contributions Are Welcome

- Bug fixes in the VM, compiler, loader, or compression engine
- New opcodes that fit within the reserved ranges defined in `docs/INSTRUCTION_SET.md`
- Platform toolchain files for new target architectures
- Documentation corrections and additions
- New test fixtures covering edge cases not currently tested

What is not accepted without a design discussion first:
- Changes to the `.tenb` binary format (breaks all existing programs)
- Changes to the TCF compression format (breaks all compressed programs)
- New opcodes outside the defined reserved ranges
- Dependencies on any external library in the runtime (`src/vm/`)

## Runtime Size Budget

The compiled `tenuisr` binary must stay under **10 240 bytes** of `.text + .rodata`. This is enforced automatically at build time by `cmake/check_size.cmake`. Any contribution that pushes the binary over budget will fail CI and must either reduce its footprint or demonstrate that an existing component can be trimmed to compensate.

The current allocation is approximately 9 548 bytes, leaving 692 bytes of headroom. Check the size report at the end of every build before submitting.

## Code Standards

**Language:** C++20. The runtime (`src/vm/`) uses only freestanding headers (`<cstdint>`, `<cstring>`). No STL containers, no exceptions, no RTTI in the runtime.

**Host tools** (`tools/`) may use the full standard library.

**Comments:** add a comment only when the reason for a decision is not obvious from the code. Do not describe what the code does; describe why it does it that way.

**No external formatting tools are required.** Follow the style of the surrounding code: 4-space indentation, braces on the same line for control flow, explicit casts for all narrowing conversions.

## Tests

Every change must pass all six test suites:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

New opcodes require:
- At least one test fixture (`.ten` source in `tests/fixtures/`)
- At least one test case in the appropriate `run_phase*.sh` script

New compiler features require tests in `tests/run_compiler_tests.sh` covering both the happy path and the relevant error conditions.

## Submitting a Patch

1. Fork the repository and create a branch from `main`.
2. Make your changes with commits that each represent a single logical change.
3. Ensure all tests pass locally before opening a pull request.
4. Describe in the pull request what the change does, why it is needed, and what the size impact is (run `size build/tenuisr` and include the output).
5. For changes to the VM or binary format, include the updated section of `docs/INSTRUCTION_SET.md` or `docs/BYTECODE_FORMAT.md` in the same pull request.

## Reporting Issues

Open an issue with:
- The version of Tenuis (check `CMakeLists.txt`, `project(Tenuis VERSION ...)`)
- The host platform and compiler version
- A minimal `.ten` source that reproduces the problem, or the exact `.tenb` hex dump
- The expected and actual output

## License

All contributions are made under the BSD 2-Clause License. By submitting a patch you confirm that you have the right to license the contribution under these terms.
