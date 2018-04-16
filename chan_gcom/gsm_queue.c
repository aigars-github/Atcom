#include <stdlib.h>
#include <stdio.h>
#include "gsm_queue.h"
#include "gsm_internal.h"
#include "libgsmcom.h"


 int write_to_event_queue(event_fifo_queue *queue,char *wr_buf)
 {
    if(!queue || !wr_buf)
          return 1;
    gsm_mutex_lock(&queue->lock);
    if ((queue->wr_idx+1)%queue->len != queue->rd_idx)    //Have new data.   
    {  
        memcpy(&queue->data[queue->wr_idx][0], wr_buf, sizeof(queue->data[queue->wr_idx]));  
        queue->wr_idx = (queue->wr_idx+1) % queue->len;    //Read out one byte.   
    }
    else
    {
        gsm_mutex_unlock(&queue->lock);
        gsm_log(GSM_LOG_WARNING, "read from queue  and is full\n");
        return 1;
    }
    gsm_mutex_unlock(&queue->lock);
    return 0;
 }

 int read_from_event_queue(event_fifo_queue *queue,char *rd_buf, int len)
 {
    if(!queue || !rd_buf)
    {
        gsm_log(GSM_LOG_WARNING, "read from queue and rdbuf is null\n");
        return 1;
    }
    gsm_mutex_lock(&queue->lock);
    if (queue->rd_idx!= queue->wr_idx)  
    {  
        memcpy(rd_buf, &queue->data[queue->rd_idx][0], len);  
        queue->rd_idx = (queue->rd_idx + 1) % queue->len;  

    }
    else
    {  
        gsm_mutex_unlock(&queue->lock);
        return 1;
    }
    gsm_mutex_unlock(&queue->lock);
    return 0;
 }
