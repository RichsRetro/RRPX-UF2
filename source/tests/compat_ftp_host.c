#include "recoveryFtp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
static const char*root;static unsigned short port;
static void*run(void*p){(void)p;ftp_run(root,port);return NULL;}
int main(int argc,char**argv){if(argc!=3)return 2;signal(SIGPIPE,SIG_IGN);root=argv[1];port=(unsigned short)atoi(argv[2]);
 ftp_prepare();ftp_stop();if(ftp_run(root,port))return 3;
 for(int round=0;round<2;++round){pthread_t thread;ftp_prepare();pthread_create(&thread,NULL,run,NULL);puts("READY");fflush(stdout);getchar();ftp_stop();pthread_join(thread,NULL);if(ftp_client_count()||ftp_transfer_count())return 4;puts("STOPPED");fflush(stdout);}return 0;}
