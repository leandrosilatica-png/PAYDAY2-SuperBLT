# DLL loader

PAYDAY 2 loads `WSOCK32.dll` from its game directory before the Windows copy. SuperBLT uses that slot to enter the process, loads the real system DLL, then forwards every networking export to it.

The same binary also exports the symbols needed when it is named `IPHLPAPI.dll`. Rename it only if `WSOCK32.dll` genuinely does not load on that machine. Never install both names at once.

## How the proxy works

Using `SetIpTTL` as an example:

- `SetIpTTL` is the public symbol exported by both SuperBLT and the Windows DLL.
- `SBLT_PROXY_IPHLPAPI_SetIpTTL` is SuperBLT's generated forwarding symbol. The DEF file exposes it as `SetIpTTL`.
- `SBLT_PROXY_STRUCT_PTR.fptr_IPHLPAPI_SetIpTTL` points at the real Windows implementation after resolution.
- `SBLT_PROXY_IPHLPAPI_SetIpTTL_RESOLVE` is the generated first-call resolver.

The forwarding stub loads its function pointer into `RAX` and jumps straight to it. Before the Windows DLL has been resolved, that pointer targets the resolver instead.

The resolver saves the volatile registers, identifies the requested function and calls `SBLT_PROXY_LOADER_FN_CXX`. That function loads the correct DLL from the Windows system directory and fills the complete proxy table. Control returns through the resolver and retries the original call, which now jumps to the real function.

Every later call is just the generated forwarding jump. The Windows DLL is loaded lazily because calling `LoadLibrary` from `DllMain` is not safe.

## Why both filenames exist

Old BLT builds used `IPHLPAPI.dll`. Some Windows setups stopped loading that proxy reliably, so SuperBLT moved to `WSOCK32.dll`; a smaller set of machines still needs the old name.

Shipping one binary with both export sets avoids maintaining two builds. The trade-off is overlapping export ordinals. Symbol-name imports work, but an application importing `IPHLPAPI.dll` by ordinal could hit the wrong export. PAYDAY 2 uses the supported path.
