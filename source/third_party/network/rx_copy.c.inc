/* BSD-2-Clause: PicoX receive ownership adaptation of nxdk's BSD driver. */
void rx_callback (void *buffer, uint16_t length)
{
    /* Return the NIC descriptor promptly: TCP queues must own their bytes,
     * rather than holding the finite DMA ring across concurrent uploads. */
    if (length > NVNET_RX_BUFF_LEN - ETH_PAD_SIZE) {
        nvnetdrv_rx_release(buffer);
        return;
    }
    const uint16_t bytes = length + ETH_PAD_SIZE;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, bytes, PBUF_RAM);
    if (!p) {
        nvnetdrv_rx_release(buffer);
        return; /* TCP can retransmit; the hardware ring must keep receiving. */
    }
    const err_t copied = pbuf_take(p, (unsigned char *)buffer - ETH_PAD_SIZE, bytes);
    nvnetdrv_rx_release(buffer);
    if (copied != ERR_OK || g_pnetif->input(p, g_pnetif) != ERR_OK) {
        pbuf_free(p);
    }
}
