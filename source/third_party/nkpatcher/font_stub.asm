; GPLv2, derived from NKPatcher font profile data by rmenhal and contributors.
; Editable assembly representation added 2026-10-31 for reproducible font data.
; Original notices and COPYING apply. Profile pointer supplied by the builder.
BITS 32
%ifndef KERNEL_GADGET
%error KERNEL_GADGET is required
%endif
db 'XTF0'
dd 32
db 'xbox'
times 28 db 0
dd 2, 0, 0, 0
dw 2, 2
dd 0x1000, 0x11c054, KERNEL_GADGET
