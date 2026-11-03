# RRXD Lite component configuration

Modified 2026-10-31. Original implementation and copyright notices retained, GPLv2. config.inc uses isolated E:/RRXD-Lite boot targets, E:/RRXD-Lite-Setup recovery target and isolated runtime-config paths. Virtual DVD, ShadowC and actual-C remapping are disabled for staged runtime testing. ernie.asm now targets E:/RRXD-Lite/nkpatcher.xbe. font_stub.asm and font_profiles.json provide an editable assembly/parameter representation that reproduces all eight supplied kernel-specific fonts byte for byte. No real EEPROM writes are enabled.

This is NOT an approved permanent install configuration. There is no activation engine yet. It needs compatible signed-loader/companion-file validation, a complete rollback procedure and physical cold-boot testing; editing a metadata flag cannot supply those missing pieces.

Upstream: https://github.com/Rocky5/Xbox-Softmodding-Tool/tree/master/App%20Sources/NKPatcher/Main%20NKP11
