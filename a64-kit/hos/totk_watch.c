/* TOTK debugging aids built on hos_watch.c (Windows; nothing happens unless asked).

   HOS_WATCH_STICK=1 (v1.4.3): the way from the controller to the camera, by who reads the right stick. What the
   runs so far found (sticks are floats: left at controller +0x120, right at +0x128):
   - f_0ab28348, the sead Npad controller's update: Npad No.1's controller (device record index 0 at +0x178).
   - f_08479c20, the controller manager's update (manager+0x180: its controllers, count at +0x178): merged controllers
     take the first moving stick of their children (+0x188, count +0x180). Merged controller 8 holds Npad No.1's
     alone (11 controllers on the title screen and in the prologue).
   - f_0837c944 copies controller 8, got by index 0 from f_0837ccc0 (the game's get-controller(manager, index, name)),
     into the game's main controller (its this+0x198) every frame, with f_0837d330 (x0 <- x1).
   - Of f_0837ccc0's 40 callers, three copy a controller's sticks into a node's output (right stick at +0x20, left at
     +0x28): f_0900d030, f_0a005070, f_0a616bf4.
   - The player camera's parameters (an object of 0x1f0 bytes made by f_08e0d3e4, read from its resource by
     f_09ac7484, x0 = the object) hold StickExponent at +0x180 (2.0 by default) and StickSensitivityLat2LngRatio at
     +0x184 (1.78): whatever reads them while playing is the camera's own stick handling, the place to give it the
     mouse. Watched now: those two (one breakpoint over both), the main controller's right stick, and the first output
     of f_0a005070 and f_0a616bf4.
   HOS_WATCH_STICK=npad watches the Npad controller itself; HOS_WATCH_STICK=cam the camera input's raw stick and
   result (totk_mouse_cam.c); HOS_WATCH_STICK=nodes the two other stick nodes' outputs, armed when they first run; HOS_WATCH_STICK=getctl who asks for a controller, by caller; HOS_WATCH_STICK=ctl who reads the game
   controller's sticks. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if GAME_VERSION == 143
static uint64_t npad_ctl, merged8;
static int main_armed, nodes_armed;   /* (at most: the main controller, the parameters, two nodes: 4 breakpoints) */
static int watch_mode(void) {   /* 0 off, 1 main controller and nodes, 2 the Npad controller */
    static int m = -1;
    if (m < 0) {
        const char *e = getenv("HOS_WATCH_STICK");
        m = !e || !*e || *e == '0' || !strcmp(e, "cam") ? 0 : !strcmp(e, "npad") ? 2 : !strcmp(e, "nodes") ? 3
            : !strcmp(e, "getctl") ? 4 : !strcmp(e, "ctl") ? 5 : !strcmp(e, "rs") ? 6 : !strcmp(e, "getters") ? 7 : !strcmp(e, "uh") ? 8 : 1;
        /* (cam: totk_mouse_cam.c; nodes: the two stick nodes' outputs as soon as they run, nothing else) */
    }
    return m;
}
static void watch(uint64_t a, const char *why) {
#ifndef __EMSCRIPTEN__
    void hos_watch_arm(uint64_t guest, int len, const char *why);
    hos_watch_arm(a, 8, why);
#else
    (void)a, (void)why;
#endif
}

ENTRY_HOOK(0x0ab28348) {
    if (!watch_mode() || npad_ctl) return 0;
    const uint64_t ctl = c->x[0];
    if (G32(ctl + 0x178) != 0) return 0;   /* (the device record index: 0 = Npad No.1) */
    npad_ctl = ctl;
    hos_log("watch: Npad No.1 controller at %#llx", (unsigned long long)ctl);
    if (watch_mode() == 2) watch(ctl + 0x128, "Npad right stick");
    return 0;
}

/* the controller manager's update: which merged controller holds Npad No.1's alone */
int hos_watch_manager_update(Ctx *c) {   /* (ENTRY_HOOK(0x08479c20) in totk_mouse_cam.c calls it first) */
    if (watch_mode() != 1 || !npad_ctl || merged8) return 0;
    uint64_t hos_guest_size(void);
    const uint64_t mgr = c->x[0], size = hos_guest_size();
    const int n = (int)G32(mgr + 0x178);
    const uint64_t list = G64(mgr + 0x180);
    if (n <= 0 || n > 64 || list < 0x8000000 || list + 8ull * n > size) return 0;
    for (int i = 0; i < n && !merged8; i++) {
        const uint64_t ctl = G64(list + 8ull * i);
        if (ctl < 0x8000000 || ctl + 0x190 > size) continue;
        const uint64_t kl = G64(ctl + 0x188);
        if (G32(ctl + 0x180) == 1 && kl >= 0x8000000 && kl + 8 <= size && G64(kl) == npad_ctl) {
            merged8 = ctl;
            hos_log("watch: controller %d of %d (%#llx) merges Npad No.1's alone", i, n, (unsigned long long)ctl);
        }
    }
    if (!merged8) merged8 = 1;   /* (looked once) */
    return 0;
}

