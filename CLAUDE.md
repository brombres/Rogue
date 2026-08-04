# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

Rogue is an ergonomic object-oriented language that compiles to C. This repo contains the **self-hosted compiler** (`roguec`), written in Rogue itself, plus the Standard Library. The compiler emits portable C; you then run a C compiler to produce the final executable.

## Build system: two stages

There are two distinct build paths and you need to know which to use:

1. **`make`** (top-level Makefile) — bootstraps `roguec` from the pre-generated C in `Source/RogueC/Bootstrap/RogueC.{c,h}`. Use this only on a fresh clone or when you need to rebuild from the bootstrap snapshot. It compiles to `Build/RogueC-<Platform>` and symlinks it to `/usr/local/bin/roguec`.

2. **`rogo`** (driven by `Build.rogue`) — the normal development loop. Rogo is a separate tool (installed via Morlock) that compiles and runs `Build.rogue` as the project's actual build script. All routines named `rogo_xxx` in `Build.rogue` become subcommands.

Common `rogo` commands (defined in `Build.rogue`):

| Command | Purpose |
|---------|---------|
| `rogo` | default: build + run `--version` |
| `rogo build` | recompile Rogue → C → exe if sources changed |
| `rogo rebuild` | force full rebuild (re-runs froley too) |
| `rogo debug` / `rogo release` | build with mode + run |
| `rogo froley` | regenerate Scanner/Parser from `Rogue.froley` |
| `rogo libs` | sync `Source/Libraries` → `Build/Libraries` |
| `rogo clean` | delete `Build/` and `.rogo/` |
| `rogo bootstrap` | build from `Source/RogueC/Bootstrap/*.c` (no roguec needed) |
| `rogo update_bootstrap` | regenerate the bootstrap C/H from a fresh build (after compiler changes) |
| `rogo install` / `rogo link` | install launcher into `/usr/local/bin` (or via Morlock) |
| `rogo publish <ver>` | bump version, commit, update bootstrap, push GitHub release, update wiki |
| `rogo update_version <ver>` | edit version/date in `RogueC.rogue` and `README.md` |

`Local.settings` (gitignored) overrides `Build` properties like `BUILD_MODE`, `ROGUEC_FLAGS`, `LAUNCHER_FOLDER`. `BuildLocal.rogue` (gitignored) lets a user add personal `rogo_xxx` routines.

## Compiler pipeline (high level)

Entry point: `Source/RogueC/RogueC.rogue`. The compiler is a multi-pass pipeline; understanding the order matters when editing it.

1. **Lexer/parser are generated.** `Source/RogueC/Rogue.froley` is the grammar definition consumed by the external `froley` tool, which writes `Scanner.rogue`, `Parser.rogue`, `Token.rogue`, `TokenType.rogue`, `ScannerCore.rogue`, `ParserCore.rogue`, `ScanTable.rogue`. The last two are gitignored — they get regenerated. **Don't hand-edit any of these files**; edit `Rogue.froley` and run `rogo froley`.

2. **Includes & organization.** `Program.include(...)` pulls in source. `StandardMacros` is loaded first, then user files, then `Standard`. Library search paths come from the executable's location (`Build/Libraries`) or `Source/Libraries`. `check_autoincludes` lazily pulls in DateTime/File/Introspection/Process/Promise/Scanner/Set/Table/Variant when the scanner sees them used.

3. **Organize → Resolve → Generate.** `Program.organize` builds types/methods, `Program.resolve` does semantic analysis, then `Program.generate_c` emits C through `CGenerator` / `CWriter`. Between these the many `*Patcher`, `*Analyzer`, `*Visitor`, `Cmd*` files in `Source/RogueC/` implement individual passes (control-flow, scope, virtual call collection, dynamic→static dispatch conversion, dead-code culling, exception analysis, inline-foreach transform, etc.).

4. **Output.** C is written to `Build/RogueC-<OS>.{h,c}` (or split across multiple `.c` files with `--split`). For the self-build, `rogo_compile_c` then invokes the system C compiler with `-O3` (release) or `-O0` (debug) and links `-lm`.

## Updating the bootstrap

The bootstrap C in `Source/RogueC/Bootstrap/` lets new contributors build without an existing `roguec`. After making compiler changes that you want available to bootstrappers (typically only when publishing a release), run `rogo update_bootstrap`. This compiles with the current `roguec`, then copies `Build/RogueC-<OS>.{c,h}` into `Source/RogueC/Bootstrap/RogueC.{c,h}` (renaming OS-specific symbols). `rogo publish` does this automatically.

## Standard Library layout

`Source/Libraries/Standard/` — `Standard.rogue` is the umbrella include. Subfolders: `Codec`, `Collection`, `Console`, `Control`, `Core`, `DateTime`, `Entity`, `FileIO`, `Geometry`, `Graphics`, `Introspection`, `IO`, `Math`, `Network`, `OS`, `UI`, `Utility`, `VM`. `StandardMacros.rogue` is included before user code.

The library is **copied into `Build/Libraries`** by `rogo libs` so the installed `roguec` can find it next to the executable. Editing files under `Source/Libraries` and forgetting to re-sync (or running `roguec` from a stale `Build/`) is a common source of confusion.

## Testing changes

There is no formal test suite committed at the repo root. The convention is to write ad-hoc cases in `Test.rogue` (gitignored) and compile/run them, e.g.:

```
roguec Test.rogue --main --debug --target=Console,C,macOS
cc -Wall -fno-strict-aliasing Test.c -o test -lm
./test
```

Adjust `--target` to `Linux` or `Windows` (`cl /nologo Test.c /Fetest.exe` on Windows). Many Rogue downstream projects (Rogo, Morlock, Vimage, etc.) live as sibling repos and serve as integration tests when bootstrap-related changes land.

## Conventions worth knowing

- The compiler hard-codes a self-bootstrap assumption: `roguec` discovers its Standard Library via the executable's folder (looking for `Libraries/`, then `Source/Libraries/`). Moving the binary without its sibling `Libraries/` will break it.
- `--main` tells `roguec` to emit a `main()`; without it the C output is library-style.
- `--gc=manual` is used for the self-build (see `ROGUEC_FLAGS` in `Build.rogue`).
- `--split[=N]` splits the generated C across multiple files for compilers that choke on huge translation units (used in two-stage rebuilds, e.g. `rogo_x2` / `rogo_x3` patterns).
- Version and date strings live in `Source/RogueC/RogueC.rogue` (`$define VERSION`, `$define DATE`) and `README.md`; `rogo update_version` is the only correct way to bump them.
- `V1/` is the legacy Rogue 1.x tree, kept for reference. The `v1` git branch is the live one.
