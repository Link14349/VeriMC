# VeriMC

Redstone hardware design tools targeting the **Minecraft Java Edition 26.2 release**, including a block-level circuit simulator, the VeriMC hardware description language and compiler frontend, and a logic graph viewer.

The project is under active development. You can build and debug block circuits manually, or compile HDL source into a logical netlist and explore its connections in the browser. **Conversion from HDL to Minecraft block circuits, automatic placement and routing, and Litematic / Create schematic export are not yet implemented.**

## Subprojects

| Directory | Purpose | Stack |
| --- | --- | --- |
| [simulator/](simulator/README.md) | 3D circuit editing, standalone simulation, single stepping, probe waveforms, project files, and runtime snapshots | C++20, CMake, Three.js, React |
| [verimc/](verimc/README.md) | Language specification, tutorials, parsing and static checks, module elaboration, netlist output and validation | C++20, CMake, ANTLR4 |
| [vmcl-visualize/](vmcl-visualize/README.md) | Local netlist loading in the browser, automatic graph layout, connection search, and SVG export | TypeScript, React Flow, ELK.js, Vite |
| [docs/](docs/module-1-design.md) | Overall design, target scope, and acceptance criteria | Markdown |

The three subprojects build independently. Normal simulator use requires neither Java nor Minecraft. The logic graph viewer requires no application backend.

## Quick Start

Start each command group below from the repository root. The native projects have primarily been validated on **macOS / Apple Silicon**; validation on other platforms is not yet complete.

### Run the Block Simulator

Requirements: Python 3, a C++20 compiler, CMake 3.25+, Boost 1.90+, nlohmann/json 3.12+, Zstandard 1.5+, OpenSSL 3, Node.js, and npm.

```sh
# macOS: install dependencies (requires Xcode Command Line Tools and Homebrew)
brew install cmake boost nlohmann-json zstd openssl@3 node python

# Build the C++ server and web app, then launch and open the browser
python3 simulator/runSimulator.py
```

The default address is [http://127.0.0.1:28765/](http://127.0.0.1:28765/). Press `Ctrl+C` in the terminal to stop. On subsequent launches, use `--no-build` to skip rebuilding, `--no-open` to keep the browser from opening automatically, or `--port` to select a port.

The workbench supports component placement, lever interaction, run/pause controls, single stepping, and probe waveforms. Load a built-in experiment from the project menu or import the [8-bit adder example](simulator/examples/README.md). See the [simulator README](simulator/README.md) for controls and supported components.

### Compile VeriMC Source

Requirements: a C++20 compiler, CMake 3.25+, Boost 1.90+, nlohmann/json 3.12+, OpenSSL 3, Java 17+, and Python 3. Java generates the parser at build time, and Python runs regression tests; neither is needed to run the compiler itself. Initial configuration downloads and verifies ANTLR 4.13.2, cached in `verimc/.cache/`.

```sh
cmake -S verimc -B verimc/build -DCMAKE_BUILD_TYPE=Release
cmake --build verimc/build -j 6

# Compile the counter into a logical netlist in the ignored build directory
verimc/build/verimcCli compile verimc/examples/valid/counter.vmc --top Counter -o verimc/build/counter.vmcl
verimc/build/verimcCli validate verimc/build/counter.vmcl
```

Pass `--force` explicitly to overwrite an existing output file. Start with the [language tutorial](verimc/docs/languageTutorial.md), and see the [compiler README](verimc/README.md) for more options and C++ interfaces.

### Explore a Logic Graph

Requirements: Node.js 20.11+ and npm.

```sh
cd vmcl-visualize
npm ci
npm run dev
```

Open [http://127.0.0.1:5174/](http://127.0.0.1:5174/), choose a built-in example, or drag in the `verimc/build/counter.vmcl` file generated above. File reading, checks, and layout all happen in the browser; files are not uploaded. Run `npm run build` to generate `dist/` for static hosting. See the [viewer README](vmcl-visualize/README.md) for full instructions.

## File Formats and Workflow

| Format | Contents | Usage |
| --- | --- | --- |
| `.vmc` | VeriMC HDL source | Compile with `verimcCli compile` |
| [`.vmcl`](verimc/docs/vmclFormat.md) | Logical netlist with nodes, types, ports, connections, and source provenance | Compiler output, standalone validation, and browser viewing |
| [`.vmcb`](simulator/docs/vmcbFormat.md) | Block circuit project | Import and export in the simulator |
| `.snapshot.vmcb` | Runtime snapshot with event queues, component state, and probe history | Save and resume a simulation |

The current HDL workflow is `.vmc → .vmcl → logic graph viewer`. The simulator works with manually built or imported block projects. Physical conversion between these workflows is not yet available, and graph layout on screen does not represent Minecraft placement and routing.

## Validation

After building the relevant subproject, run the checks below. These cover native tests, compiler regressions, and viewer checks. Refer to each subproject's documentation for full server workflows and differential testing against vanilla Minecraft.

```sh
# Native simulator tests
ctest --test-dir simulator/build --output-on-failure

# Compiler, shared IR, and .vmcl adapter tests
ctest --test-dir verimc/build --output-on-failure

# Viewer tests, type checking, and production build
npm --prefix vmcl-visualize test
npm --prefix vmcl-visualize run build
```

Coverage and validation records: [simulator progress](simulator/docs/implementationStatus.md), [Minecraft reference validation](simulator/docs/referenceValidation.md), [compiler status](verimc/docs/compilerStatus.md), and [viewer verification](vmcl-visualize/docs/verification.md).

## Current Limitations

- The compatibility target is fixed at Minecraft Java Edition 26.2. Component and environmental behavior coverage is still expanding; full compatibility is not claimed. Component documentation records verified behavior, external stimulus interfaces, and remaining gaps.
- The compiler checks the pure logical design elaborated from the selected top-level module. Source `test` / `build` declarations are currently parsed for syntax only; tests and physical implementation are not executed.
- The logic graph viewer does not simulate circuits. Use `verimcCli validate` for complete bit-level combinational cycle and clock domain checks.
- TNT duplicators, fluid farms, minecart computers, and machines relying on mob AI are outside the current implementation scope.

## Development Conventions

Keep source code, dependencies, tests, examples, and build outputs within their respective subprojects. Write generated netlists to `verimc/build/` and local simulator projects to `simulator/runtime/`; both directories are ignored by Git. Official examples and test fixtures remain version-controlled, and dependency lockfiles should be committed.

Project communication and design documents default to Chinese. Code identifiers use English camelCase, with UpperCamelCase for types. Commit each meaningful, validated update separately, keeping the entire commit message within 20 characters. See [AGENTS.md](AGENTS.md) for detailed conventions.
