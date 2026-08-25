/* What XInput reports -- i.e. what a game actually sees, not what the driver
 * stack says it built. joy.cpl proves a device node exists; this proves axes
 * and buttons carry values.
 *
 *   WINEPREFIX=... runtimes/<rt>/bin/wine tools/probes/xinputprobe.exe [seconds]
 *
 * Prints one line per changed state packet: which of the four XInput slots is
 * connected, the button mask, both sticks and both triggers. If nothing is
 * connected it says so and exits 1 -- which is the answer, not a failure to
 * produce one.
 */
#include <windows.h>
#include <xinput.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int seconds = (argc > 1) ? atoi(argv[1]) : 15;
    DWORD last[XUSER_MAX_COUNT] = {0};
    int connected = 0, i, ticks;

    for (i = 0; i < XUSER_MAX_COUNT; i++)
    {
        XINPUT_STATE st;
        DWORD r = XInputGetState(i, &st);
        printf("slot %d: XInputGetState -> %lu%s\n", i, (unsigned long)r,
               r == ERROR_SUCCESS ? "  CONNECTED" : (r == ERROR_DEVICE_NOT_CONNECTED ? "  (empty)" : "  (error)"));
        if (r == ERROR_SUCCESS) { connected++; last[i] = st.dwPacketNumber; }
    }
    if (!connected) { printf("\nno XInput device connected\n"); return 1; }

    printf("\npress buttons / move sticks -- %d s\n", seconds);
    fflush(stdout);
    for (ticks = 0; ticks < seconds * 100; ticks++)
    {
        for (i = 0; i < XUSER_MAX_COUNT; i++)
        {
            XINPUT_STATE st;
            if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;
            if (st.dwPacketNumber == last[i]) continue;
            last[i] = st.dwPacketNumber;
            printf("slot %d  buttons %04x  L(%6d,%6d) R(%6d,%6d)  LT %3u RT %3u\n",
                   i, st.Gamepad.wButtons,
                   st.Gamepad.sThumbLX, st.Gamepad.sThumbLY,
                   st.Gamepad.sThumbRX, st.Gamepad.sThumbRY,
                   st.Gamepad.bLeftTrigger, st.Gamepad.bRightTrigger);
            fflush(stdout);
        }
        Sleep(10);
    }
    printf("done\n");
    return 0;
}
