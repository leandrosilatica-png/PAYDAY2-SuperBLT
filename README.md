# PAYDAY 2 SuperBLT — performance fork

[![Build](https://img.shields.io/github/actions/workflow/status/chronicmods/PAYDAY2-SuperBLT/create_build.yml?label=build)](https://github.com/chronicmods/PAYDAY2-SuperBLT/actions/workflows/create_build.yml)

This is my performance-focused SuperBLT build for PAYDAY 2's 64-bit version. It cuts avoidable startup and loading overhead without breaking the Lua, Wren or native plugin interfaces existing mods rely on.

## What changed

- Release optimisation works again without forcing unsafe flags onto compiler-sensitive game and Lua bridge code.
- Signature scanning is faster and existing 64-bit assets skip redundant conversion work.
- File I/O, hashing and HTTP use bounded workers instead of creating and retaining a new OS thread for every job.
- Hashing streams large files, HTTP work is ordered safely with TLS verification enabled, and normal log lines are buffered instead of flushed one by one.
- Cached XAudio buffers no longer leak OpenAL buffers, and the upstream DLL self-updater is disabled so it cannot replace this fork.

The exact technical breakdown, before-and-after numbers and test results are in [CHANGES.md](CHANGES.md).

## Download and install

Download `WSOCK32.dll` from the artifacts on the latest successful [build workflow](https://github.com/chronicmods/PAYDAY2-SuperBLT/actions/workflows/create_build.yml), put it beside `PAYDAY2.exe`, then launch the game. The matching basemod is downloaded automatically.

Fork DLL updates are manual. Grab a newer Actions build when I publish one.

Never install both `WSOCK32.dll` and `IPHLPAPI.dll`. Use `WSOCK32.dll` unless your machine specifically needs the alternate filename.

## Build it

You need Visual Studio 2022 with the Desktop development with C++ workload, CMake and Python 3.

```powershell
git clone --recursive https://github.com/chronicmods/PAYDAY2-SuperBLT.git
cd PAYDAY2-SuperBLT
cmake -S . -B build -A x64 -G "Visual Studio 17 2022"
cmake --build build --config RelWithDebInfo --target SuperBLT --parallel
```

The DLL and PDB land in `build/RelWithDebInfo/`.

If you cloned without the submodules, run:

```powershell
git submodule update --init --recursive
```

## Documentation and licence

The compatible Lua and plugin documentation is on the [SuperBLT website](https://superblt.znix.xyz).

This fork stays under GPL-3.0 and keeps the required notices and credits. See [LICENSE.txt](LICENSE.txt), [LICENSE-BLT.md](LICENSE-BLT.md), [LICENCE-WWISE.txt](LICENCE-WWISE.txt) and [CREDITS.md](CREDITS.md).
