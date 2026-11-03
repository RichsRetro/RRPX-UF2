/* SPDX-License-Identifier: MIT. Exercise the exact callback shipped in SDK. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef ETH_PAD_SIZE
#define ETH_PAD_SIZE 0
#endif
#define NVNET_RX_BUFF_LEN 1600
#define PBUF_RAW 0
#define PBUF_RAM 0
#define ERR_OK 0
typedef int err_t;
struct pbuf {unsigned char* payload;uint16_t length;};
struct netif {err_t(*input)(struct pbuf*,struct netif*);};
static unsigned char dma[1616],expected[1616];
static int release_count,alloc_count,free_count,input_count;
static int fail_alloc,fail_copy,reject_input;
static struct pbuf* accepted;
static struct pbuf* pbuf_alloc(int layer,uint16_t length,int kind){
 assert(layer==PBUF_RAW&&kind==PBUF_RAM);++alloc_count;
 if(fail_alloc)return NULL;
 struct pbuf*p=malloc(sizeof(*p));assert(p);p->payload=malloc(length?length:1);assert(p->payload);p->length=length;return p;
}
static err_t pbuf_take(struct pbuf*p,const void*src,uint16_t length){
 assert(release_count==0&&length==p->length);
 if(fail_copy)return -1;
 memcpy(p->payload,src,length);return ERR_OK;
}
static void pbuf_free(struct pbuf*p){++free_count;free(p->payload);free(p);}
static void nvnetdrv_rx_release(void*buffer){
 assert(buffer==dma+ETH_PAD_SIZE);assert(release_count++==0);
 // Simulate immediate NIC reuse. Input must retain the original copied bytes.
 memset(dma,0xa5,sizeof(dma));
}
static err_t receive(struct pbuf*p,struct netif*n){
 (void)n;assert(release_count==1);++input_count;
 assert(!memcmp(p->payload,expected,p->length));
 if(reject_input)return -1;
 accepted=p;return ERR_OK;
}
static struct netif interface={receive};
static struct netif*g_pnetif=&interface;
#include "third_party/network/rx_copy.c.inc"
static void reset(void){
 release_count=alloc_count=free_count=input_count=0;
 fail_alloc=fail_copy=reject_input=0;accepted=NULL;
 for(unsigned i=0;i<sizeof(dma);++i)expected[i]=dma[i]=(unsigned char)(i*7+13);
}
int main(void){
 for(unsigned i=0;i<10000;++i){reset();rx_callback(dma+ETH_PAD_SIZE,1514);assert(release_count==1&&input_count==1&&accepted);pbuf_free(accepted);assert(alloc_count==free_count);}
 reset();fail_alloc=1;rx_callback(dma+ETH_PAD_SIZE,64);assert(release_count==1&&!input_count&&!free_count);
 reset();fail_copy=1;rx_callback(dma+ETH_PAD_SIZE,64);assert(release_count==1&&!input_count&&free_count==1);
 reset();reject_input=1;rx_callback(dma+ETH_PAD_SIZE,64);assert(release_count==1&&input_count==1&&free_count==1);
 reset();rx_callback(dma+ETH_PAD_SIZE,NVNET_RX_BUFF_LEN-ETH_PAD_SIZE+1);assert(release_count==1&&!alloc_count&&!input_count);
 printf("PASS RX ownership with padding%d: immediate DMA reuse, 10000 packets, allocation/copy/queue failures and bounds\n",ETH_PAD_SIZE);
}
