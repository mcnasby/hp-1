# HP-1 licensing

HP-1 is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`,
full text in `LICENSE`). It is built on FiMba-1 (and through it FoMni, X0X and Felucca), and keeps
their license. If you distribute HP-1, or firmware derived from it, you must give your recipients its
complete corresponding source under the same license.

## Where the code comes from

| What | Origin | License |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `firmware/crt0.S`, `firmware/app.ld`, `tools/` (build, package, install, rescue, font), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x) and [FoMni](https://github.com/charlesvestal/fm1-fomni) (USB names changed here) | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `firmware/src/main.c`, `host/hp1_host.c`, the frame of `firmware/src/app/ui.c` (bands, knobs, autosave) and `project.c` | adapted from FoMni (itself from X0X and Felucca) | GPL-3.0-only |
| `firmware/src/dsp/fastmath.h`; the plate reverb in `firmware/src/dsp/handpan.c` (Dattorro's design) | FoMni / X0X | GPL-3.0-only |
| Platform tests (`tests/host/{fastmath,encoder,uac,trs}_test.c`, `tests/{storage,ota,ldr}_test.c`, `tests/install_test.py`) | FoMni / Felucca | GPL-3.0-only |
| The simulated FM-1 for the web installer's tests (`tests/web/fake_fm1.mjs`) and the installer panel's protocol use (`web/fm1ota.js`, `web/fm1pkg.js`, inlined into the page) | Felucca's `web/test_web.mjs` and update modules, by way of FoMni's web installer | GPL-3.0-only |
| The modal voice, the effects (chorus, filter, tape, lo-fi), the grain cloud, the delay, the command queue's use, the browser page's frame (`web/emu/`), the scenario harness, and the frame of the tests | [FiMba-1](https://github.com/jadamsowers/fm1-fimba) (itself from FoMni) | GPL-3.0-only |
| Everything else: the tone fields, the shell, the cavity, the percussion, sympathy, the handpan scales and the second pan, the grooves (`firmware/src/dsp/handpan.{c,h}`), the UI's instrument and pages (`firmware/src/app/ui.c`), `tests/host/handpan_test.c`, `tests/scenarios/` | HP-1 | GPL-3.0-only |

## Third-party material

| What | License | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are
trademarks of their respective owners, as are the handpan makers' scale names. HP-1 is independent firmware, not affiliated with, endorsed
by or supported by any of them.

## Radio

HP-1 never enables the Bluetooth / Wi-Fi radio of the hardware.
