# Building Retail v0.7.14

The source directory is the matching dashboard/Pico/component source for the tested UF2. Use Linux/WSL for nxdk. Dependencies are build-time only: users of the released UF2 do not need downloads or extra installer assets.

Validated dependencies: nxdk commit `f8c52e4be65bab7d6e10455db8d86d69d232bbe3`; Pico SDK commit `bddd20f928ce76142793bef434d4f75f4af6e433` (2.1.1); ARM GNU 14.2.1; LLVM 18; Python 3; CMake/Ninja; NASM 0.98.38 for the legacy assembly. Initialise SDK submodules. Keep original SDK and prepared SDK in separate sibling directories.

```sh
# From source/, with paths adapted to your SDK/tool installations:
export RRXD_NASM=/absolute/path/to/nasm
python3 tools/build_boot_components.py
python3 tools/prepare_network_sdk.py /absolute/path/to/nxdk /absolute/path/to/nxdk-ftp-copyrx
export NXDK_DIR=/absolute/path/to/nxdk-ftp-copyrx
export PATH="$NXDK_DIR/bin:$PATH"
make -C Dashboard -j4 NXDK_DIR="$NXDK_DIR"
python3 tools/check_ftp_abi.py Dashboard/main.obj Dashboard/Sources/ftpServer.obj
python3 tools/sign_dashboard.py
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake -S pico -B build-pico -G Ninja -DPICO_BOARD=pico -DPICO_NO_PICOTOOL=1
cmake --build build-pico -j4
python3 pico/tools/make_uf2.py build-pico/pico_endgame.bin RRPX-UF2-Retail-v0.7.14.uf2
```

Use a clean native build when switching SDK or source trees. Never reuse foreign `.obj` or absolute `.d` files. The prepared network SDK has owned-copy receive buffers and compiled capacity/mode guards required by the dashboard.

Local checks: `python3 pico/tools/test_card.py`; `python3 pico/tools/test_firmware.py build-pico/pico_endgame.elf build-pico/generated/card.bin` (requires Unicorn and pyelftools). The production UploadWriter host test generator is `tests/test_upload_writer.py`; compile its generated C++ harness with a host C++17 compiler, threads and ASAN/UBSAN.

GPL component: `tools/build_boot_components.py` assembles the supplied NKPatcher source and configuration, signs its standalone XBE and reproduces the included font profiles. No Microsoft loader is distributed. All required component source/configuration is in `third_party/nkpatcher/`.
