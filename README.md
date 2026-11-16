# wgslcheck

A dependency-free C++20 command-line checker and CPU simulator for a **subset** of WGSL compute shaders. No browser, GPU or driver is required.

This is not a complete WGSL validator or a GPU conformance implementation. A successful `check` means that the implemented checks passed, not that every WGSL rule was verified.

## Build and test

Requires CMake 3.20 or newer and a C++20 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Linux/macOS, the executable is normally `build/wgslcheck`. With Visual Studio on Windows it is `build/Release/wgslcheck.exe`.

The repository also provides `bash tests/test_basic.sh` as a convenience wrapper. CI builds and runs CTest on Linux, Windows and macOS. There is no automatic golden-file test discovery.

## Commands

```text
wgslcheck check <file.wgsl>
wgslcheck sim <file.wgsl> [--entry NAME] [--dispatch X Y Z]
wgslcheck --version
wgslcheck --help
```

Each command accepts one input file. `sim` defaults to entry point `main` and dispatch `1 1 1`. Dispatch dimensions must be positive integers.

```sh
./build/wgslcheck check examples/sample.wgsl
# examples/sample.wgsl: no issues found (1 function(s) checked)

./build/wgslcheck sim examples/sample.wgsl --dispatch 2 2 1
# Executed 32 invocation(s) of 'main' (workgroup_size=8x1x1, dispatch=2x2x1)
```

Diagnostics use `file:line:column: severity: message`. `check` prints a summary on success.

| Exit code | Meaning |
|---|---|
| 0 | Command succeeded |
| 1 | Usage or file I/O error |
| 2 | Lexical, parse, analysis or simulation error |

## Implemented subset

| Area | Support |
|---|---|
| Declarations | Functions, parameters, local `var`/`let`, struct definitions |
| Types | Type names and nested generic type syntax; no complete type validation |
| Statements | Blocks, assignments and compound assignments, `if`/`else if`/`else`, `return` |
| Expressions | Scalar literals, unary/binary operators, named calls, member access |
| Static analysis | Duplicate declarations/attributes, local name resolution, callable names and argument counts, immutable assignments, shader stage and workgroup-size checks |
| Scope | Local shadowing, separate function environments, restoration on block exit |
| Compute builtins | `global_invocation_id`, `local_invocation_id`, `workgroup_id`, `num_workgroups`, `local_invocation_index` |
| Scalar constructors | `u32`, `i32`, `f32`, `bool`, with zero or one argument |
| Vectors | Untemplated `vec2`, `vec3`, `vec4` calls with numeric components; scalar splat and component flattening; single-component `.x`/`.r` etc. |
| Math functions | Scalar `min`, `max`, `abs`, `sqrt`, `floor`, `ceil` |

The checker recognizes the listed math functions and constructors, plus declared functions and structs. Other function names receive an **unknown or unsupported** diagnostic. Vector argument counts receive a coarse check; component widths and types are checked during simulation. Struct constructors are recognized statically but cannot be executed.

The parser does **not** implement global/resource declarations, loops (`loop`, `for`, `while`), `break`, `continue`, `switch`, pointers, array indexing or templated constructor expressions such as `vec3<u32>(...)`. Generic type annotations such as `id: vec3<u32>` are parsed.

Simulation does **not** implement storage buffers, textures, struct instances, shared memory, barriers, atomics, multiple-component swizzles or vector arithmetic. Local variables without initializers produce a simulation diagnostic. Invocation execution is sequential and bounded; it cannot detect GPU data races.

## Numeric behavior and limits

The simulator retains scalar numeric kinds for suffixed literals, supported explicit type annotations and constructors. Supported `u32`/`i32` arithmetic uses 32-bit wrapping, integer division truncates toward zero, signed right shifts preserve the sign, and concrete integer shift counts are reduced modulo 32. Compound assignments use the same operator implementation.

`f32` conversions and arithmetic results are rounded through the host's IEEE binary32 `float`. Integer-to-integer conversions preserve the low 32 bits; floating-point-to-integer conversions clamp to the representable range and truncate. Boolean conversions and zero-value scalar constructors are supported. Arithmetic division by zero and non-finite results are diagnosed.

Remaining limitations are intentional and must be considered when interpreting results:

- This is not full static typing, overload resolution or WGSL constant-expression evaluation. Some invalid type combinations can still reach simulation; declaration/assignment conversions are simplified.
- Abstract integers are stored exactly only through magnitude `2^53 - 1`; larger values/results are rejected. Abstract floating-point values use host `double`.
- Abstract-value concretization and GPU floating-point corner cases are not fully modeled. Runtime and constant-expression overflow rules are not distinguished.
- `f16`/`h` values are rejected during simulation rather than silently treated as doubles; extension directives are not parsed.
- Vectors retain a common numeric component kind, but mixed component kinds and full vector type inference are unsupported.
- Workgroup dimensions and total workgroup size are capped at 256 by this tool; these are not device capability queries.

For the language rules, see the [WGSL specification](https://www.w3.org/TR/WGSL/), particularly [numeric conversions](https://www.w3.org/TR/WGSL/#floating-point-conversion). See [architecture](docs/architecture.md) for implementation details.

## Tests

CTest runs the unit/regression executable and four CLI tests: valid checking, valid simulation, missing-file handling, and semantic/numeric regressions. Unit tests cover lexer errors, structs, scope and immutability, conditionals, helper calls, name/argument validation and scalar numeric behavior. CLI regressions assert exact exit codes and relevant diagnostic text.

Add regression cases to `tests/test_wgslcheck.cpp` or the CMake CLI scripts, then run the complete CTest suite before submitting changes.

## Roadmap

- Complete static type and return-path validation
- Broader WGSL parser coverage and standard-library builtins
- Resource bindings, memory and synchronization
- More complete numeric and vector semantics
- JSON diagnostics and pre-built releases

## License

MIT — see [LICENSE](LICENSE).
