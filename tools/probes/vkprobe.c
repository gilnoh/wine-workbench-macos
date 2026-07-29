#include <windows.h>
#include <stdio.h>

typedef int (__stdcall *PFN_EIV)(unsigned int *);
typedef int (__stdcall *PFN_EIEP)(const char*, unsigned int*, void*);

int main(void)
{
    HMODULE h = LoadLibraryA("vulkan-1.dll");
    printf("LoadLibrary(vulkan-1.dll) = %p (err %lu)\n", (void*)h, GetLastError());
    if (!h) return 1;

    char path[MAX_PATH] = {0};
    GetModuleFileNameA(h, path, sizeof(path));
    printf("resolved to: %s\n", path);

    PFN_EIV eiv = (PFN_EIV)GetProcAddress(h, "vkEnumerateInstanceVersion");
    printf("vkEnumerateInstanceVersion = %p\n", (void*)eiv);
    if (eiv) {
        unsigned int v = 0;
        int r = eiv(&v);
        printf("  -> res=%d version=%u.%u.%u (raw 0x%08x)\n",
               r, v >> 22, (v >> 12) & 0x3ff, v & 0xfff, v);
    } else {
        printf("  -> ABSENT: app must assume Vulkan 1.0\n");
    }

    PFN_EIEP eiep = (PFN_EIEP)GetProcAddress(h, "vkEnumerateInstanceExtensionProperties");
    if (eiep) {
        unsigned int n = 0;
        int r = eiep(NULL, &n, NULL);
        printf("instance extensions: res=%d count=%u\n", r, n);
    }
    return 0;
}
