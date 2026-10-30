/* TOTK debugging aids built on hos_watch.c (Windows; nothing happens unless asked).

   HOS_WATCH_STICK=1 (v1.4.3): finds the game's controller for Npad No.1 (the sead Npad controller's update,
   f_0ab28348, which stores the left stick at +0x120 and the right stick at +0x128 as floats, from the Npad state's
   sticks / 32767) and watches its right stick: the log then names the code that reads it, the first step of the
   way to the camera (for a mouse camera that turns it directly). */
#include "hos.h"
#include <stdlib.h>

#if GAME_VERSION == 143
ENTRY_HOOK(0x0ab28348) {
    static int armed, on = -1;
    if (on < 0) on = getenv("HOS_WATCH_STICK") && *getenv("HOS_WATCH_STICK") == '1';
    if (!on || armed) return 0;
    const uint64_t ctl = c->x[0];
    if (G32(ctl + 0x178) != 0) return 0;   /* (the device record index: 0 = Npad No.1) */
    armed = 1;
#ifndef __EMSCRIPTEN__
    void hos_watch_arm(uint64_t guest, int len, const char *why);
    hos_log("watch: Npad No.1 controller at %#llx", (unsigned long long)ctl);
    hos_watch_arm(ctl + 0x128, 8, "right stick (controller+0x128)");
#endif
    return 0;
}
#endif
