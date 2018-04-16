#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "gsm_internal.h"
#include "libgsmcom.h"


 int gsm_sched_runq(sched_fifo_queue *schedq)
  {
	struct timeval tv;
	int numevents;
	int res;
    gsm_sched_t *sched = NULL;
    if(!schedq)
         return 0;
	gsm_mutex_lock(&schedq->lock);
	for (numevents = 0; numevents < schedq->len; numevents++)
	{
		tv = gsm_tvadd(gsm_tvnow(), gsm_tv(0, 1000));
		if (schedq->sched[numevents].callback && (gsm_tvcmp(schedq->sched[numevents].when, tv) != -1))
		{
          sched = &schedq->sched[numevents];
          gsm_mutex_unlock(&schedq->lock);
          res = sched->callback(sched->data);
          gsm_mutex_lock(&schedq->lock);
          schedq->sched[numevents].callback = NULL;
          schedq->sched[numevents].data = NULL;
          break;
        }
	}
	gsm_mutex_unlock(&schedq->lock);
	return numevents;
 }


 int gsm_sched_add(sched_fifo_queue *schedq,int when, gsm_sched_callback callback, void *data)
 {
    int i = 0;
    if(!schedq)
          return 1;
    gsm_mutex_lock(&schedq->lock);
    for(i=0; i<schedq->len; i++)
    {
        if(!schedq->sched[i].callback)
            break;
    }
    if(i > schedq->len)
    {
         gsm_mutex_unlock(&schedq->lock);
         gsm_log(GSM_LOG_WARNING, " read from queue  and is full\n");
         return -1;
    }
    else
    {  
        schedq->sched[i].callback = callback;
        schedq->sched[i].data = data;
        schedq->sched[i].when = gsm_tv(0, 0);
        schedq->sched[i].id = i; 
        gsm_sched_settimer(&schedq->sched[i].when, when);
    }
    gsm_mutex_unlock(&schedq->lock);
    return i;
 }

 int gsm_sched_del(sched_fifo_queue *schedq, unsigned int id)
 {
     if(!schedq)
           return 1;
     if(id < 0 || id >= schedq->len)
           return 1;
	 gsm_mutex_lock(&schedq->lock);
     schedq->sched[id].callback = NULL;
     schedq->sched[id].data = NULL;
 	 gsm_mutex_unlock(&schedq->lock);
     return 0;
 }

 int gsm_sched_wait(sched_fifo_queue *schedq)
 {
    int ms;
    int i;
    if(!schedq)
          return -1;
 	gsm_mutex_lock(&schedq->lock);
    for(i = 0; i < schedq->len; i++)
    {
       if(schedq->sched[i].callback)
       {
	      ms = gsm_tvdiff_ms(schedq->sched[i].when, gsm_tvnow());
		  if (ms < 0)
			    ms = 0;
       }
	}
	gsm_mutex_unlock(&schedq->lock);
	return ms;
 }

 int gsm_sched_settimer( struct timeval *tv, int when)
 {
    struct timeval now = gsm_tvnow();
 	if (gsm_tvzero(*tv))
		*tv = now;
 	*tv = gsm_tvadd(*tv, gsm_samp2tv(when, 1000));
	if (gsm_tvcmp(*tv, now) > 0)
		*tv = now;
	return 0;
 }
