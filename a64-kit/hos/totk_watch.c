/* TOTK debugging aids built on hos_watch.c (Windows; nothing happens unless asked).

   HOS_WATCH_STICK=1 (v1.4.3): the way from the controller to the camera, by who reads the right stick.
   - f_0ab28348, the sead Npad controller's update, stores the left stick at +0x120 and the right stick at +0x128 as
     floats (the Npad state's sticks / 32767). Npad No.1's controller (device record index 0 at +0x178) is found here.
   - f_08479c20, the controller manager's update (x0: the manager): its controllers are at manager+0x180 (count at
     +0x178); a merged controller takes the first moving stick of its children (children at +0x188, count +0x180)
     into its own +0x128. Watched (the first run found only these two functions and the idle timer f_08d940d0 reading
     the Npad controller): the merged controllers that have Npad No.1's among their children, so the log names the
     code that reads the stick the game plays with. HOS_WATCH_STICK=npad watches the Npad controller instead. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if GAME_VERSION == 143
static uint64_t npad_ctl;
static int watch_mode(void) {   /* 0 off, 1 merged controllers, 2 the Npad controller */
    static int m = -1;
    if (m < 0) {
        const char *e = getenv("HOS_WATCH_STICK");
        m = !e || !*e || *e == '0' ? 0 : !strcmp(e, "npad") ? 2 : 1;
    }
    return m;
}
#ifndef __EMSCRIPTEN__
void hos_watch_arm(uint64_t guest, int len, const char *why);
#endif

ENTRY_HOOK(0x0ab28348) {
    if (!watch_mode() || npad_ctl) return 0;
    const uint64_t ctl = c->x[0];
    if (G32(ctl + 0x178) != 0) return 0;   /* (the device record index: 0 = Npad No.1) */
    npad_ctl = ctl;
    hos_log("watch: Npad No.1 controller at %#llx", (unsigned long long)ctl);
#ifndef __EMSCRIPTEN__
    if (watch_mode() == 2) hos_watch_arm(ctl + 0x128, 8, "Npad right stick (controller+0x128)");
#endif
    return 0;
}

ENTRY_HOOK(0x08479c20) {
    static int done;
    if (watch_mode() != 1 || !npad_ctl || done) return 0;
    uint64_t hos_guest_size(void);
    const uint64_t mgr = c->x[0], size = hos_guest_size();
    const int n = (int)G32(mgr + 0x178);
    const uint64_t list = G64(mgr + 0x180);
    if (n <= 0 || n > 64 || list < 0x8000000 || list + 8ull * n > size) return 0;
    done = 1;
    int armed = 0;
    for (int i = 0; i < n && armed < 3; i++) {
        const uint64_t ctl = G64(list + 8ull * i);
        if (ctl < 0x8000000 || ctl + 0x190 > size) continue;
        const int kids = (int)G32(ctl + 0x180);
        const uint64_t kl = G64(ctl + 0x188);
        if (kids <= 0 || kids > 16 || kl < 0x8000000 || kl + 8ull * kids > size) continue;
        for (int k = 0; k < kids; k++)
            if (G64(kl + 8ull * k) == npad_ctl) {
                static char why[3][64];
                snprintf(why[armed], sizeof why[armed], "merged controller %d right stick", i);
                hos_log("watch: controller %d of %d (%#llx) merges Npad No.1's (child %d of %d)", i, n,
                        (unsigned long long)ctl, k, kids);
#ifndef __EMSCRIPTEN__
                hos_watch_arm(ctl + 0x128, 8, why[armed]);
#endif
                armed++;
                break;
            }
    }
    if (!armed) hos_log("watch: no controller of the %d at %#llx merges Npad No.1's", n, (unsigned long long)mgr);
    return 0;
}
#endif