typedef struct { uint64_t lr; volatile LONG n, shown; } Caller;
static Caller getctl[128], copies[128], getters[256];   /* get-controller's callers; the controller copy's; the
                                                         player input properties' getters' (key: getter << 56 | caller) */
static volatile LONG ngetctl, ncopies, ngetters;
static void census_n(Caller *t, volatile LONG *count, LONG cap, uint64_t lr, const char *what, uint64_t arg) {
    LONG n = *count;
    for (LONG i = 0; i < n; i++)
        if (t[i].lr == lr) {
            InterlockedIncrement(&t[i].n);
            return;
        }
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    n = *count;
    LONG i = 0;
    while (i < n && t[i].lr != lr) i++;
    if (i == n && n < cap) {
        t[n].lr = lr;
        InterlockedExchange(count, n + 1);
        hos_log("watch: %s called from %#llx (%#llx)", what, (unsigned long long)lr, (unsigned long long)arg);
    }
    if (i < cap) InterlockedIncrement(&t[i].n);
    ReleaseSRWLockExclusive(&lock);
}
static void census(Caller *t, volatile LONG *count, uint64_t lr, const char *what, uint64_t arg) {
    census_n(t, count, 128, lr, what, arg);
}
static int summarize(char *line, int len, int cap, Caller *t, LONG count) {
    int any = 0;
    for (LONG i = 0; i < count && len < cap - 60; i++) {
        const LONG n = t[i].n, d = n - t[i].shown;
        t[i].shown = n;
        if (d && t[i].lr >> 56)
            any = 1, len += snprintf(line + len, cap - len, " %02x@%#llx +%ld", (unsigned)(t[i].lr >> 56),
                                     (unsigned long long)(t[i].lr & 0xFFFFFFFFFFFFull), (long)d);
        else if (d) any = 1, len += snprintf(line + len, cap - len, " %#llx +%ld", (unsigned long long)t[i].lr, (long)d);
    }
    return any ? len : -len;
}

/* a controller copying another (x0 <- x1): the game's main controller, the copy of that merged controller */
ENTRY_HOOK(0x0837d330) {
    static int done;
    if (watch_mode() == 4) census(copies, &ncopies, c->x[30], "controller copy", c->x[0]);   /* (getctl: below) */
    if (watch_mode() != 1 || merged8 < 2 || done || c->x[1] != merged8) return 0;
    done = 1;
    hos_log("watch: main controller %#llx copies %#llx (lr %#llx)", (unsigned long long)c->x[0],
            (unsigned long long)merged8, (unsigned long long)c->x[30]);
    watch(c->x[0] + 0x128, "main controller right stick");
    main_armed = 1;
    return 0;
}

/* the player camera's parameters, as they are read from the resource: StickExponent and the yaw / pitch ratio */
ENTRY_HOOK(0x09ac7484) {
    static int done;
    if (watch_mode() != 1 || done) return 0;
    done = 1;
    hos_log("watch: player camera parameters at %#llx (lr %#llx)", (unsigned long long)c->x[0],
            (unsigned long long)c->x[30]);
    watch(c->x[0] + 0x180, "camera StickExponent / Lat2LngRatio");
    return 0;
}

/* two of the nodes copying a controller's sticks into their output: the first output of each */
static void node(Ctx *c, int which, const char *name) {
    static int done[3];
    static volatile LONG calls[3];
    if (watch_mode() == 3) {   /* (when each starts running: the one that runs only while Ultrahand holds) */
        const long n = InterlockedIncrement(&calls[which]);
        if (n == 1 || n == 100 || n == 1000 || n % 10000 == 0)
            hos_log("watch: %s called %ld times (output %#llx, lr %#llx)", name, n, (unsigned long long)c->x[0] + 0x20,
                    (unsigned long long)c->x[30]);
    }
    if (watch_mode() == 3 && !done[which] && nodes_armed < 2) {
        done[which] = 1;
        hos_log("watch: %s output at %#llx (lr %#llx)", name, (unsigned long long)c->x[0] + 0x20,
                (unsigned long long)c->x[30]);
        watch(c->x[0] + 0x20, name);
        nodes_armed++;
        return;
    }
    if (watch_mode() != 1 || done[which] || !main_armed || nodes_armed >= 2) return;
    done[which] = 1;
    hos_log("watch: %s output at %#llx (lr %#llx)", name, (unsigned long long)c->x[0] + 0x20,
            (unsigned long long)c->x[30]);
    watch(c->x[0] + 0x20, name);
    nodes_armed++;
}
ENTRY_HOOK(0x0a005070) { node(c, 1, "node f_0a005070 right stick"); return 0; }
ENTRY_HOOK(0x0a616bf4) { node(c, 2, "node f_0a616bf4 right stick"); return 0; }
/* HOS_WATCH_STICK=getctl: who asks for a controller (f_0837ccc0, the game's get-controller(manager, index, name):
   every stick reader starts there), by caller, with how many times in each 5 s that saw a change. The callers that
   show up only while Ultrahand holds something are the holding camera's way to the stick. */
