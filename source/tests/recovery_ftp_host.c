/* SPDX-License-Identifier: MIT. Native test harness; not shipped on the Xbox. */
#include "recoveryFtp.h"
#include <stdlib.h>
#include <signal.h>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    signal(SIGPIPE,SIG_IGN);
    return ftp_run(argv[1],(unsigned short)atoi(argv[2]));
}
