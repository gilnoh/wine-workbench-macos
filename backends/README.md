# backends/

One directory per graphics-translation backend, versioned. Contents are
gitignored (they are build outputs); this file documents the contract.

```
backends/<name>-<version>/
    backend.conf          # sourced by bin/gamerun
    x86_64-windows/*.dll  # PE DLLs, copied into a prefix's system32
    i386-windows/*.dll    # 32-bit equivalents, if the backend has them
    x86_64-unix/*.so      # unix-side libs, installed into the runtime tree
```

Layout mirrors CrossOver's `SharedSupport/CrossOver/lib/{dxvk,dxmt}/`, which
uses exactly these three subdirectory names.

## backend.conf

A shell fragment defining:

- `BACKEND_OVERRIDES` — the `WINEDLLOVERRIDES` string this backend needs,
  e.g. `dxgi,d3d11,d3d10core=n,b`
- `BACKEND_ENV` — bash array of extra `KEY=value` env assignments (optional)

Example (DXMT):

```sh
BACKEND_OVERRIDES="dxgi,d3d11,d3d10core=n,b"
BACKEND_ENV=( DXMT_LOG_LEVEL=warn )
```

## Note on unix-side libraries

DXMT's `winemetal.so` belongs in the *runtime* tree
(`runtimes/<rt>/lib/wine/x86_64-unix/`), not in a prefix. That mutates a shared
runtime, which is mildly against the "runtimes are immutable" idea — but the
library is inert unless `winemetal.dll` is actually loaded, so it is additive
and safe. `bin/backend-install` records whatever it copies into a runtime so
the change is traceable.

⚠️ **A unixlib's PE half has to go into the runtime as well**, not only into the
prefix — so `winemetal.dll` is installed to *both*
`runtimes/<rt>/lib/wine/x86_64-windows/` and the prefix's `system32`. Wine
locates a unixlib by taking the path of the **builtin** PE it just loaded and
swapping `.dll` → `.so` (`dlls/ntdll/unix/loader.c`, inside `find_builtin_dll`).
A PE that exists only in the prefix is loaded as an ordinary native module, so
ntdll never derives a path for the `.so` and `DllMain` aborts the process:

```
warn:module:find_builtin_dll cannot find builtin library for L"...\winemetal.dll"
err:module:loader_init "winemetal.dll" failed to initialize, aborting
```

with status `c0000142`. Measured 2026-08-14 — this is why DXMT had never once
loaded here, and it reads like a bad backend build rather than a missing file.
`bin/backend-install` now copies the pair automatically: for every `*.so` it
installs, it looks for the same-named `.dll` in `x86_64-windows/` and places
that in the runtime too.