static DWORD WINAPI getctl_summary(void *p) {
    (void)p;
    for (int tick = 1;; tick++) {
        Sleep(5000);
        char line[3000];
        int len = snprintf(line, sizeof line, "watch: get-controller callers at %d s:", tick * 5);
        int r = summarize(line, len, (int)sizeof line, getctl, ngetctl);
        if (r > 0) hos_log("%s", line);
        len = snprintf(line, sizeof line, "watch: controller copy callers at %d s:", tick * 5);
        r = summarize(line, len, (int)sizeof line, copies, ncopies);
        if (r > 0) hos_log("%s", line);
        len = snprintf(line, sizeof line, "watch: input property getters (getter@caller) at %d s:", tick * 5);
        r = summarize(line, len, (int)sizeof line, getters, ngetters);
        if (r > 0) hos_log("%s", line);
    }
    return 0;
}
ENTRY_HOOK(0x0837ccc0) {
    if (watch_mode() != 4) return 0;
    static volatile LONG started;
    if (!InterlockedExchange(&started, 1)) CreateThread(NULL, 0, getctl_summary, NULL, 0, NULL);
    census(getctl, &ngetctl, c->x[30], "get-controller", c->x[1]);
    return 0;
}
/* HOS_WATCH_STICK=ctl: the game controller f_0900d030 (the camera input's stick node) gets with get-controller: its
   sticks at +288 / +296, which f_0900d030 copies into the camera input's raw stick. Who else reads them (the holding
   camera's way to the stick: neither the camera input nor the other stick nodes run while Ultrahand holds). */
SPLICE_AFTER(0x0900d030, "c->x[30] = 0x900d07cULL; f_0837ccc0(c);") {
    extern volatile uint64_t hos_game_controller;   /* totk_mouse_cam.c: the stick boost writes its right stick */
    hos_game_controller = c->x[0];
    static uint64_t at;   /* (made again on loading a save: the breakpoints follow it) */
    static long calls;
    if (watch_mode() != 5 || !c->x[0]) return;
    /* (and every 1000 calls they are put on the threads again: a breakpoint only reaches threads that existed when
       it was set, and loading a save may make the workers again) */
    if (c->x[0] == at && ++calls % 1000) return;
    if (c->x[0] == at) {
#ifndef __EMSCRIPTEN__
        void hos_watch_move(const char *why, uint64_t guest);
        hos_watch_move("game controller +288 (a stick)", at + 288);
#endif
        return;
    }
    hos_log("watch: the camera stick node's game controller at %#llx", (unsigned long long)c->x[0]);
#ifndef __EMSCRIPTEN__
    void hos_watch_move(const char *why, uint64_t guest);
    if (at) {
        hos_watch_move("game controller +288 (a stick)", c->x[0] + 288);
        hos_watch_move("game controller +296 (a stick)", c->x[0] + 296);
        at = c->x[0];
        return;
    }
#endif
    at = c->x[0];
    watch(c->x[0] + 288, "game controller +288 (a stick)");
    watch(c->x[0] + 296, "game controller +296 (a stick)");
}
/* HOS_WATCH_STICK=rs: f_08f24ab0, the stick node that reads the game controller's right stick (+296) and publishes
   it as AI properties: the right stick as a vector (*(this+216)), its length (*(this+152)) and its angle
   (*(this+136)). The holding camera's logic reads one of them (nothing else reads the controller's right stick). */
