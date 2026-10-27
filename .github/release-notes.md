**HP-1**: a physically modeled handpan firmware for the M-VAVE FM-1.

> **Beta.** HP-1 is built on HP-1's platform, which runs on the FM-1. Custom firmware is installed at
> your own risk, so have the way back ready first (below). Its package differs from FoMni's only in
> the app, checked byte by byte in this release's build.

### Files

| File | What it is |
|---|---|
| `hp1-VERSION.fwsc` | The firmware package, to install on the FM-1 |
| `HP-1.html` | The browser version in one file: open it from disk, no FM-1 needed |

Or play it online: <https://mcnasby.github.io/hp-1/>

### Installing

1. **Get the way back ready.** Download M-VAVE's stock firmware (FM-1 V15, "PC Firmware" at
   m-vave.com/download) and keep it. If HP-1 fails to start twice, the FM-1 comes up in safe mode
   (no sound, USB on), where you can reinstall. After four failed starts it enters the chip's own
   update mode, which the package can't overwrite, and `tools/fm1_rescue.py` puts stock firmware
   back. FoMni's web installer can also restore it.
2. **Install from the web page**: <https://mcnasby.github.io/hp-1/#install> (Chrome or Edge):
   connect the FM-1 by USB and press Install. Or **over USB from the command line**, from a checkout
   of this repository (`pip install -r requirements.txt`):
   ```
   python3 tools/fm1_install.py --info                   # finds the FM-1, writes nothing
   python3 tools/fm1_install.py hp1-VERSION.fwsc
   ```
   Or hold OCT− and OCT+ for 5 seconds for update mode, and use any Felucca-family installer.
3. The splash screen shows the version, and the Setup page (GLO) shows the audio load. If it's
   high, or if you hear dropouts, please open an issue.

The [README](https://github.com/mcnasby/hp-1#readme) explains every key, button and knob.
