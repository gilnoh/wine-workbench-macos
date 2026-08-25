/* Reproduce, outside Wine, what winebus does to libSDL2 at service start:
 * dlopen it from a worker thread.  This is the whole of the "NSWindow should
 * only be instantiated on the main thread!" crash -- no prefix, no Steam, no
 * game needed, and it runs in under a second.
 *
 *   clang -arch x86_64 -O0 -o sdlopen sdlopen.c
 *   ./sdlopen <path-to-libSDL2-2.0.0.dylib>
 *
 * Exit 0 with "worker returned cleanly"  -> a real SDL2; winebus will be fine.
 * Exit 1 "dlopen FAILED"                 -> not loadable at all.
 * Crash with NSInternalInconsistencyException, stack ending in
 *   error_dialog -> [NSAlert init] -> [NSWindow _initContent:...]
 *                                        -> it is sdl2-compat without libSDL3
 *   beside it: the shim cannot find SDL3 and pops an AppKit alert from
 *   whatever thread called dlopen.  See docs/RUNTIME.md section 1.
 *
 * The architecture matters: the runtime is x86_64 under Rosetta, and Homebrew's
 * two prefixes hold different builds, so always build this -arch x86_64 and
 * point it at the library the runtime actually uses
 * (grep SONAME_LIBSDL2 build/<runtime>/include/config.h).
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static const char *g_path;

static void *worker(void *unused)
{
    void *h = dlopen(g_path, RTLD_NOW);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); exit(1); }
    printf("dlopen OK, handle %p, SDL_Init %p\n", h, dlsym(h, "SDL_Init"));
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t t;
    if (argc < 2) { fprintf(stderr, "usage: %s <libSDL2-2.0.0.dylib>\n", argv[0]); return 2; }
    g_path = argv[1];
    pthread_create(&t, NULL, worker, NULL);
    pthread_join(t, NULL);
    printf("worker returned cleanly\n");
    return 0;
}
