# RRPX-UF2 Retail v0.7.14

First public Retail Edition: temporary Xbox homebrew and service tools from a **single UF2 on a standard 2 MB Pico**.

- ENDGAME entry into RRXD Lite.
- Temporary homebrew and burned-DVD patches.
- FTP with receive/write overlap restored: approximately **8.5 MiB/s** on the tested setup.
- EEPROM backup, hardware information and independent recovery FTP.
- E:\Games and F:\Games discovery, scrolling menu, right-side status panel and animated early loading bar.
- Cold boot without Pico returns to the retail Microsoft dashboard. **No permanent installation.**

**Hardware checks:** 2400 game files / 3,466,109,578 bytes independently matched the PC originals by SHA256 with zero missing, extra or mismatched files. Disconnect/resume, abrupt reset, overwrite cleanup, START + BACK recovery, recovery FTP, restart and normal relaunch passed. Tested on the user's stock HDD and 60 GB setup; not a claim of exhaustive console/kernel/adapter compatibility.

## Download

Flash **RRPX-UF2-Retail-v0.7.14.uf2** to the Pico in BOOTSEL mode. The ZIP includes firmware, matching source, notices, instructions and test summaries. Source is also provided separately. UF2 container overhead makes the download about 3 MB; actual flash use is **1,538,076 bytes**.

No HeXEn disc, compatible memory stick or additional installation files required. A suitable Xbox-to-Pico USB connection is required. See the repository README for startup, FTP and recovery instructions.

## Credits and licences

Thanks to the authors of ENDGAME, NevolutionX, NKPatcher, nxdk, Pico SDK, TinyUSB, SDL and the other included libraries. This is a multi-licence release; original licences, notices and GPL component source/configuration are included. No Microsoft dashboard/BIOS or console EEPROM is bundled.

## Support

[Buy me a coffee](https://buymeacoffee.com/richsretro) — completely optional, always appreciated.
