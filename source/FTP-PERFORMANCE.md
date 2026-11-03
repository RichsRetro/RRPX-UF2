# v0.7.14 throughput candidate

Restores main RRXD two-buffer UploadWriter overlap while retaining v0.7.13 owned-copy RX, socket guards and all UI/session changes. Two 256 KiB buffers per uploading connection, with single-buffer synchronous fallback on allocation failure. Pending writes are drained before buffers are reused or files closed.

v0.7.13 remains the working baseline. Its two simultaneous 16 MiB test uploads took 6.796 seconds overall (about 4.71 MiB/s), verified by full readback. User reports approximately 6 MiB/s in FileZilla on stock IDE storage. These measurements do not isolate disk and network costs.

This candidate has NOT been hardware validated. Compare one large file and two parallel files on the same HDD, verify full readback, then test interrupted upload/resume and FileZilla. A throughput gain is not established until measured; a stall means revert to v0.7.13. No permanent installation or C: boot changes.
