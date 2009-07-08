/*****************************************************************************
 * tcpssl.c
 *
 * TCP SSL connection generator.
 *
 * ---------------------------------------------------------------------------
 * dosis - DoS: Internet Sodomizer
 *   (C) 2008-2009 Gerardo García Peña <gerardo@kung-foo.dhs.org>
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

#define DEFAULT_CWAIT           3000000
#define DEFAULT_RWAIT           10000000

typedef struct _tag_TCPSSL_CFG {
  /* options */
  INET_ADDR          dhost;

  /* parameters */
  double             hitratio;
  char              *payload;
  unsigned           payload_size;

  /* other things */
  pthreadex_timer_t  timer;
  struct timeval     sockwait_cwait;
  struct timeval     sockwait_rwait;
  struct sockaddr    dsockaddr;
} TCPSSL_CFG;

#define BUFSIZE 4096
static char nullbuff[BUFSIZE];

/*****************************************************************************
 * THREAD IMPLEMENTATION
 *****************************************************************************/

#define ip_protocol(x) (((struct iphdr *) (x))->protocol)
#define ip_header(x)   ((struct iphdr *)  (x))
#define tcp_header(x)  ((struct tcphdr *) ((x) \
                       + (((struct iphdr *) (x))->ihl << 2)))

/*****************************************************************************
 * $Id: hashkssl.c,v 1.20 2007-05-28 17:01:51 gerardo Exp $
 *
 * SSL Handshake attack.
 *
 * ---------------------------------------------------------------------------
 * calltoarms - HTTP/S DoS Giver
 *   Coded Gerardo García Peña <gerardo@kung-foo.dhs.org>
 *****************************************************************************/

#include <config.h>
#include "calltoarms.h"
#include "hashkssl.h"
#include "output.h"
#include "ttimer.h"

