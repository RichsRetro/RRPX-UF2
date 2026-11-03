#pragma once
/* SPDX-License-Identifier: MIT */
#ifdef __cplusplus
extern "C" {
#endif
int ftp_run(const char *host_root, unsigned short port);
void ftp_prepare(void);
void ftp_stop(void);
int ftp_transfer_count(void);
int ftp_client_count(void);
int ftp_is_listening(void);

#ifdef __cplusplus
}
#endif
