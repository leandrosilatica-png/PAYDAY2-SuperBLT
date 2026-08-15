# PAYDAY 2 SuperBLT — performance fork

[![Build](https://img.shields.io/github/actions/workflow/status/leandrosilatica-png/PAYDAY2-SuperBLT/create_build.yml?label=build)](https://github.com/leandrosilatica-png/PAYDAY2-SuperBLT/actions/workflows/create_build.yml)

This is my performance-focused fork of SuperBLT for PAYDAY 2's 64-bit build. The goal is simple: cut the avoidable startup and loading overhead without breaking the Lua, Wren or plugin APIs mods already use.

I'm not rebranding SuperBLT or pretending I wrote the original loader. This fork keeps its compatibility, licence and credits intact. It just stops doing a bunch of expensive work it never needed to do.

## What changed

- Release optimisation works again for every C++ dependency. The old build quietly replaced `/O2` with `/Od` across the whole tree.
- The hottest safe SuperBLT paths are optimised too. Compiler-sensitive game/Lua bridge code stays isolated until it can be tested in-game properly.
- Signature cache misses use an anchored scan and bail on the first mismatched byte instead of grinding through every byte of every pattern.
- Existing 64-bit assets skip the full read, copy, conversion and datastore rebuild path.
- Async file I/O, hashing and HTTP use bounded workers instead of spawning and retaining a new OS thread for every job.
- Hashing streams large files, reuses the Windows SHA-256 provider and no longer allocates lowercase path copies inside every sort comparison.
- HTTP response chunks append straight into the output buffer, progress events are throttled and ordered safely, and TLS certificate checks are back on.
- The inherited DLL self-updater is disabled. It points at upstream and would replace this fork the moment the version changed.
- Logging no longer flushes the file after every normal line.
- Cached XAudio buffers no longer leak a fresh OpenAL buffer on every cache hit.

No fake FPS claims. These changes target startup, asset loading, mod I/O, network work and build output—the places this DLL can actually affect.

The full technical breakdown, before/after numbers and test notes are in [CHANGES.md](CHANGES.md).

## Download and install

Fork builds are attached to the latest successful [GitHub Actions run](https://github.com/leandrosilatica-png/PAYDAY2-SuperBLT/actions/workflows/create_build.yml).

If you want the current signed upstream beta instead, use [PAYDAY 2 SuperBLT on ModWorkshop](https://modworkshop.net/mod/58342).

Put `WSOCK32.dll` beside `PAYDAY2.exe`, then launch the game. SuperBLT creates `mods/` and downloads the matching [basemod](https://modworkshop.net/mod/58345) automatically.

Fork DLL updates are manual on purpose. Grab a newer Actions build when I publish one; this DLL won't quietly replace itself with the upstream build.

Never install both `WSOCK32.dll` and `IPHLPAPI.dll`. Keep `WSOCK32.dll` unless your machine specifically needs the other name.

## Build it

You need Visual Studio 2022 with the Desktop development with C++ workload, CMake and Python 3.

```powershell
git clone --recursive https://github.com/leandrosilatica-png/PAYDAY2-SuperBLT.git
cd PAYDAY2-SuperBLT
cmake -S . -B build -A x64 -G "Visual Studio 17 2022"
cmake --build build --config RelWithDebInfo --target SuperBLT --parallel
```

The DLL and PDB land in `build/RelWithDebInfo/`.

If you cloned without the submodules:

```powershell
git submodule update --init --recursive
```

For local game testing, copy the built DLL beside `PAYDAY2.exe`, or create a symlink from the game directory:

```powershell
cmd /c mklink WSOCK32.dll C:\full\path\to\PAYDAY2-SuperBLT\build\RelWithDebInfo\WSOCK32.dll
```

## Documentation

The existing Lua and plugin documentation is on the [SuperBLT website](https://superblt.znix.xyz). XAudio is enabled in this build; the old README saying it was gone was stale.

The Lua basemod lives in [diesel-modding/PAYDAY2-SuperBLT-Lua](https://github.com/diesel-modding/PAYDAY2-SuperBLT-Lua).

This project inherits SuperBLT's GPL-3.0 licence and original contributor history. See [CREDITS.md](CREDITS.md) and [LICENSE.txt](LICENSE.txt).
