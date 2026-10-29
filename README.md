# wgslcheck

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://isocpp.org/)
[![CI](https://github.com/Sahkaara/wgslcheck/actions/workflows/ci.yml/badge.svg)](https://github.com/Sahkaara/wgslcheck/actions/workflows/ci.yml)

**A small WGSL subset checker and CPU-side compute shader simulator written in
C++20.**

`wgslcheck` helps inspect and experiment with simple compute shaders without a
GPU. It is an educational and debugging aid—not a complete WGSL validator, GPU
implementation, or conformance tool.

> [!WARNING]
> This project implements only a limited WGSL subset. A successful check does
> not guarantee that a shader is valid WGSL or will compile or behave correctly
> on a GPU. Review [the current limitations](#supported-subset-and-limitations)
> before relying on its results.

## Contents

- [Features](#features)
- [Quick start](#quick-start)
- [Build from source](#build-from-source)
- [Command reference](#command-reference)
- [Supported subset and limitations](#supported-subset-and-limitations)
- [Project structure](#project-structure)
- [Tests](#tests)
- [Contributing](#contributing)
- [License](#license)

## Features

- WGSL-style tokenization, including nested block comments and source locations.
- Parsing of attributed functions, parameter and return types, generic type
  arguments, blocks, local variables, assignments, expressions, `if`/`else`,
  and `return`.
- Static checks for duplicate function, parameter, and local names; assignments
  to immutable `let` values; shader-stage attributes; and compute workgroup
  size.
- CPU execution of a bounded number of compute invocations, with selected
  invocation builtins, basic vectors, scalar arithmetic, conditionals, and
  simple helper functions.
- No third-party runtime dependencies.

## Quick start

### Requirements

- CMake 3.20 or newer.
- A C++20-compatible compiler: MSVC, GCC, or Clang.

### Configure, build, and test

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

### Try the example

```sh
wgslcheck check examples/sample.wgsl
wgslcheck sim examples/sample.wgsl --dispatch 2 1 1
```

Example output:

```text
examples/sample.wgsl: no issues found (1 function(s) checked)
Executed 16 invocation(s) of 'main' (workgroup_size=8x1x1, dispatch=2x1x1)
```

The commands above assume the executable is on your `PATH`. See
[Build from source](#build-from-source) for the generated executable location
and platform-specific invocation examples.

## Build from source

The project has no third-party dependencies. Configure the build in a separate
directory so generated files stay out of the source tree:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

The `--config Release` option works with both single- and
multi-configuration generators. The executable is typically generated at:

| Platform / generator | Executable |
| --- | --- |
| Visual Studio | `build/Release/wgslcheck.exe` |
| Single-configuration Windows | `build/wgslcheck.exe` |
| Linux or macOS | `build/wgslcheck` |

PowerShell example:

```powershell
.\build\Release\wgslcheck.exe check examples\sample.wgsl
.\build\Release\wgslcheck.exe sim examples\sample.wgsl --dispatch 2 1 1
```

Linux or macOS example:

```sh
./build/wgslcheck check examples/sample.wgsl
./build/wgslcheck sim examples/sample.wgsl --dispatch 2 1 1
```

## Command reference

```text
wgslcheck --help
wgslcheck --version
wgslcheck check <file.wgsl>
wgslcheck sim <file.wgsl> [--entry NAME] [--dispatch X Y Z]
```

### `check`

```sh
wgslcheck check examples/sample.wgsl
```

Reads the source, tokenizes and parses supported function declarations, then
runs the implemented static checks. On success it prints the number of
functions checked. Diagnostics are written to standard error and include the
source path and line/column where available.

### `sim`

```sh
# Select the first compute entry point; dispatch one workgroup
wgslcheck sim examples/sample.wgsl

# Select an entry point and dispatch two workgroups along X
wgslcheck sim examples/sample.wgsl --entry main --dispatch 2 1 1
```

| Option | Description |
| --- | --- |
| `--entry NAME` | Select a compute entry point. Defaults to the first compute entry point in the source. |
| `--dispatch X Y Z` | Number of workgroups along each axis. Defaults to `1 1 1`; each value must be a positive integer. |

The simulator executes each invocation in the selected entry point and prints
an execution summary. The total dispatch is capped at 4096 invocations.

Supported compute builtins:

- `global_invocation_id`
- `local_invocation_id`
- `workgroup_id`
- `num_workgroups`
- `local_invocation_index`

The simulator also supports initialized local `var` and `let` declarations,
assignments to mutable locals, scalar expressions, conditionals, returns,
simple helper functions, vector constructors and member access, scalar
conversions, and a small set of common scalar functions.

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Command completed successfully. `--help` and `--version` also return `0`. |
| `1` | Invalid command-line usage, unknown command, or unknown option. |
| `2` | File, lexing, parsing, analysis, or simulation error. |

## Supported subset and limitations

The parser currently accepts top-level function declarations, function
attributes and parameter attributes, generic type arguments, and these
function-body constructs:

- Local `let` and `var` declarations.
- Assignments and expression statements.
- Unary and binary scalar expressions, function calls, and vector member
  access.
- Blocks, `if`/`else`, and `return`.

The analyzer checks duplicate function/parameter/local names, assignments to
immutable local values, shader-stage attribute combinations, and compute
`@workgroup_size`. The workgroup size must contain one to three positive
integer literals; each dimension is at most 256 and the product is at most 256
invocations.

This is **not a full WGSL type checker**. It does not currently parse module
declarations such as structures, global variables, resources, or overrides.
Loops, `switch`, pointers, and other statements outside the list above are not
supported. It also does not validate all WGSL typing rules, resource layouts,
stage interfaces, control-flow uniformity, or target-specific constraints.

The CPU simulator does not emulate storage/resource memory, output buffers,
atomics, barriers, or textures. It does not reproduce exact WGSL integer
overflow, floating-point, or GPU execution semantics. A successful `check`
means only that the source passed this project's implemented subset checks.
Use a standards-compliant WGSL implementation for validation and GPU builds.

For implementation details, see [the architecture notes](docs/architecture.md).

## Project structure

```text
include/wgslcheck/  Public headers, AST, and diagnostics
src/                CLI, lexer, parser, analyzer, and simulator
tests/              Unit and command-line tests
examples/            Example compute shader
docs/                Architecture and implementation notes
```

## Tests

Run the CMake/CTest suite after building:

```sh
ctest --test-dir build --build-config Release --output-on-failure
```

The suite includes unit coverage for lexing, parsing, analysis, and simulation,
as well as CLI checks against the sample shader.

## Contributing

Bug reports and contributions are welcome. When changing the supported
language subset, please include focused tests and update this README and the
[architecture notes](docs/architecture.md) as appropriate. Before opening a
pull request, build the project and run CTest:

```sh
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

Please include a concise description of the change and any relevant
limitations or compatibility impact in the pull request.

## License

`wgslcheck` is distributed under the MIT License. See [LICENSE](LICENSE) for
the complete text.
