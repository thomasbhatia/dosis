/*****************************************************************************
 * tea.c
 *
 * Is now the tea time?
 *
 * ---------------------------------------------------------------------------
 * dosis - DoS: Internet Sodomizer
 *   (C) 2008 Gerardo Garc�a Pe�a <gerardo@kung-foo.dhs.org>
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "dosconfig.h"
#include "dosis.h"
#include "log.h"
#include "pthreadex.h"
#include "tea.h"

static THREAD_WORK **ttable;

static void tea_timer_fini(void)
{
  int i;

  if(ttable)
  {
    for(i = 0; i < cfg.maxthreads; i++)
      if(ttable[i])
        free(ttable[i]);
    free(ttable);
  }
}

void tea_timer_init(void)
{
  if(atexit(tea_timer_fini))
    D_FAT("Cannot set finalization routine.");

  if((ttable = calloc(cfg.maxthreads, sizeof(THREAD_WORK *))) == NULL)
    D_FAT("Cannot allocate memory for managing %d threads.", cfg.maxthreads);
}

static TEA_MSG_QUEUE *tea_timer_mqueue_create(void)
{
  TEA_MSG_QUEUE *mq;

  if((mq = calloc(1, sizeof(TEA_MSG_QUEUE))) == NULL)
    D_FAT("[%02d] No memory for a tea message queue.", tw->id);
  pthreadex_mutex_init(&(mq->mutex));
  pthreadex_flag_init(&(tin[tid].mwaiting), 0);

  return mq;
}

static void tea_timer_mqueue_destroy(TEA_MSG_QUEUE *mq)
{
  TEA_MSG *m;

  /* empty queue */
  while((m = tea_timer_msg_get()) != NULL)
    tea_timer_msg_release(m);

  /* free queue */
  free(mq);
}

TEA_MSG *tea_timer_mqueue_get(TEA_MSG_QUEUE *mq)
{
  TEA_MSG *m;

  pthreadex_mutex_begin(&(mq->mutex));
  if(mq->last)
  {
    m = mq->last;
    mq->last = m->prev;
    if(mq->first == m)
      mq->first = NULL;
  }
  pthreadex_mutex_end()
}

static void tea_timer_basic_thread_cleanup(THREAD_WORK *tw)
{
  TEA_MSG_QUEUE *mq;

  tw->methods->cleanup(tw);

  /* disassociate mqueue of tw */
  pthreadex_mutex_begin(&(tw->mqueue->mutex));
  if(tw->mqueue)
    mq = tw->mqueue;
  else
    mq = NULL;
  tw->mqueue = NULL;
  pthreadex_mutex_end()

  /* destroy mqueue */
  if(mq)
    tea_timer_mqueue_destroy(mq);
  pthread_flag_destroy(tw->mwaiting);
}

static void tea_timer_basic_thread(THREAD_WORK *tw)
{
  int r;

  /* initialize thread */
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &r);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &r);
  pthread_cleanup_push((void *) tea_timer_basic_thread_cleanup, &tw);

  /* launch thread */
  if(tw->methods->listen)
  {
    while(!tw->finalize)
    {
      pthreadex_flag_wait(&(tw->mwaiting));
      tw->methods->listen(tw);
    }
  } else
    tw->methods->thread(tw);

  /* finish him */
  pthread_cleanup_pop(1);
  pthread_exit(NULL);
}

void tea_timer_new_thread(int tid, TEA_OBJECT *to)
{
  int i;
  struct timeval sttime, entime;
  pthreadex_barrier_t start_barrier;
  THREAD_WORK *tin;
  unsigned int mneeded, tneeded;

  /* build threads */
  DBG("Alloc'ing thread %d.", tid);
  if(ttable[tid])
    D_FAT("Cannot alloc thread %d because it is already used.", tid);

  tin[tid].id         = i;
  tin[tid].pthread_id = 0;
  tin[tid].to         = to;

  /* check methods */
  if(tw->methods->listen)
  {
    tw->mqueue = tea_timer_mqueue_create();
  } else {
    tw->mqueue = NULL;
  }

  /* configure thread here */
  XXXXXXX

  /* launch thread */
  if(pthread_create(&(tin[tid].pthread_id),
                    NULL,
                    (void *) attack_thread,
                    tin+tid) != 0)
    FAT("Error creating thread %d: %s", tid, strerror(errno));

  if(cfg->finalize)
    WRN("Attack cancelled by user.");

  x
  /* cancel all threads */
  /* NOTE: Only cancelations with 'errno' different from zero are real    */
  /*       errors. A pthread_cancel return value different from zero, but */
  /*       a zero errno only means that thread is already finished.       */
  LOG("[--] Cancelling all threads.");
  for(i = 0; i < cfg->c; i++)
    if(pthread_cancel(tin[i].pthread_id) && errno != 0)
      ERR("[--] Cannot cancel thread %02u: %s", i, strerror(errno));

  DBG("[--] Waiting for all to join.");
  for(i = 0; i < cfg->c; i++)
    if(pthread_join(tin[i].pthread_id, NULL))
      ERR("[--] Cannot join with thread %02u: %s", i, strerror(errno));

  /* free memory */
  DBG2("[--]   Free memory.");
  pthreadex_timer_destroy(&timer);
  pthreadex_barrier_destroy(&start_barrier);
  free(tin);
}

