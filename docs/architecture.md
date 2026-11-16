# Architecture and implementation scope

The command-line pipeline is `Lexer -> Parser -> Analyzer -> Simulator`.

1. `Lexer` owns its tokens, supports nested block comments, and reports malformed literals with one-based locations.
2. `Parser` builds owned AST nodes for functions, structs, expressions and the supported statement subset. Expression parsing follows operator precedence.
3. `Analyzer` checks declaration duplicates, immutable assignments and shader/workgroup attributes. A name-resolution pass collects function signatures before visiting bodies, checks argument counts, and tracks lexical scopes. Initializers are checked before introducing their declaration; function forward references are allowed.
4. `Simulator` selects a compute entry point, binds invocation builtins and executes invocations sequentially under a configurable invocation budget. Helper calls have a depth limit. Errors are returned as diagnostics.

The CLI formats diagnostics and returns 0 for success, 1 for usage/I/O errors and 2 for processing failures.

## Runtime values and scopes

Values carry a scalar/vector/boolean kind and a numeric kind (abstract integer, abstract float, u32, i32 or f32). Numeric payloads use double storage; concrete 32-bit integer multiplication uses integer intermediates to preserve low bits. A shared binary operator implementation serves expressions and compound assignments. Conversion helpers handle integer bit patterns, floating-point truncation/clamping and f32 rounding.

Each function invocation isolates its immutability map. Blocks save only declarations that shadow previous bindings and restore both values and mutability when they exit, including through return/error unwinding. Assignments to non-shadowed outer bindings remain visible.

## Deliberate limitations

Struct definitions are parsed and their names checked, but struct values are not simulated. Global/resource declarations, loops, switch, pointers and array indexing are not parsed. The analyzer is not a full type checker and does not implement all builtins, overload resolution, return-path validation, resource layout or uniformity checks. Unknown/unsupported callable names are reported explicitly.

Numeric execution covers common scalar arithmetic but is not a conformance oracle. Abstract integers are limited to the exactly representable double range; abstract concretization, constant-vs-runtime validation, vector arithmetic and GPU floating-point corner cases remain incomplete. Half precision is explicitly rejected. See the README for the supported conversions and numeric limits.

No memory model, storage buffers, barriers, textures or atomics are implemented. Invocations run sequentially and cannot reproduce GPU races. Workgroup limits are tool limits rather than adapter-specific validation.

## Verification

`tests/test_wgslcheck.cpp` exercises the frontend and interpreter directly. CTest also runs valid CLI checking/simulation, missing-file errors and semantic/numeric regressions, including exact exit codes. CI uses the same CMake/CTest workflow on Linux, macOS and Windows.
