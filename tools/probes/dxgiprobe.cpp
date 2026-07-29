/* Enumerate DXGI adapters the way Chromium's CollectBasicGraphicsInfo does. */
#include <windows.h>
#include <dxgi.h>
#include <cstdio>

int wmain(void)
{
    IDXGIFactory *factory = nullptr;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory);
    wprintf(L"CreateDXGIFactory hr=0x%08lx\n", hr);
    if (FAILED(hr) || !factory) return 1;

    UINT i = 0;
    IDXGIAdapter *ad = nullptr;
    int n = 0;
    while (factory->EnumAdapters(i, &ad) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC d = {};
        if (SUCCEEDED(ad->GetDesc(&d)))
        {
            wprintf(L"adapter %u: vendor=0x%04x device=0x%04x vram=%lluMB desc=%ls\n",
                    i, d.VendorId, d.DeviceId,
                    (unsigned long long)(d.DedicatedVideoMemory >> 20), d.Description);
            n++;
        }
        ad->Release();
        i++;
    }
    wprintf(L"total adapters = %d\n", n);
    factory->Release();
    return 0;
}
