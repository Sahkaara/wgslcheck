# wgslcheck

**A static analyzer and CPU-side debugger for WGSL compute shaders — CLI-first, cross-backend, zero-dependency.**

[![CI](https://github.com/Sahkaara/wgslcheck/actions/workflows/ci.yml/badge.svg)](https://github.com/Sahkaara/wgslcheck/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

`wgslcheck` is a portable C++20 tool for debugging WGSL compute shaders **outside the browser**. Unlike RenderDoc or browser extensions, it runs anywhere — Linux, macOS, Windows, CI pipelines — without requiring a GPU.

## What it does

- **`check`** — parse a WGSL file and run static checks, reporting issues with line/column in GNU format (`file:line:column: severity: message`)
- **`sim`** — execute a compute shader on the CPU, with configurable `--entry` and `--dispatch`; useful for reproducing bugs, testing kernels without a GPU, and validating logic in CI

Both subcommands work headlessly: no browser, no driver, no GPU.

## Status

**v0.1.0 — usable.** Lexer, parser, analyzer, and CPU simulator are implemented.

- [x] WGSL lexer
- [x] WGSL parser (subset: functions, attributes, primitives, control flow)
- [x] Static analyzer (`check`)
- [x] CPU simulator (`sim` with `--entry` and `--dispatch`)
- [x] `--version` and `--help`
- [ ] Concurrent workgroup simulation (single workgroup at a time for now)
- [ ] JSON output for CI integration

See [`docs/architecture.md`](docs/architecture.md) for the internals.

## Quick start

**Requirements:** a C++20 compiler (`g++`, `clang++`, `cl.exe`) and CMake ≥ 3.20.

```bash
git clone https://github.com/Sahkaara/wgslcheck.git
cd wgslcheck
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/wgslcheck --version
```

You should see:

```
wgslcheck 0.1.0
```

## CLI usage

```
wgslcheck <command> [options] <file.wgsl>

Commands:
  check <file.wgsl>                              Run static checks
  sim   <file.wgsl> [--entry NAME] [--dispatch X Y Z]   Execute on the CPU
  --version                                      Print version
  --help                                         Show this message
```

Exit codes:

| Command | 0 | 1 | 2 |
|---|---|---|---|
| `check` | no issues found | I/O or usage error | parse error or analysis error |
| `sim` | executed successfully | I/O or usage error | parse or simulation error |

### `wgslcheck check`

Parses the file and runs static checks. Silent on success.

```bash
$ ./wgslcheck check examples/sample.wgsl
examples/sample.wgsl: no issues found (1 function(s) checked)
exit: 0
```

On error:

```
examples/bad.wgsl:12:5: error: unknown function 'undefined_function'
```

### `wgslcheck sim`

Executes a compute shader on the CPU. The default entry point is `main`; the default dispatch is `1 1 1`.

```bash
$ ./wgslcheck sim examples/sample.wgsl
Executed 8 invocation(s) of 'main' (workgroup_size=8x1x1, dispatch=1x1x1)
exit: 0

$ ./wgslcheck sim examples/sample.wgsl --dispatch 4 1 1
Executed 32 invocation(s) of 'main' (workgroup_size=8x1x1, dispatch=4x1x1)
exit: 0

$ ./wgslcheck sim examples/sample.wgsl --entry my_kernel --dispatch 2 2 1
Executed 8 invocation(s) of 'my_kernel' (workgroup_size=8x1x1, dispatch=2x2x1)
```

The simulator is **single-workgroup per invocation**: each workgroup runs independently on the CPU. Concurrent simulation of multiple workgroups (to expose data races) is on the roadmap.

## Architecture

```
wgslcheck/
├── include/wgslcheck/   # Public headers
│   ├── ast.hpp          # AST node types
│   ├── diagnostic.hpp   # Diagnostic struct (severity, line, column, message)
│   ├── lexer.hpp        # Tokenizer interface
│   ├── parser.hpp       # Recursive-descent parser
│   ├── analyzer.hpp     # Static checks on the AST
│   └── simulator.hpp    # CPU-side execution
├── src/                 # Implementation
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── analyzer.cpp
│   ├── simulator.cpp
│   └── main.cpp         # CLI (subcommand dispatch)
├── examples/
│   └── sample.wgsl      # Minimal compute shader used in docs and tests
├── tests/
│   └── test_basic.sh    # Smoke tests for the CLI
└── docs/
    └── architecture.md
```

**Design principles:**

- **Zero dependencies.** Just the C++ standard library.
- **Single binary.** No shared libraries, no runtime, no config files.
- **Headless.** No GPU, no browser, no driver.
- **CLI-first.** Every feature is available from the command line, ready for CI.
- **Honest scope.** The parser supports a subset of WGSL, documented below. Unsupported constructs produce clear errors, not silent failures.

## Supported WGSL subset

`wgslcheck` implements a practical subset of WGSL — enough for real compute kernels, not the full language spec.

| Feature | Supported | Notes |
|---|:---:|---|
| Functions (`fn`) | ✅ | Entry points and helpers |
| Attributes (`@compute`, `@workgroup_size`, `@builtin`, `@group`, `@binding`) | ✅ | |
| Primitive types (`u32`, `i32`, `f32`, `bool`) | ✅ | |
| Vector types (`vec2`, `vec3`, `vec4`) | ✅ | |
| Control flow (`if`, `else`, `loop`, `break`, `continue`, `return`) | ✅ | |
| Variable declarations (`var`, `let`) | ✅ | |
| Binary operators (`+`, `-`, `*`, `/`, `%`, comparisons) | ✅ | |
| **Structs** | ❌ | Planned |
| **Storage/texture bindings** | ❌ | Planned (parser accepts, simulator does not) |
| **Swizzles** (`vec.xyz`) | ❌ | Planned |
| **Built-in functions** (`dot`, `cross`, `length`, ...) | ❌ | Planned |
| **Workgroup shared memory** (`var<workgroup>`) | ❌ | Planned |
| **Barriers** (`workgroupBarrier()`) | ❌ | Planned |

If you need one of the ❌ features, please [open an issue](https://github.com/Sahkaara/wgslcheck/issues).

## Building from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Override the compiler:

```bash
CXX=clang++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### Windows

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Release\wgslcheck.exe --version
```

## Testing

```bash
bash tests/test_basic.sh
```

The smoke suite covers:

- `--version` and `--help` output
- `check` on a valid file (exit 0) and on an invalid one (exit 2)
- `sim` with default dispatch and with `--dispatch`
- exit codes for usage errors

Golden-file tests live in `tests/cases/` — drop a `foo.wgsl` and its expected `foo.expected` and the suite picks it up automatically.

## CI integration

Add `.github/workflows/ci.yml` (already present in this repo):

```yaml
name: CI
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build
      - name: Test
        run: bash tests/test_basic.sh
```

Use it in a pipeline:

```yaml
- name: Check shaders
  run: ./build/wgslcheck check shaders/*.wgsl

- name: Simulate
  run: ./build/wgslcheck sim shaders/game_of_life.wgsl --dispatch 64 64 1
```

## Roadmap

- [x] WGSL lexer
- [x] WGSL parser (functions, attributes, primitives, control flow)
- [x] Static analyzer (`check`)
- [x] CPU simulator (`sim`, `--entry`, `--dispatch`)
- [ ] Structs and storage/texture bindings
- [ ] Built-in functions (`dot`, `cross`, `length`, ...)
- [ ] Workgroup shared memory and barriers
- [ ] Concurrent workgroup simulation (expose data races)
- [ ] JSON output for CI integration
- [ ] Pre-built binaries via GitHub Releases

See the [issues](https://github.com/Sahkaara/wgslcheck/issues) for the live roadmap.

## Contributing

Contributions are welcome — bug reports, feature requests, docs, tests, code.

1. **Open an issue first** for non-trivial changes.
2. **Fork, branch, PR.** One feature or fix per PR.
3. **Follow the existing style.** 4-space indent, `snake_case` for functions, `PascalCase` for types.
4. **Add a test.** If you're fixing a bug, add a test that fails before your fix.
5. **Run `bash tests/test_basic.sh` before pushing.**

Good first issues are labeled [`good first issue`](https://github.com/Sahkaara/wgslcheck/labels/good%20first%20issue).

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- [WGSL spec](https://www.w3.org/TR/WGSL/) — the language reference.
- [Bevy](https://bevyengine.org/) and [wgpu](https://wgpu.rs/) — real-world WGSL used to test the parser.
- [llama.cpp](https://github.com/ggerganov/llama.cpp) — inspiration for the "zero-dependency, single-binary" philosophy.