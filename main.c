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
#include <pspiofilemgr.h>
#include <pspsysmem.h>
#include <pspwlan.h>
#include <pspusb.h>
#ifndef NO_IR
#include <pspsircs.h>          /* IR (sceSircs). Build with NO_IR=1 to omit — see Makefile. */
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
static unsigned int g_frame = 0;       /* animation clock, ticks every vblank */

static void input_poll(void) {
    sceCtrlReadBufferPositive(&g_pad, 1);
    g_pressed = g_pad.Buttons & ~g_prev;   /* buttons newly down this frame  */
    g_prev = g_pad.Buttons;
    g_frame++;
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

/* ---- "breathing" violet glow + shimmer text, driven off g_frame -------- */
static u32 glow_violet(void) {
    static const u32 steps[] = { 0xFFC9346Au, 0xFFCF4A78u, 0xFFD86088u, 0xFFCF4A78u };
    return steps[(g_frame / 8) % 4];
}
static const u32 WAVE[] = { 0xFFC9346Au, 0xFFD86088u, 0xFFE788A6u, 0xFFFFFFFFu, 0xFFE788A6u, 0xFFD86088u };
#define N_WAVE ((int)(sizeof(WAVE)/sizeof(WAVE[0])))
static void print_wave(int x, int y, const char *s, int phase) {
    int i; at(x, y);
    for (i = 0; s[i]; i++) {
        pspDebugScreenSetTextColor(WAVE[(i + phase + g_frame / 4) % N_WAVE]);
        pspDebugScreenSetBackColor(C_BG);
        at(x + i, y);
        pspDebugScreenPrintf("%c", s[i]);
    }
}
static char spin_glyph(void) {
    static const char s[] = { '|', '/', '-', '\\' };
    return s[(g_frame / 4) % 4];
}

/* header + footer chrome shared by every screen -------------------------- */
static void chrome(const char *title) {
    char line[COLS];
    pspDebugScreenSetBackColor(C_BG);
    pspDebugScreenClear();
    bar(0, glow_violet());
    ink(C_WHITE, glow_violet()); at(1, 0);  pspDebugScreenPrintf("PSP-OS  v1.0");
    ink(C_WHITE, glow_violet()); at(41, 0); pspDebugScreenPrintf("PSP-1000/01g [%c]", spin_glyph());
    snprintf(line, sizeof(line), ">> %s", title);
    print_wave(1, 2, line, 0);
    bar(33, glow_violet());
}
static void footer(const char *hint) {
    ink(C_WHITE, glow_violet()); at(1, 33); pspDebugScreenPrintf("%s", hint);
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
        ink(C_INK, C_BG); at(20, y++);pspDebugScreenPrintf("32 MB DDR  +4 MB eDRAM  (%d KB free)",
                                      sceKernelTotalFreeMemSize() / 1024);
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
    struct sircs_data sd;
    int i, rc = 0, sent = 0;
    sd.type = 12;          /* SIRC 12-bit                         */
    sd.cmd  = 21;           /* TV power toggle                     */
    sd.dev  = 1;            /* TV                                  */
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

/* --- 5. Crypto Lab: real on-device SHA-256 compression benchmark -------- */
/* Raw SHA-256 core (public-domain-style, single 64-byte block per call).   */
/* Used to genuinely measure this PSP's hash throughput -- no network, no  */
/* pool, no wallet. Pure "how fast is the Allegrex at this" experiment.    */
#define RR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static const u32 SHA256_H0[8] = {
    0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
    0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
};
static const u32 SHA256_K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static void sha256_block(u32 h[8], const u8 *p) {
    u32 w[64], a,b,c,d,e,f,g,hh,t1,t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|((u32)p[i*4+3]);
    for (i = 16; i < 64; i++) {
        u32 s0 = RR32(w[i-15],7) ^ RR32(w[i-15],18) ^ (w[i-15]>>3);
        u32 s1 = RR32(w[i-2],17) ^ RR32(w[i-2],19) ^ (w[i-2]>>10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];hh=h[7];
    for (i = 0; i < 64; i++) {
        u32 S1 = RR32(e,6) ^ RR32(e,11) ^ RR32(e,25);
        u32 ch = (e & f) ^ (~e & g);
        t1 = hh + S1 + ch + SHA256_K[i] + w[i];
        u32 S0 = RR32(a,2) ^ RR32(a,13) ^ RR32(a,22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}

static void screen_crypto(void) {
    int done = 0;
    double blocks_sec = 0.0, khs = 0.0, double_khs = 0.0;
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;

        if (g_pressed & PSP_CTRL_CROSS) {
            u8 block[64]; u32 h[8]; int i;
            for (i = 0; i < 64; i++) block[i] = (u8)(i * 37 + 11);
            memcpy(h, SHA256_H0, sizeof(h));

            SceInt64 t0 = sceKernelGetSystemTimeWide();
            unsigned long ops = 0;
            /* run for ~1.5s of wall-clock so short runs don't skew the rate */
            while (sceKernelGetSystemTimeWide() - t0 < 1500000) {
                sha256_block(h, block);
                ops++;
            }
            SceInt64 t1 = sceKernelGetSystemTimeWide();
            double secs = (double)(t1 - t0) / 1000000.0;
            blocks_sec  = (double)ops / secs;      /* 64-byte compressions/sec */
            khs         = blocks_sec / 1000.0;      /* single SHA-256, KH/s     */
            double_khs  = khs / 2.0;                /* SHA-256d (Bitcoin-style) */
            done = 1;
        }

        chrome("CRYPTO LAB");
        int y = 4;
        ink(C_INK, C_BG); at(3, y++); pspDebugScreenPrintf("Real on-device SHA-256 compression benchmark.");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("No network, no pool, no wallet -- pure hardware test.");
        y++;
        ink(C_VIOLET, C_BG); at(3, y++); pspDebugScreenPrintf("[ Press X to BENCHMARK  (~1.5s) ]");
        y++;
        if (done) {
            ink(C_GREEN, C_BG); at(3, y++); pspDebugScreenPrintf("SHA-256      : %.1f KH/s", khs);
            ink(C_GREEN, C_BG); at(3, y++); pspDebugScreenPrintf("SHA-256d     : %.1f KH/s  (Bitcoin-style double hash)", double_khs);
            ink(C_DIM, C_BG);   at(3, y++); pspDebugScreenPrintf("Raw blocks/s : %.0f", blocks_sec);
            y++;
            ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("For context: a modern ASIC does ~10^9x this rate.");
            ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("This is a hash-speed lab, not a miner -- by design.");
        } else {
            ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("(no result yet)");
        }
        footer(" X=run    O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* --- 6. CPU / RAM Bench: real timed integer + memory throughput tests --- */
static void screen_bench(void) {
    int done = 0;
    double int_mops = 0.0, memset_mbs = 0.0, memcpy_mbs = 0.0;
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;

        if (g_pressed & PSP_CTRL_CROSS) {
            /* integer throughput: tight add/xor loop, ops/sec */
            {
                volatile unsigned long acc = 0;
                unsigned long i, iters = 20000000UL;
                SceInt64 t0 = sceKernelGetSystemTimeWide();
                for (i = 0; i < iters; i++) acc += (i ^ (acc << 1)) + 1;
                SceInt64 t1 = sceKernelGetSystemTimeWide();
                double secs = (double)(t1 - t0) / 1000000.0;
                int_mops = (iters / secs) / 1000000.0;
            }
            /* memory throughput: memset + memcpy over a 1 MB buffer */
            {
                const int SZ = 1 * 1024 * 1024;
                char *a = (char *)malloc(SZ);
                char *b = (char *)malloc(SZ);
                if (a && b) {
                    SceInt64 t0 = sceKernelGetSystemTimeWide();
                    memset(a, 0xAA, SZ);
                    SceInt64 t1 = sceKernelGetSystemTimeWide();
                    memcpy(b, a, SZ);
                    SceInt64 t2 = sceKernelGetSystemTimeWide();
                    double s1 = (double)(t1 - t0) / 1000000.0;
                    double s2 = (double)(t2 - t1) / 1000000.0;
                    memset_mbs = (SZ / 1048576.0) / s1;
                    memcpy_mbs = (SZ / 1048576.0) / s2;
                }
                if (a) free(a);
                if (b) free(b);
            }
            done = 1;
        }

        chrome("CPU / RAM BENCH");
        int y = 4;
        ink(C_INK, C_BG); at(3, y++); pspDebugScreenPrintf("Real timed loops on this PSP's CPU and RAM.");
        y++;
        ink(C_VIOLET, C_BG); at(3, y++); pspDebugScreenPrintf("[ Press X to RUN ]");
        y++;
        if (done) {
            ink(C_GREEN, C_BG); at(3, y++); pspDebugScreenPrintf("Integer     : %.1f M ops/s", int_mops);
            ink(C_GREEN, C_BG); at(3, y++); pspDebugScreenPrintf("memset      : %.1f MB/s", memset_mbs);
            ink(C_GREEN, C_BG); at(3, y++); pspDebugScreenPrintf("memcpy      : %.1f MB/s", memcpy_mbs);
        } else {
            ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("(no result yet)");
        }
        footer(" X=run    O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* --- 7. Hex / File Viewer: real Memory Stick file browser + hex dump ---- */
#define MAX_ENTRIES 64
static char g_names[MAX_ENTRIES][256];
static int  g_is_dir[MAX_ENTRIES];
static int  g_n_entries = 0;

static void scan_dir(const char *path) {
    g_n_entries = 0;
    int dfd = sceIoDopen(path);
    if (dfd < 0) return;
    SceIoDirent d;
    memset(&d, 0, sizeof(d));
    while (g_n_entries < MAX_ENTRIES && sceIoDread(dfd, &d) > 0) {
        strncpy(g_names[g_n_entries], d.d_name, 255);
        g_names[g_n_entries][255] = '\0';
        g_is_dir[g_n_entries] = FIO_S_ISDIR(d.d_stat.st_mode);
        g_n_entries++;
        memset(&d, 0, sizeof(d));
    }
    sceIoDclose(dfd);
}

static void hexdump_file(const char *name) {
    char path[300];
    snprintf(path, sizeof(path), "./%s", name);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    long off = 0;
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) break;
        if (g_pressed & PSP_CTRL_RTRIGGER) off += 256;
        if (off < 0) off = 0;

        chrome("HEX VIEWER");
        ink(C_DIM, C_BG); at(3, 3); pspDebugScreenPrintf("%s  @ offset %ld", name, off);

        u8 buf[256];
        int got = 0;
        if (fd >= 0) {
            sceIoLseek(fd, off, PSP_SEEK_SET);
            got = sceIoRead(fd, buf, sizeof(buf));
        }
        int y = 5, i, r;
        if (fd < 0) { ink(C_RED, C_BG); at(3, y++); pspDebugScreenPrintf("Could not open file."); }
        for (r = 0; r < got / 16 + ((got % 16) ? 1 : 0) && r < 16; r++) {
            ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("%04lX", off + r*16);
            ink(C_INK, C_BG); at(10, y);
            for (i = 0; i < 16; i++) {
                int idx = r*16 + i;
                if (idx < got) pspDebugScreenPrintf("%02X ", buf[idx]);
                else pspDebugScreenPrintf("   ");
            }
            ink(C_VIOLET, C_BG); at(59-16, y);
            for (i = 0; i < 16; i++) {
                int idx = r*16 + i;
                char c = (idx < got) ? buf[idx] : ' ';
                pspDebugScreenPrintf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            y++;
        }
        footer(" R=next 256B    O=back ");
        sceDisplayWaitVblankStart();
        if (got == 0 && off > 0) off -= 256; /* don't run past EOF forever */
    }
    if (fd >= 0) sceIoClose(fd);
}

static void screen_files(void) {
    int sel = 0;
    scan_dir(".");
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;
        if (g_n_entries > 0) {
            if (g_pressed & PSP_CTRL_UP)   sel = (sel - 1 + g_n_entries) % g_n_entries;
            if (g_pressed & PSP_CTRL_DOWN) sel = (sel + 1) % g_n_entries;
            if ((g_pressed & PSP_CTRL_CROSS) && !g_is_dir[sel]) hexdump_file(g_names[sel]);
        }

        chrome("HEX / FILE VIEWER");
        ink(C_DIM, C_BG); at(3, 3); pspDebugScreenPrintf("ms0:/PSP/GAME/PSP-OS/   (%d entries)", g_n_entries);
        int y = 5, i;
        for (i = 0; i < g_n_entries && y < 31; i++) {
            if (i == sel) { bar(y, C_VIOLET); ink(C_WHITE, C_VIOLET); at(3, y); }
            else          { ink(g_is_dir[i] ? C_VIOLET : C_INK, C_BG); at(3, y); }
            pspDebugScreenPrintf("%s%s", g_names[i], g_is_dir[i] ? "/" : "");
            y++;
        }
        footer(" UP/DOWN=move    X=hexdump    O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* --- 8. Wi-Fi Info: real hardware query via sceWlan (switch/power/MAC) -- */
/* Note: this reads the actual 802.11b radio state. Active AP scanning needs */
/* the net/apctl stack (sceNet*) and is a bigger, separate module -- v1.2.  */
static void screen_wifi(void) {
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;

        int sw  = sceWlanGetSwitchState();
        int pow = sceWlanDevIsPowerOn();
        u8 mac[8]; memset(mac, 0, sizeof(mac));
        int rc = sceWlanGetEtherAddr(mac);

        chrome("WI-FI");
        int y = 4;
        ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("Hardware switch");
        ink(sw ? C_GREEN : C_RED, C_BG); at(22, y++); pspDebugScreenPrintf("%s", sw ? "ON" : "OFF");
        ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("Radio power");
        ink(pow ? C_GREEN : C_DIM, C_BG); at(22, y++); pspDebugScreenPrintf("%s", pow ? "ON" : "off");
        ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("MAC address");
        if (rc == 0) { ink(C_INK, C_BG); at(22, y++);
            pspDebugScreenPrintf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]); }
        else { ink(C_DIM, C_BG); at(22, y++); pspDebugScreenPrintf("(unavailable -- flip the Wi-Fi switch on)"); }
        y += 2;
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("This is the real 802.11b radio, read live off the");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("hardware. AP scanning / signal survey needs the net");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("stack (sceNet/apctl) -- planned as a separate v1.2");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("module rather than bolted on here.");

        footer(" O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* --- 9. USB Info: real cable/activation state via sceUsbGetState -------- */
static void screen_usb(void) {
    for (;;) {
        input_poll();
        if (g_pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_LTRIGGER)) return;

        int st = sceUsbGetState();
        int cable = (st & PSP_USB_CABLE_CONNECTED) != 0;
        int actd  = (st & PSP_USB_ACTIVATED) != 0;
        int conn  = (st & PSP_USB_CONNECTION_ESTABLISHED) != 0;

        chrome("USB");
        int y = 4;
        ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("Cable connected");
        ink(cable ? C_GREEN : C_DIM, C_BG); at(24, y++); pspDebugScreenPrintf("%s", cable ? "YES" : "no");
        ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("Driver activated");
        ink(actd ? C_GREEN : C_DIM, C_BG); at(24, y++); pspDebugScreenPrintf("%s", actd ? "YES" : "no");
        ink(C_DIM, C_BG); at(3, y); pspDebugScreenPrintf("Host connection");
        ink(conn ? C_GREEN : C_DIM, C_BG); at(24, y++); pspDebugScreenPrintf("%s", conn ? "ESTABLISHED" : "none");
        y += 2;
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("Live read of the real USB bus driver state. Use");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("XMB's own USB Connection mode for mass-storage mode --");
        ink(C_DIM, C_BG); at(3, y++); pspDebugScreenPrintf("this screen is a diagnostic, not a mode switcher.");

        footer(" O=back ");
        sceDisplayWaitVblankStart();
    }
}

/* --- 10. About ----------------------------------------------------------- */
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
    { "Crypto Lab",     "real on-device SHA-256 hash benchmark"    },
    { "CPU / RAM Bench","real integer + memory throughput test"    },
    { "Hex / File View","browse + hex-dump files on the stick"     },
    { "Wi-Fi Info",     "real radio switch/power/MAC readout"      },
    { "USB Info",       "real cable/activation state readout"      },
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
        case 2: screen_crypto(); break;
        case 3: screen_bench(); break;
        case 4: screen_files(); break;
        case 5: screen_wifi(); break;
        case 6: screen_usb(); break;
        case 7: screen_info("SERIAL BENCH",
                    "Port: UART4 on the remote connector, 2.5 V TTL.",
                    "WARNING: needs a level-shifted cable (2.5V, not 5V).",
                    "Planned: terminal + sensor logger for your MCU rigs.",
                    "Bridges the PSP to the KR-85 / XR15 / PT2399 benches."); break;
        case 8: screen_about(); break;
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
                u32 glow = glow_violet();
                bar(y, glow);
                ink(C_WHITE, glow); at(2, y);  pspDebugScreenPrintf("%c", (g_frame/8)%2 ? '>' : '*');
                ink(C_WHITE, glow); at(4, y);  pspDebugScreenPrintf("%s", MENU[i].name);
                ink(C_WHITE, glow); at(24, y); pspDebugScreenPrintf("%s", MENU[i].desc);
            } else {
                ink(C_INK, C_BG); at(4, y);  pspDebugScreenPrintf("%s", MENU[i].name);
                ink(C_DIM, C_BG); at(24, y); pspDebugScreenPrintf("%s", MENU[i].desc);
            }
            y += 2;
        }
        {
            static const char *TAG = "  A custom OS for a 2004 handheld. Because we can.  ::  PSP-OS LIBERTY  ::  ";
            int len = (int)strlen(TAG);
            int off = (g_frame / 6) % len;
            char row[COLS + 1]; int k;
            for (k = 0; k < COLS - 6; k++) row[k] = TAG[(off + k) % len];
            row[k] = '\0';
            ink(C_DIM, C_BG); at(3, 31); pspDebugScreenPrintf("%s", row);
        }
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

/* ---- boot splash: shimmering logo + animated progress bar -------------- */
static void screen_boot(void) {
    static const char *LOGO1 = "======================================================";
    static const char *LOGO2 = "   P   S   P   -   O   S       ::  LIBERTY  ::";
    static const char *LOGO3 = "======================================================";
    int t;
    for (t = 0; t < 210; t++) {
        input_poll();
        if (g_pressed) break;                  /* any button -- skip the intro */

        pspDebugScreenSetBackColor(C_BG);
        pspDebugScreenClear();
        bar(0, glow_violet());
        print_wave(1, 0, "PSP-OS BOOTLOADER", 0);
        bar(33, glow_violet());

        print_wave(2, 10, LOGO1, 0);
        print_wave(2, 12, LOGO2, 3);
        print_wave(2, 14, LOGO3, 0);

        ink(C_DIM, C_BG); at(3, 17); pspDebugScreenPrintf("LIBERTY SHELL  --  built for a 2004 handheld");

        {   /* animated loading bar, 0..100% over the intro */
            int pct = (t * 100) / 200; if (pct > 100) pct = 100;
            int width = 45, fill = (pct * width) / 100, i;
            ink(C_DIM, C_BG); at(3, 20); pspDebugScreenPrintf("[");
            ink(glow_violet(), C_BG); at(4, 20);
            for (i = 0; i < width; i++) pspDebugScreenPrintf("%c", i < fill ? '#' : '.');
            ink(C_DIM, C_BG); at(4 + width, 20); pspDebugScreenPrintf("] %3d%%", pct);
        }

        ink(C_DIM, C_BG); at(3, 23); pspDebugScreenPrintf("Waking Allegrex ... Wi-Fi radio ... USB bus ... IR emitter ...");
        if ((g_frame / 20) % 2 == 0) {
            ink(C_VIOLET, C_BG); at(3, 26); pspDebugScreenPrintf("PRESS ANY BUTTON TO CONTINUE");
        }

        sceDisplayWaitVblankStart();
    }
}

/* ---- entry point ------------------------------------------------------- */
int main(void) {
    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    screen_boot();                /* cinematic-ish intro, skip with any button */
    screen_menu();               /* run the shell until START               */

    sceKernelExitGame();         /* back to the XMB                         */
    return 0;
}
