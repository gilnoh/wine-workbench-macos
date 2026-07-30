# Retracted patches

These hardcoded absolute Homebrew paths at each `dlopen(SONAME_LIB*)` site in
Wine. They worked, and they are why Steam's client renders — but they were the
wrong shape, and the list kept growing: `win32u/vulkan.c`,
`win32u/freetype.c`, `dwrite/freetype.c`, `crypt32/unixlib.c`,
`secur32/schannel_gnutls.c`. Five sites is not five bugs, it is one policy.

Wine `dlopen`s by soname **by design** and delegates to the dynamic loader.
Linux extends that search path with `/etc/ld.so.conf.d/`; macOS has no
equivalent and dyld searches only `/usr/lib` plus the shared cache. So the
mismatch is the platform's, not Wine's.

Replaced by `bin/bundle-libs` plus soname seeding in `bin/build-runtime`:
the libraries are copied into `runtimes/<name>/lib/bundled` with their
transitive dependencies and install names rewritten to `@loader_path`, and
configure is given `ac_cv_lib_soname_*=<absolute path into the bundle>`. That
covers every site at once, including ones nobody has hit yet, needs no source
change, and survives `brew upgrade`.

Kept for reference only. Do not re-apply.
