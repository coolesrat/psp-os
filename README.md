# PSP-OS v1.0 "Liberty"

A custom boot-to-your-own-UI **shell for the PSP-1000**, running on ARK-4 CFW.
This is the real source of your OS — it compiles to an `EBOOT.PBP` you drop into
`ms0:/PSP/GAME/PSP-OS/` and launch from **Game → Memory Stick**, like any homebrew.

Built for a stock-to-unlocked PSP-1000 (`6.60 ARK-4 cIPL`, model 01g).

---

## What v1 does

| Module | Status | What it is |
|---|---|---|
| **System Monitor** | ✅ real | Live battery / clock / temp, **and a working 222↔333 MHz toggle** (press □). 333 MHz = smoother N64/PS1. |
| **IR Blaster** | ✅ real | Fires a Sony **TV-power** code out the IR emitter (press X). The Phase-1 proof: your code driving real hardware. |
| **Wi-Fi Recon** | 🚧 v1.1 | Scaffolded. AP scanner → CSV, signal map, handshake capture. |
| **Serial Bench** | 🚧 v1.1 | Scaffolded. UART4 terminal for your MCU rigs (needs the 2.5 V cable). |
| **About** | ✅ | The machine, the plan, the credits. |

**Controls:** D-pad Up/Down = move · **X** = open · **O** = back · **□** = toggle clock (in System Monitor) · **X** = blast (in IR) · **START** = exit to XMB · **HOME** = quit.

---

## How to turn this into `EBOOT.PBP`

Pick whichever is easiest for you. **A** needs nothing installed.

### A) GitHub Actions — zero tools on your PC (recommended)
1. Make a new GitHub repo and upload these files (keep the `.github/workflows/` folder).
2. Open the repo's **Actions** tab → the **build-psp-os** run starts on its own.
3. When it's green, open the run → **Artifacts** → download **PSP-OS-EBOOT** → inside is `EBOOT.PBP`.

### B) Docker one-liner (if you have Docker Desktop)
From this folder:
```powershell
# Windows PowerShell
docker run --rm -v ${PWD}:/src -w /src pspdev/pspdev:latest make
```
```bash
# macOS / Linux
docker run --rm -v "$PWD":/src -w /src pspdev/pspdev:latest make
```

### C) Native toolchain
Install **pspdev** (https://github.com/pspdev/pspdev), then just:
```bash
make
```

All three produce **`EBOOT.PBP`** in this folder.

---

## Install it on the PSP
1. On the Memory Stick, make a folder: `PSP/GAME/PSP-OS/`
2. Copy `EBOOT.PBP` into it → `ms0:/PSP/GAME/PSP-OS/EBOOT.PBP`
3. On the PSP: **Game → Memory Stick → PSP-OS** → **X**.

That's it — you're booting into your own OS.

---

## Notes
- **IR is the one feature to test** (it's the only part that touches a Sony driver in an
  iffy way). It's fully guarded — if it can't send, it just reports `rc=...`, it won't hang.
  If your toolchain ever errors on the IR library, build without it: `make NO_IR=1`
  (everything else still works).
- v1 uses the SDK text framebuffer (`pspDebugScreen`) on purpose — it's bomb-proof and boots
  on every PSP. The pretty GU/2D shell is a v1.1 job.
- IR sends **Sony SIRC TV-power** by default. Non-Sony TVs may not respond — the full
  multi-protocol code database lands in v1.1.

## Roadmap
v1.1: GU 2D UI · Wi-Fi AP scanner · UART4 serial terminal · IR code database + macros
→ then the big one: register PSP-OS as the **launcher that replaces the XMB**.

---
*Made for coolesrat's rig. A full custom computer out of a 2004 handheld — because we can.*
