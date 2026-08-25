/* What DirectInput8 enumerates, and whether the device can be acquired and read.
 * The XInput counterpart is xinputprobe.c; run both, because a pad can be
 * perfect on one API and absent on the other -- winexinput.sys splits an
 * XInput-capable gamepad into an &XI_00 node (xinput) and an &IG_00 node
 * (dinput), and only the second is what DirectInput games see.
 *
 *   WINEPREFIX=... runtimes/<rt>/bin/wine tools/probes/dinputprobe.exe [seconds]
 */
#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

static int count;
static const GUID guid_null;

static BOOL CALLBACK enum_cb(LPCDIDEVICEINSTANCEW inst, void *ctx)
{
    printf("  [%d] %ls  (product: %ls)  type %02x subtype %02x\n", count++,
           inst->tszInstanceName, inst->tszProductName,
           GET_DIDEVICE_TYPE(inst->dwDevType), GET_DIDEVICE_SUBTYPE(inst->dwDevType));
    if (IsEqualGUID((GUID *)ctx, &guid_null)) *(GUID *)ctx = inst->guidInstance;
    return DIENUM_CONTINUE;
}

int main(int argc, char **argv)
{
    int seconds = (argc > 1) ? atoi(argv[1]) : 15, ticks;
    IDirectInput8W *di = NULL;
    IDirectInputDevice8W *dev = NULL;
    GUID first = {0};
    DIJOYSTATE2 st, prev;
    HRESULT hr;

    hr = DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION,
                            &IID_IDirectInput8W, (void **)&di, NULL);
    if (FAILED(hr)) { printf("DirectInput8Create failed: %08lx\n", (unsigned long)hr); return 2; }

    printf("DI8DEVCLASS_GAMECTRL, DIEDFL_ATTACHEDONLY:\n");
    hr = IDirectInput8_EnumDevices(di, DI8DEVCLASS_GAMECTRL, enum_cb, &first, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr)) { printf("EnumDevices failed: %08lx\n", (unsigned long)hr); return 2; }
    if (!count) { printf("  (none)\n\nno DirectInput game controller enumerated\n"); return 1; }

    hr = IDirectInput8_CreateDevice(di, &first, &dev, NULL);
    if (FAILED(hr)) { printf("\nCreateDevice failed: %08lx\n", (unsigned long)hr); return 2; }
    if (FAILED(hr = IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2)))
    { printf("SetDataFormat failed: %08lx\n", (unsigned long)hr); return 2; }
    if (FAILED(hr = IDirectInputDevice8_Acquire(dev)))
    { printf("Acquire failed: %08lx\n", (unsigned long)hr); return 2; }

    printf("\nacquired -- press buttons / move sticks, %d s\n", seconds);
    fflush(stdout);
    memset(&prev, 0, sizeof(prev));
    for (ticks = 0; ticks < seconds * 100; ticks++)
    {
        if (SUCCEEDED(IDirectInputDevice8_Poll(dev)) &&
            SUCCEEDED(IDirectInputDevice8_GetDeviceState(dev, sizeof(st), &st)) &&
            memcmp(&st, &prev, sizeof(st)))
        {
            int b, mask = 0;
            for (b = 0; b < 16; b++) if (st.rgbButtons[b] & 0x80) mask |= 1 << b;
            /* Print all six axes. An earlier version printed only X/Y/Z/Rz and
             * made the right stick look dead: SDL puts it on Rx/Ry. An axis you
             * did not print is not an axis that read zero. */
            printf("  buttons %04x  X %6ld Y %6ld Z %6ld  Rx %6ld Ry %6ld Rz %6ld  POV %ld\n",
                   mask, st.lX, st.lY, st.lZ, st.lRx, st.lRy, st.lRz, (long)st.rgdwPOV[0]);
            fflush(stdout);
            prev = st;
        }
        Sleep(10);
    }
    printf("done\n");
    return 0;
}
