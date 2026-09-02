/* ============================================================================
 *  PSP-OS  —  a custom shell for the PSP-1000 (ARK-4 CFW)
 *  v1.0  "Liberty"
 *
 *  A boot-to-your-own-UI homebrew shell. Built with the pspdev SDK.
 *  Runs as an EBOOT.PBP in ms0:/PSP/GAME/PSP-OS/ — launch it from
 *  Game -> Memory Stick, exactly like any other homebrew.
 *
 *  This is v1: the frame, the app registry, and the first real modules.
 *    [OK ] System Monitor  — live battery/clock + 222/333 MHz toggle (real)
 *    [OK ] IR Blaster      — sends a TV power code over the IR emitter (real)
 *    [ .. ] Wi-Fi Recon     — scaffolded (v1.1 — needs the sceWlan scan port)
 *    [ .. ] Serial Bench    — scaffolded (v1.1 — needs the 2.5V remote cable)
 *    [OK ] About            — the machine, the plan, the credits
 *
 *  Design note: v1 uses pspDebugScreen (the SDK text framebuffer) for the
 *  UI. It is bomb-proof and boots on every PSP — the right call for a first
 *  release you can trust on real hardware. The GU/2D pass is a v1.1 job.
 * ==========================================================================*/

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <psppower.h>
#ifndef NO_IR
#include <pspsircs.h>          /* IR (sceSircs). Build with NO_IR=1 to omit — see Makefile. */
#endif
#include <stdio.h>
#include <string.h>

/* ---- module header: identifies this EBOOT to the PSP loader ------------- */
PSP_MODULE_INFO("PSP-OS", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* ---- 0xAABBGGRR palette (PSP-OS violet identity, matches the handbook) -- */
#define C_BG      0xFF120A08u   /* near-black ground              */
#define C_INK     0xFFEDE7F3u   /* primary text                  */
#define C_DIM     0xFF8A7F96u   /* secondary text                */
#define C_VIOLET  0xFFC9346Au   /* accent  (#6A34C9)             */
#define C_WHITE   0xFFFFFFFFu
#define C_GREEN   0xFF579B4Fu   /* good (#4F9B57-ish)            */
#define C_AMBER   0xFF0F74B0u   /* warn (#B0740F)               */
#define C_RED     0xFF343BBEu   /* bad  (#BE3B34)               */

#define COLS 60                 /* safe character width we draw within       */

/* ---- edge-triggered input ---------------------------------------------- */
static SceCtrlData g_pad;
static unsigned int g_prev = 0;
static unsigned int g_pressed = 0;

static void input_poll(void) {
    sceCtrlReadBufferPositive(&g_pad, 1);
    g_pressed = g_pad.Buttons & ~g_prev;   /* buttons newly down this frame  */
    g_prev = g_pad.Buttons;
}

/* ---- tiny draw helpers over pspDebugScreen ----------------------------- */
static void at(int x, int y) { pspDebugScreenSetXY(x, y); }
static void ink(u32 fg, u32 bg) {
    pspDebugScreenSetTextColor(fg);
    pspDebugScreenSetBackColor(bg);
}
static void bar(int y, u32 bg) {         /* full-width colored row          */
    int i; ink(C_WHITE, bg); at(0, y);
    for (i = 0; i < COLS; i++) pspDebugScreenPrintf(" ");
}

/* header + footer chrome shared by every screen -------------------------- */
static void chrome(const char *title) {
    pspDebugScreenSetBackColor(C_BG);
    pspDebugScreenClear();
    bar(0, C_VIOLET);
    ink(C_WHITE, C_VIOLET); at(1, 0);  pspDebugScreenPrintf("PSP-OS  v1.0");
    ink(C_WHITE, C_VIOLET); at(46, 0); pspDebugScreenPrintf("PSP-1000 / 01g");
    ink(C_VIOLET, C_BG);    at(1, 2);  pspDebugScreenPrintf(">> %s", title);
    bar(33, C_VIOLET);
}
static void footer(const char *hint) {
    ink(C_WHITE, C_VIOLET); at(1, 33); pspDebugScreenPrintf("%s", hint);
}

/* ============================ MODULES ==================================== */

/* --- 1. System Monitor: live readout + real 222/333 MHz toggle ---------- */
static void screen_sysinfo(void) {
    int want333 = (scePowerGetCpuClockFrequency() >= 300);
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;

        /* SQUARE toggles the clock — a genuinely useful, user-mode-safe op */
        if (g_pressed & PSP_CTRL_SQUARE) {
            want333 = !want333;
            if (want333) scePowerSetClockFrequency(333, 333, 166);
            else         scePowerSetClockFrequency(222, 222, 111);
        }

        int pct  = scePowerGetBatteryLifePercent();
        int cpu  = scePowerGetCpuClockFrequency();
        int bus  = scePowerGetBusClockFrequency();
        int chg  = scePowerIsBatteryCharging();
        int ac   = scePowerIsPowerOnline();
        int mins = scePowerGetBatteryLifeTime();

        chrome("SYSTEM MONITOR");
        int y = 4;
        ink(C_DIM, C_BG); at(3, y);   pspDebugScreenPrintf("CPU");
        ink(C_INK, C_BG); at(20, y++);pspDebugScreenPrintf("2x Allegrex MIPS R4000");
        ink(C_DIM, C_BG); at(3, y);   pspDebugScreenPrintf("Clock");
        ink(cpu>=300?C_GREEN:C_INK,C_BG); at(20,y++);
                                      pspDebugScreenPrintf("%d MHz  (bus %d MHz)", cpu, bus);
        ink(C_DIM, C_BG); at(3, y);   pspDebugScreenPrintf("RAM");
        ink(C_INK, C_BG); at(20, y++);pspDebugScreenPrintf("32 MB DDR  +4 MB eDRAM");
        ink(C_DIM, C_BG); at(3, y);   pspDebugScreenPrintf("Firmware");
        ink(C_INK, C_BG); at(20, y++);pspDebugScreenPrintf("6.60 ARK-4 cIPL");
        y++;
        ink(C_DIM, C_BG); at(3, y);   pspDebugScreenPrintf("Battery");
        if (pct >= 0) { ink(pct<20?C_RED:C_GREEN,C_BG); at(20,y++);
                        pspDebugScreenPrintf("%d%%  %s", pct,
                          ac?(chg?"(charging)":"(AC power)"):"(on battery)"); }
        else { ink(C_DIM,C_BG); at(20,y++); pspDebugScreenPrintf("(reading...)"); }
        if (mins > 0) { ink(C_DIM,C_BG); at(3,y); pspDebugScreenPrintf("Est. life");
                        ink(C_INK,C_BG); at(20,y++);
                        pspDebugScreenPrintf("%d h %02d min", mins/60, mins%60); }

        y += 2;
        ink(C_VIOLET, C_BG); at(3, y++);
        pspDebugScreenPrintf("Performance : %s", want333 ? "333 MHz  [MAX]" : "222 MHz  [eco]");
        ink(C_DIM, C_BG); at(3, y++);
        pspDebugScreenPrintf("Press [] (Square) to toggle. 333 = smoother N64/PS1.");

        footer(" []=toggle clock    O=back ");
        sceDisplayWaitVblankStart();
    }
}