ENTRY_HOOK(0x08f24ab0) {
    if (watch_mode() != 6) return 0;
    static uint64_t at[3];
    static const int offs[3] = {216, 152, 136};
    static const char *names[3] = {"stick node: right stick vector", "stick node: right stick length",
                                   "stick node: right stick angle"};
    for (int i = 0; i < 3; i++) {
        const uint64_t p = G64(c->x[0] + offs[i]);
        if (!p || p == at[i]) continue;
        if (!at[i]) {
            hos_log("watch: %s property at %#llx (node %#llx)", names[i], (unsigned long long)p, (unsigned long long)c->x[0]);
            watch(p, names[i]);
        } else {
#ifndef __EMSCRIPTEN__
            void hos_watch_move(const char *why, uint64_t guest);
            hos_watch_move(names[i], p);
#endif
        }
        at[i] = p;
    }
    static long calls;
    if (++calls % 1000 == 0) {   /* (onto new threads too) */
#ifndef __EMSCRIPTEN__
        void hos_watch_move(const char *why, uint64_t guest);
        if (at[0]) hos_watch_move(names[0], at[0]);
#endif
    }
    return 0;
}
/* HOS_WATCH_STICK=getters: the player input object's property getters (what the game's logic reads the stick node's
   outputs through: right stick vector, length, angle and the rest), counted by getter and caller every 5 s. Software
   counting: every thread, whatever reads them. The callers that show up only while Ultrahand holds something lead to
   the holding camera. Getter ids: the low byte of its address (0x09bcc2XX -> XX), the offset it reads in brackets. */
static void getter_hit(Ctx *c, unsigned id) {
    if (watch_mode() != 7) return;
    static volatile LONG started;
    if (!InterlockedExchange(&started, 1)) CreateThread(NULL, 0, getctl_summary, NULL, 0, NULL);
    census_n(getters, &ngetters, 256, (uint64_t)id << 56 | (c->x[30] & 0xFFFFFFFFFFFFull), "input property getter", id);
}
ENTRY_HOOK(0x09bcc260) { getter_hit(c, 0x60); return 0; }   /* float [152] */
ENTRY_HOOK(0x09bcc26c) { getter_hit(c, 0x6c); return 0; }   /* float [168] compared */
ENTRY_HOOK(0x09bcc280) { getter_hit(c, 0x80); return 0; }   /* float [152] compared */
ENTRY_HOOK(0x09bcc29c) { getter_hit(c, 0x9c); return 0; }   /* vec2 [264] */
ENTRY_HOOK(0x09bcc2b0) { getter_hit(c, 0xb0); return 0; }   /* float [120] */
ENTRY_HOOK(0x09bcc2bc) { getter_hit(c, 0xbc); return 0; }   /* float [104] */
ENTRY_HOOK(0x09bcc2c8) { getter_hit(c, 0xc8); return 0; }   /* float [136] */
ENTRY_HOOK(0x09bcc2ec) { getter_hit(c, 0xec); return 0; }   /* float [184] compared */
ENTRY_HOOK(0x09bcc300) { getter_hit(c, 0x00); return 0; }   /* float [216] compared */
ENTRY_HOOK(0x09bcc31c) { getter_hit(c, 0x1c); return 0; }   /* vec2 [280] */

/* HOS_WATCH_STICK=uh: Ultrahand's own copy of the right stick. f_0a415844 (Ultrahand's update) has f_0a41648c read
   the stick node's right stick vector (getter 0x1c: the only reader of the controller's right stick that runs just
   while Ultrahand is out, besides Link's animation) and the left one, apply the Camera X / Y-axis Inverted options,
   and store them in Ultrahand's context (x1 +284: right stick, +292: left); f_0a416910 then eases its own copy of
   the right stick toward it (this +320 / +324) for Link's arm. Whoever reads those while something is held is the
   holding camera's way to the stick. */
static void uh_watch(uint64_t a, const char *why, uint64_t *at, long *calls) {
    if (!a) return;
    if (!*at) {
        hos_log("watch: %s at %#llx", why, (unsigned long long)a);
        watch(a, why);
    } else if (a != *at || ++*calls % 1000 == 0) {   /* (moved, or onto new threads now and then) */
#ifndef __EMSCRIPTEN__
        void hos_watch_move(const char *why, uint64_t guest);
        hos_watch_move(why, a);
#endif
    }
    *at = a;
}
ENTRY_HOOK(0x0a41648c) {
    extern volatile uint64_t hos_uh_ctx;   /* totk_mouse_cam.c: Ultrahand holding's mouse */
    hos_uh_ctx = c->x[1];
    if (watch_mode() != 8) return 0;
    static uint64_t at;
    static long calls;
    uh_watch(c->x[1] + 284, "Ultrahand context: right stick", &at, &calls);
    return 0;
}
ENTRY_HOOK(0x0a416910) {
    if (watch_mode() != 8) return 0;
    static uint64_t at;
    static long calls;
    uh_watch(c->x[0] + 320, "Ultrahand: eased right stick", &at, &calls);
    return 0;
}
#endif
