# Architecture and implementation scope

The command-line pipeline is intentionally straightforward:

1. `Lexer` scans source text into owned tokens and reports lexical errors with
   one-based line and column locations. Block comments may nest.
2. `Parser` turns function declarations into the AST in `ast.hpp`. Expressions
   use precedence parsing; malformed statements recover at statement or
   declaration boundaries where possible.
3. `Analyzer` performs checks that do not require a full type system: duplicate
   function names, conflicting shader stages, and compute workgroup-size rules.
4. `Simulator` selects a compute entry point, binds supported invocation
   builtins, executes the supported statement/expression subset, and enforces a
   fixed invocation budget.

The AST owns its expression and statement trees, while tokens own their text;
there are no source-lifetime dependencies after lexing. Diagnostics are data
objects shared between the front end, analyzer, and simulator, and the CLI is
the only layer that formats them for users.

## Deliberate limitations

This version is a subset tool, not a normative WGSL validator. It currently
does not parse global/resource/struct declarations, loops, switch statements,
pointer operations, or the full type/attribute grammar. Consequently, a shader
using those features is rejected or cannot be simulated. The analyzer does not
perform full static typing, resource-layout validation, control-flow
uniformity, or target-specific checks.

The CPU simulator operates on scalar numeric/boolean values and small vectors.
It supports `global_invocation_id`, `local_invocation_id`, `workgroup_id`,
`num_workgroups`, and `local_invocation_index`; memory, barriers, atomics,
textures, and exact WGSL integer/float semantics are outside its scope. It is
intended for stepping through simple arithmetic and control flow, not as a
conformance oracle.