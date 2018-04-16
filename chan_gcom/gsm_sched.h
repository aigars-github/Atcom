#ifndef _ATCOM_SCHED_H
#define _ATCOM_SCHED_H

#include <sys/time.h>
#include <pthread.h>
#include "gsm_internal.h"
#include "gsm_queue.h"
#include "libgsmcom.h"

#define GSM_SCHED_ARGS (void *data)

typedef int (*gsm_sched_callback) GSM_SCHED_ARGS;
#define GSM_SCHEDULED_CB(name) int  (name) (void *data)

 typedef struct gsm_sched
 {
    struct timeval when;
    unsigned int id;
    gsm_sched_callback callback;
    void *data;
 }gsm_sched_t;

 typedef struct
 {
   gsm_sched_t sched[GSM_MAX_SCHED_QUEUE_LEN] ;
   int len;
   pthread_mutex_t lock;
  }sched_fifo_queue;

 void gsm_sched_queue(sched_fifo_queue *schedq,  struct gsm_sched *s);
 int gsm_sched_runq(sched_fifo_queue *schedq);
 int gsm_sched_add(sched_fifo_queue *schedq, int when, gsm_sched_callback callback, void *data);
 int gsm_sched_del(sched_fifo_queue *schedq, unsigned int id);
 int gsm_sched_wait(sched_fifo_queue *schedq);
 int gsm_sched_settimer( struct timeval *tv, int when);


#endif /* _ATCOM_SCHED_H */