#ifndef NO_IR
/* --- 2. IR Blaster: real IR-emitter output via sceSircs ----------------- */
/* Sends a Sony SIRC "TV power" (device 1, command 21). Many Sony TVs react. */
/* Fully guarded: if the IR module can't init, it reports instead of hanging.*/
static void ir_send_power(int *out_rc, int *out_sent) {
    struct SircsData sd;
    int i, rc = 0, sent = 0;
    sd.type = 0;          /* SIRC 12-bit                         */
    sd.count = 1;
    sd.command = 21;      /* TV power toggle                     */
    sd.device = 1;        /* TV                                  */
    for (i = 0; i < 8; i++) {          /* repeat — IR remotes send bursts    */
        rc = sceSircsSend(&sd, 1);
        if (rc == 0) sent++;
        sceKernelDelayThread(60 * 1000);   /* ~60 ms between frames          */
    }
    *out_rc = rc; *out_sent = sent;
}

static void screen_ir(void) {
    int fired = 0, rc = 0, sent = 0;
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;
        if (g_pressed & PSP_CTRL_CROSS) { ir_send_power(&rc, &sent); fired = 1; }

        chrome("IR BLASTER");
        int y = 4;
        ink(C_INK, C_BG); at(3, y++); pspDebugScreenPrintf("Sony SIRC  ->  TV Power (device 1, cmd 21)");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("Point the top edge of the PSP at the TV, then fire.");
        y++;
        ink(C_VIOLET, C_BG); at(3, y++); pspDebugScreenPrintf("[ Press X to BLAST ]");
        y++;
        if (fired) {
            if (sent > 0) { ink(C_GREEN, C_BG); at(3, y++);
                            pspDebugScreenPrintf("Sent %d/8 IR frames. If the TV blinked -- it works!", sent); }
            else { ink(C_AMBER, C_BG); at(3, y++);
                   pspDebugScreenPrintf("IR returned rc=%d. See note below.", rc); }
        }
        y += 2;
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("This is the Phase-1 proof: your code driving real");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("PSP hardware through a Sony driver. IR is TX-only.");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("v1.1: full SIRC/NEC/RC5 code database + macros.");

        footer(" X=blast    O=back ");
        sceDisplayWaitVblankStart();
    }
}
#endif /* NO_IR */

