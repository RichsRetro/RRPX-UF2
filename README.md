# RRPX-UF2

A reusable original Xbox service toolkit on a standard **2 MB Raspberry Pi Pico**. The Retail Edition launches ENDGAME and RRXD Lite, with temporary homebrew/DVD patches, FTP, EEPROM backup and recovery.

**No permanent softmod installation.** Cold boot without the Pico to return to the Microsoft dashboard. No HeXEn disc, additional installation files or compatible USB memory stick required. You need a suitable Pico-to-Xbox USB connection.

## Download

Get **RRPX-UF2-Retail-v0.7.15.uf2** from [Releases](https://github.com/RichsRetro/RRPX-UF2/releases/latest). The UF2 container is about 3 MB because of its block overhead; the firmware written to flash is **1,507,420 bytes**, within the Pico's 2 MB capacity.

## Start

1. Hold BOOTSEL while connecting the Pico to a PC.
2. Copy the UF2 to the RPI-RP2 drive and allow it to finish flashing.
3. Connect the Pico to the Xbox through a suitable Xbox USB adapter.
4. Open the retail dashboard's Memory section to trigger ENDGAME. Allow PicoX startup to finish.

RRXD Lite provides temporary session patches, homebrew and burned-disc launching, games/apps, EEPROM backup, hardware information and FTP. Games are discovered under **E:\Games** and **F:\Games**; existing custom paths are preserved.

For FTP, use the IP shown on screen, port **21**, username **xbox**, password **xbox**, plain FTP. Keep EEPROM backups separately on your PC.

## Recovery

Hold **START + BACK** as PicoX starts. Recovery runs independently of dashboard settings, fonts and artwork and provides FTP file access. Disconnect FTP clients before pressing **B** to restart, then launch PicoX normally again.

Session patches are temporary. This edition does not install a permanent boot chain, provide permanent-install rollback, clone HDDs or replace your retail system files.

## Tested

- FileZilla bulk upload: **2400 files**, approximately **8.5 MiB/s**, zero reported failures.
- Independent read-only verification: **all 2400 files / 3,466,109,578 bytes** matched PC originals by length and SHA256, with no missing or extra files.
- Interrupted transfers, abrupt reset, reconnect/resume, overwrite truncation and file-handle release.
- START + BACK recovery, recovery FTP readback/reconnect, B restart and normal dashboard relaunch.
- User-confirmed game launching, burned DVD, EEPROM backup, E/F game discovery, menu scrolling, boot animation and retail cold boot.
- Stock HDD and a 60 GB HDD setup.

These results cover the tested console/storage setups, not every Xbox revision, kernel, SATA bridge or network. Performance depends on storage, file sizes and network. Original dashboard artwork is reduced in RRXD Lite to fit the Pico.

## Source and credits

Source is in [source/](source/). Build instructions and component notices are included. This is a multi-licence distribution: Rich's original RRXD work, NevolutionX, ENDGAME, NKPatcher, nxdk, Pico SDK, TinyUSB, SDL and other components retain their respective licences. See [LICENSING.md](LICENSING.md). No Microsoft dashboard, retail BIOS or console EEPROM is included.

Thanks to the original authors and the Xbox homebrew community.

## Support

[Buy me a coffee](https://buymeacoffee.com/richsretro) — completely optional, always appreciated.
