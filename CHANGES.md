# What I changed and why

This isn't a pile of random micro-optimisations or a blind `/O2` switch. I went through startup, asset conversion, file I/O, hashing, HTTP, audio, logging and the actual build output, then fixed the stuff doing measurable or obviously wasteful work.

## The numbers

| RelWithDebInfo build | Before | After | Difference |
| --- | ---: | ---: | ---: |
| `WSOCK32.dll` | 8,299,520 bytes | 3,432,448 bytes | 4,867,072 bytes smaller (58.6%) |
| PE code section | 6,712,832 bytes | 2,401,280 bytes | 64.2% smaller |
| Exported symbols | 344 | 344 | no ABI exports lost |

The size drop isn't the goal by itself. It's proof that compiler optimisation, dead-code removal and identical-code folding are finally doing real work. The old config built Release-flavoured C++ with optimisation disabled.

## Build and compiler fixes

- Stopped rewriting `/O2` to `/Od` for every C++ target in the project. That one global workaround was kneecapping OpenAL, DieselFormats, converters and the rest of the dependency tree.
- Kept the compiler-sensitive SuperBLT game/Lua bridge quarantined. The hot files that don't cross that ABI boundary get `/O2`; the risky bridge code stays conservative until it has proper in-game coverage.
- Added `/OPT:REF`, `/OPT:ICF` and non-incremental linking to production-style builds so unused and duplicate code is actually removed.
- Added MSVC parallel compilation. Clean local compile time is roughly half the inherited baseline on this machine.
- Put LuaJIT and the DLL on the same dynamic MSVC runtime. That removes the `/MT` versus `/MD` mismatch and its `LNK4098` warning.
- Fixed libpsl's generated numeric version. `0.21.5` was being ASCII-encoded into the macro instead of emitted as `0x1505`.
- Turned curl's bundled zlib support on for real. The old comments said it was enabled while the generated config explicitly disabled it.
- Left global `/arch:AVX2` alone. zlib-ng already picks SSE, AVX2 and AVX-512 code at runtime; forcing AVX2 across the DLL would just break older CPUs for no good reason.

## Startup and signature scanning

- Disabled the inherited DLL self-updater by default. It downloads the signed upstream ModWorkshop build, so leaving it on would eventually replace this fork with upstream. Fork DLL updates are manual instead.
- Kept the upstream updater behind an explicit `SBLT_ENABLE_UPSTREAM_DLL_UPDATES` CMake opt-in. If somebody deliberately enables it, routine checks return immediately, pending updates are revalidated, and failed checker/installer processes no longer count as success.
- Reworked signature cache misses around a fixed-byte `memchr` anchor.
- Pattern checks now stop on the first mismatched byte. The old loop checked every byte even after the match was already impossible.
- Fixed the final scan boundary, missing-signature offset handling, cache sentinels and reversed "too many/not enough" messages while I was in there.
- Query the executable module once per scan instead of once per signature.

## Asset loading

- Added tiny header probes for scriptdata, animation, font and massunit files.
- Files that are already 64-bit now go straight through. They no longer get fully read, copied, passed through a converter, deleted and rebuilt in a memory datastore for nothing.
- ScriptSerializer now checks the existing `PDString` before allocating a conversion buffer.
- Compressed animation conversion no longer duplicates the entire compressed input before allocating its output.
- Removed four illegal `vector<char>` to `vector<uint8_t>` object casts. The replacement copies the bytes once into the final correctly typed buffer, which is no more payload copying than the old `const &&` path already did.
- File-backed datastores use positional overlapped reads. Concurrent callers can't corrupt each other by racing a shared seek cursor anymore.

## Async work and queues

- Fixed a real use-after-free in async Lua reads: the next-frame callback captured a worker-local vector by reference after that vector was dead.
- Replaced the timeout-based Lua I/O workers with a bounded persistent pool. The old exit/dispatch race could leave work stranded.
- Replaced one-thread-per-hash and one-thread-per-HTTP-request growth with bounded worker queues.
- Event queues swap their pending deque under the lock, then run callbacks after releasing it. Producers aren't blocked while the main thread executes every callback.
- HTTP progress and completion now share one ordered event stream. A late progress callback can't point at an `HTTPItem` the completion queue already destroyed.
- Progress counts are 64-bit and updates are throttled to 64 KiB steps instead of allocating an event for every curl tick.

The worker managers deliberately live for the process lifetime. SuperBLT itself is a process-lifetime proxy DLL, and joining worker threads from CRT teardown while Windows holds the loader lock is a deadlock trap.

## Hashing and file work

- Directory hashing enumerates each folder once instead of once for files and again for directories.
- Nested `.git` and `.hg` folders are actually ignored now; the old recursion accidentally turned that option off after the first level.
- Reparse-point directories are skipped so a junction can't loop the walk forever.
- Case-insensitive sorting no longer copies and lowercases both full paths on every comparison.
- SHA-256 reuses one Windows CNG provider and streams files through a 256 KiB buffer instead of loading every file into RAM.
- The final hash format stays compatible. Known SHA-256, double-hash and nested-directory golden tests all pass.
- File reads allocate once from the known file size instead of growing through stream iterators.

## HTTP, logging and audio

- HTTP response bodies append the incoming curl chunk directly. No temporary string per callback.
- Response headers avoid duplicate map searches and unnecessary substring allocations.
- TLS peer and hostname verification are back on. Shipping downloads with certificate checks disabled wasn't acceptable.
- Normal log lines stay buffered. Warnings, errors and explicit flushes still hit disk immediately, but ordinary chatty mod logs don't force a filesystem flush on every line.
- Wwise lookup tables use hash maps, avoid double lookups and release their mutex before calling back into the sound engine.
- Wwise hashing is linear now. The old loop recalculated `strlen` for every character.
- XAudio checks its cache before generating an OpenAL buffer. Cache hits no longer leak an unused buffer ID.

## Correctness fixes picked up on the way

- `DBAssetHook.enabled` no longer dereferences an empty `optional`, and it now returns the state its documentation promises.
- HTTP fields have safe defaults when curl can't initialise a request.
- Signature failure no longer adds an offset to a null result and turns it into a fake non-null address.
- Several unaligned binary reads now use `memcpy` instead of typed pointer casts.
- Loader and runtime errors now say what actually failed and what the player should do next.

## Documentation and CI

- Rewrote the stale README in my own voice and pointed every fork link at this repository.
- Removed the dead Nightly.link download, the wrong RAID ModWorkshop page, the false manual-basemod step and the claim that XAudio is disabled.
- Simplified the build workflow: no redundant Chocolatey installs, no second submodule checkout, parallel CMake builds, proper path triggers, read-only permissions and cancellation of obsolete runs.
- Made version generation fall back to the commit ID when a fork has no copied Git tags, so a clean GitHub Actions checkout still builds.
- Kept the SuperBLT name, public API strings, updater trust key, licence and original credits intact.

## What still needs a real game test

Both Release and RelWithDebInfo compile and link cleanly, the export count matches, generated flags were inspected, hash compatibility tests pass and the changed C++ is clang-formatted. That still isn't the same thing as launching PAYDAY 2 with a real mod stack.

Before calling this a stable release, I want an in-game smoke test covering startup with and without a signature cache, Lua/Wren loading, 32-bit asset conversion, XAudio cache hits, HTTP callbacks and a few popular mods. That's also why I didn't recklessly turn `/O2` on for the entire game/Lua bridge or throw `/GL` at it and hope for the best.
