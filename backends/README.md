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
