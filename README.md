# APACK

**A packager for Axiom programs, written in Axiom.**

It turns a `.ax` source file into a directory that runs on a machine which has neither the
compiler nor the standard library installed.

```sh
acvm run apack.ax -- demo.ax -o dist/MyApp --name MyApp
```

```
dist/MyApp/
  MyApp.exe      the runtime, renamed
  MyApp.acvm     the compiled bytecode
  io.dll         only the libraries the program actually reaches
```

The recipient needs no toolchain, no source, and no build step. They run `MyApp.exe`, or
double-click it.

## Why it is written in Axiom

A packager is a tool, and a language that cannot build its own tools is a language with a
ceiling. This is that claim made testable: if the program below produces a bundle that runs,
nothing in the work needed a feature the language did not already have.

It is testable in the literal sense, too. APACK can package itself:

```sh
acvm run apack.ax -- apack.ax -o dist/apack --name apack
dist/apack/apack.exe hello.ax -o dist/hello --runtime <toolchain>
```

The second command is the packaged packager producing another bundle, and that bundle runs.
Three generations, each built by the previous.

## How the dependency list is obtained

The packager has to answer one question: which shared library does each module the program
reaches live in?

That question is already answered by the import resolver, so APACK asks the compiler rather
than working it out again:

```sh
acvm deps demo.ax
io
```

One library name per line. Scanning the sources for `import` and `library` declarations here
would be a second implementation of module resolution, and the two would diverge the first
time a search rule changed. The failure mode is not an error message — it is a bundle that
omits a library, works on the machine that produced it, and traps on the first call at the
recipient's.

## Usage

```
apack <file.ax> -o <dir> [options]

  -o <dir>          output directory (required)
  --name <name>     launcher name, without extension (default: the source's stem)
  --runtime <dir>   where acvm and the standard library live
```

`--runtime` defaults to the directory holding the running executable, which is correct when
APACK is run through `acvm run`. A packaged APACK has no compiler beside it, so pass
`--runtime` when using one.

## Requirements

- The Axiom toolchain, with `acvm deps` — added alongside this packager, so an older build
  will not have the command.
- `extralib/cli.ax`, for argument parsing.

Nothing else. There is no build step: the packager is a script.

## What it does not do

- **Single-file output.** One executable containing the program and the standard library.
  That mode appends bytes to the runtime and writes an eight-byte little-endian footer; a
  `string` can carry those bytes, but there is no way to assemble one byte at a time yet.
- **Version resources, icons, manifests.** These are PE structures edited through the Win32
  resource API.
- **Checksums.** `sha256.ax` could produce one; it is left out so this file stays a
  demonstration of packaging rather than of hashing.
- **Data files.** Only the program and its libraries are packaged.

A native implementation of the packager exists and does all four. It is not published here:
two implementations in one repository make the claim above harder to read, and the
interesting one is the one written in the language being tested.

## A defect this found

Writing APACK found a bug in `extralib/cli.ax`. `cliParseArgvWith` built its command line from
Axiom's argument array, which starts at the first real argument, while the parser reads token
0 as the program name — so every program using it silently lost its first argument, and
`apack demo.ax -o dist` reported that no input file had been given.

It went unnoticed because every existing check builds a command line as a string, program name
included. Only the entry point that reads real arguments was affected, and nothing tested it.
The regression test is `cli_argv_check.ax`, registered with arguments for that reason.

## Licence

MIT. See `LICENSE`.
