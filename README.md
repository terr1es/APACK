# APACK

A bundler for Axiom programs. It turns a `.ax` source file into a self-contained directory,
or a single executable, that runs on a machine which has neither the compiler nor the
standard library installed.

```sh
apack demo.ax -o dist/MyApp --name MyApp
```

```
dist/MyApp/
  MyApp.exe      the runtime, renamed
  MyApp.acvm     the compiled bytecode
  gui.dll        only the libraries the program actually reaches
```

```sh
apack demo.ax -o dist/MyApp --name MyApp --single
```

```
dist/MyApp/
  MyApp.exe      2.7 MB: the runtime, the program, and the standard library
```

The recipient needs no toolchain, no source, and no build step. They run `MyApp.exe`.

## Design

Two decisions account for most of the code.

### The dependency closure is computed by the compiler

A packager has to answer one question: which modules does this program reach, and which
shared library does each of them live in?

That question is already answered by the import resolver. Re-deriving it by scanning
sources for `import` and `library` declarations would create a second implementation of
module resolution, and the two would diverge the first time a search rule changed. The
failure mode is not an error message — it is a bundle that omits a library, works on the
machine that produced it, and traps on the first call at the recipient's.

`Driver::dependencies()` therefore exposes what the import walk already collected: for each
module, its `library` name, the path it was resolved at, and its declared name and version.
The packager reads that list and does nothing else with module resolution.

### Verification does not execute the program

"Run it once after packing" is the intuitive check, and it is not available here: a GUI
program opens a window, and a program that reads input blocks the packager indefinitely.

The two conditions that actually reach a user are checked instead, and neither of them
executes code the program's author wrote:

1. **The payload parses.** The bytecode is read back and loaded as a module, which catches
   truncation and container-version mismatch. In single-file mode it is read back out of the
   executable rather than off the intermediate file, so the append itself is verified.
2. **Every shipped library loads from the bundle.** Each shared object is loaded with its
   own directory as the search root, which catches both a missing library and a library
   whose own dependencies are missing.

## Building

```sh
./build.sh
```

`build.sh` uses the toolchain configured for this machine. To build elsewhere:

```sh
cmake -S . -B build -G "MinGW Makefiles" \
      -DCMAKE_CXX_COMPILER=<llvm-mingw>/bin/clang++.exe \
      -DACVM_ROOT=<Axiom checkout> -DACVM_BUILD=<its build directory>
cmake --build build
```

The output is `build/apack.exe`.

`--single` needs `acvm-static.exe` in the Axiom build directory, which is a separate target:

```sh
cmake --build <Axiom build> --target acvm_static
```

This project consumes Axiom as a library and does not build it. `ACVM_ROOT` points at a
checkout and `ACVM_BUILD` at a configured build of that checkout; if the libraries are
absent, configuration stops with a message rather than producing a half-configured tree.

The static archives are linked inside `--start-group` rather than in a fixed order. The
references between them run in both directions — the VM names the JIT, the JIT names the
IR — so a hand-written order works until a symbol moves, and then fails for a reason
unrelated to the change that moved it.

## Usage

```
apack <file.ax> -o <dir> [options]

  -o <dir>          output directory (required)
  --name <name>     launcher name, without extension (default: the source's stem)
  --runtime <dir>   where acvm.exe and the standard library live
  -O0 .. -O4        optimization level (default: -O2)
  --single          one executable instead of a directory
```

## Single-file output

`--single` appends the compiled program to `acvm-static.exe` — a build of the runtime with
all ten standard-library modules linked in — and removes the intermediate bytecode. What
remains is one file.

Nothing is extracted at run time. The bytes are read out of the executable's own image and
parsed from memory, so there is no temporary directory to clean up, no first-run delay, and
no residue if the program is killed. The container is:

```
[ executable ][ payload ][ footer ]
```

The footer is a fixed 24 bytes at the very end, holding a magic value, the payload's offset,
and its length; locating it is a seek to `fileSize - 24` and a comparison. Appending is safe
because the Windows loader reads the PE headers and section table and ignores whatever
follows the last section.

Single-file output covers the standard library and nothing else. A program that imports a
module living in its own project — `gui`, for example — is refused, because that module has
no static counterpart and the bundle would fail on the recipient's machine at the first
call. Such programs use a directory bundle, which ships the library beside the executable.

## Where the runtime is found

When `--runtime` is not given, three locations are tried in order:

1. `$ACVM_HOME`, for a shell that needs to select between toolchains.
2. The packer's own directory — but only if it actually contains a runtime, which is the
   case when the packer has been placed beside one.
3. The build directory recorded at configure time.

Each candidate is validated before it is used, so a wrong guess falls through to the next
rather than failing later with an unrelated message. The third fallback exists for the same
reason the compiler records its own standard-library path: a tool that cannot locate its
siblings is a tool every caller must configure.

## Where the libraries are found

Two locations, in this order, and no `PATH` lookup:

1. **Beside the `.axd` that declared the library.** This is what allows a module that lives
   in the project rather than in the standard library — `gui`, for instance — to be packed
   correctly.
2. **Beside the runtime.** The standard library ships with the compiler, not with its own
   sources.

Searching `PATH` would make the contents of a bundle depend on the machine that produced
it, and a bundle whose contents vary by machine is not a bundle.

A library that cannot be found fails the pack with exit code 3, and the bytecode already
written is removed. A program that needs `parallel.dll` does not fail until it calls
`parallel`, which is after the bundle has been handed over.

## How the launcher works

`MyApp.exe` is the Axiom runtime, renamed. Started without arguments, it looks for
`MyApp.acvm` in its own directory and runs it.

The check is on the sibling file rather than on the executable's own name, so nothing
depends on what the runtime was renamed to. Invoked with arguments, the runtime behaves as
it always does.

## Exit codes

| Code | Meaning |
|---|---|
| `0` | success |
| `2` | usage error, or an input that cannot be read |
| `3` | a required library was not found |
| `4` | a file could not be copied |
| `5` | verification failed |

## An unrelated defect this tool uncovered

`acvm run` marshals an external call directly from the AST. `acvm exec` uses the parameter
plan stored in the bytecode file. Container format v4 recorded the array flag but not the
string flag, so after a round trip a `string` argument was marshalled as a single scalar
and the callee received the VM's descriptor address where it expected characters. The
program faulted on the first external call that took a string.

The defect was invisible to `run`, which never round-trips through a file, and every bundle
this tool produces is executed through `exec` — which is how it was found.

The flag is now packed into the same byte as the array flag, leaving the record size
unchanged, and the container version was raised to v5. A v4 module is rejected explicitly
rather than loaded and faulting later.

Regression test:
`tests/test_e2e.cpp::a_saved_module_keeps_the_string_flag_of_external_parameters`.

## Not yet implemented

- **Version resource, icon, and manifest**, embedded at pack time with `llvm-rc`.
- **Checksums**, emitted as `app.exe.sha256`.
- **Self-hosting**, meaning `apack` producing a bundle of `apack` that can itself produce
  bundles.
- **Data files.** Only the program and its libraries are packaged; resources the program
  reads at run time must be placed alongside it.