/* --- 3 & 4. Scaffolded modules (honest placeholders) -------------------- */
static void screen_info(const char *title, const char *l1, const char *l2,
                        const char *l3, const char *l4) {
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;
        chrome(title);
        int y = 5;
        ink(C_AMBER, C_BG);  at(3, y++); pspDebugScreenPrintf("STATUS: scaffolded for v1.1");
        y++;
        ink(C_INK, C_BG);    at(3, y++); pspDebugScreenPrintf("%s", l1);
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("%s", l2);
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("%s", l3);
        y++;
        ink(C_VIOLET, C_BG); at(3, y++); pspDebugScreenPrintf("%s", l4);
        footer(" O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* --- 5. About ----------------------------------------------------------- */
static void screen_about(void) {
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;
        chrome("ABOUT");
        int y = 4;
        ink(C_VIOLET, C_BG); at(3, y++); pspDebugScreenPrintf("PSP-OS  v1.0  \"Liberty\"");
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("A custom shell for the PSP-1000, built on ARK-4.");
        y++;
        ink(C_INK, C_BG);    at(3, y++); pspDebugScreenPrintf("The plan: one handheld that is a full computer +");
        ink(C_INK, C_BG);    at(3, y++); pspDebugScreenPrintf("games + an IR / Wi-Fi / serial field console.");
        y++;
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("Stack : your shell -> ARK-4 -> Sony fw -> Allegrex");
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("Build : pspdev / psp-gcc  ->  EBOOT.PBP");
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("Roadmap: Wi-Fi recon, serial bench, GU 2D shell,");
        ink(C_DIM, C_BG);    at(3, y++); pspDebugScreenPrintf("         then replace the XMB as the launcher.");
        y++;
        ink(C_GREEN, C_BG);  at(3, y++); pspDebugScreenPrintf("Made for coolesrat's rig. Free hydro, zero excuses.");
        footer(" O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* ============================ MENU ====================================== */
typedef struct { const char *name; const char *desc; } Item;
static Item MENU[] = {
    { "System Monitor", "live battery + clock, 222/333 MHz toggle" },
    { "IR Blaster",     "fire a TV power code out the IR emitter"  },
    { "Wi-Fi Recon",    "802.11b AP scanner  (v1.1)"               },
    { "Serial Bench",   "UART4 hardware console (v1.1)"            },
    { "About",          "the machine, the plan, the credits"      },
};
#define N_ITEMS ((int)(sizeof(MENU)/sizeof(MENU[0])))

static void run_item(int i) {
    switch (i) {
        case 0: screen_sysinfo(); break;
        case 1:
#ifndef NO_IR
                screen_ir();
#else
                screen_info("IR BLASTER",
                    "Built without IR (NO_IR=1).",
                    "Rebuild without NO_IR and with -lpspsircs to enable",
                    "the real IR emitter output.",
                    "See the Makefile note.");
#endif
                break;
        case 2: screen_info("WI-FI RECON",
                    "Hardware: 802.11b, 2.4 GHz, WPA/WPA2.",
                    "Planned: AP survey (SSID/BSSID/ch/RSSI) -> CSV,",
                    "signal mapping, handshake capture (AirCrack-PSP port).",
                    "The heaviest module -- lands after the cable + shell."); break;
        case 3: screen_info("SERIAL BENCH",
                    "Port: UART4 on the remote connector, 2.5 V TTL.",
                    "WARNING: needs a level-shifted cable (2.5V, not 5V).",
                    "Planned: terminal + sensor logger for your MCU rigs.",
                    "Bridges the PSP to the KR-85 / XR15 / PT2399 benches."); break;
        case 4: screen_about(); break;
    }
}

static void screen_menu(void) {
    int sel = 0;
    for (;;) {
        input_poll();
        if (g_pressed & PSP_CTRL_UP)   sel = (sel - 1 + N_ITEMS) % N_ITEMS;
        if (g_pressed & PSP_CTRL_DOWN) sel = (sel + 1) % N_ITEMS;
        if (g_pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) run_item(sel);

        chrome("MAIN MENU");
        int i, y = 5;
        for (i = 0; i < N_ITEMS; i++) {
            if (i == sel) {
                bar(y, C_VIOLET);
                ink(C_WHITE, C_VIOLET); at(2, y);  pspDebugScreenPrintf(">");
                ink(C_WHITE, C_VIOLET); at(4, y);  pspDebugScreenPrintf("%s", MENU[i].name);
                ink(C_WHITE, C_VIOLET); at(24, y); pspDebugScreenPrintf("%s", MENU[i].desc);
            } else {
                ink(C_INK, C_BG); at(4, y);  pspDebugScreenPrintf("%s", MENU[i].name);
                ink(C_DIM, C_BG); at(24, y); pspDebugScreenPrintf("%s", MENU[i].desc);
            }
            y += 2;
        }
        ink(C_DIM, C_BG); at(3, 31); pspDebugScreenPrintf("A custom OS for a 2004 handheld. Because we can.");
        footer(" UP/DOWN=move    X=open    START=exit to XMB ");
        sceDisplayWaitVblankStart();

        if (g_pressed & PSP_CTRL_START) return;   /* clean exit to XMB       */
    }
}

/* ---- HOME-button exit callback boilerplate ----------------------------- */
static int exit_cb(int a1, int a2, void *c) { (void)a1;(void)a2;(void)c; sceKernelExitGame(); return 0; }
static int cb_thread(SceSize n, void *p) { (void)n;(void)p;
    int id = sceKernelCreateCallback("Exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, THREAD_ATTR_USER, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

/* ---- entry point ------------------------------------------------------- */
int main(void) {
    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    screen_menu();               /* run the shell until START               */

    sceKernelExitGame();         /* back to the XMB                         */
    return 0;
}
