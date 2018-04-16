#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "libgsmcom.h"
#include "gsm_internal.h"
#include "gsm_sched.h"
#include "gsm_queue.h"



/*is call for application*/
 int  gsm_call_set_state(struct gsm_span *span, gsm_call_state_t callstate)
 {
     int status = 0;
     int res = 0;
     if(!span)
     {
        gsm_log(GSM_LOG_WARNING,"gsm_call_set_state but  have no call\n");
        return 1;
     }
     span->calls.newstate = callstate;
     switch(callstate)
     {
          case GSM_CALL_STATE_DIALING://ring
          {
                if(span->calls.direction == GCOM_DIRECTION_INCOMING)
                     break;
                else
                {
                    char buf[512] = {0};
                    snprintf(buf, sizeof(buf), "%s%s;%s", "ATD", span->calls.data.called_num,GSM_CMD_END);
                    res = gsm_write_cmd(span, buf, strlen(buf));
                    span->call_progress = 1;
                }
          }
          break;
          case GSM_CALL_STATE_DIALED:
          {
                if(span->calls.direction == GCOM_DIRECTION_INCOMING)//recv clip
               {
                    status = GSM_CALL_STATE_DIALED;
                    struct gsm_call_data  event;
                    memset(&event, 0, sizeof(struct gsm_call_data));
                    //add num
                    memcpy(event.calling_num, span->calls.data.calling_num, sizeof(event.calling_num));
                    event.callid = span->calls.data.callid;//add other
                    event.sub = GSM_SUB_REAL;
                    gsm_call_ind( span->span_id, &event);
               }
          }
          break;
          case GSM_CALL_STATE_RINGING:
          {
                status = GSM_CALL_STATE_RINGING;

                gsm_call_report(span->span_id, span->calls.data.callid, status);
               
          }
          break;
          case GSM_CALL_STATE_ANSWERED://remote answer
          {
                gsm_log(GSM_LOG_DEBUG,"gsm_call_set_state span_%d->calls.newtate=GSM_CALL_STATE_ANSWERED\n",span->span_id);
                
                if(span->calls.direction  == GCOM_DIRECTION_INCOMING)
                {
                     char buf[100] = {0};
                     snprintf(buf,sizeof(buf),"%s%s","ATA",GSM_CMD_END);
                     res = gsm_write_cmd(span, buf, strlen(buf));
                     gsm_call_set_state(span, GSM_CALL_STATE_UP);
                }
                else 
                {
                    status = GSM_CALL_STATE_ANSWERED;
                    gsm_call_report(span->span_id, span->calls.data.callid, status);   
                    gsm_call_set_state(span, GSM_CALL_STATE_UP);
                    span->call_progress = 0;
                }
                         
          }
          break;
          case GSM_CALL_STATE_UP: 
          {

             gsm_log(GSM_LOG_DEBUG,"gsm_call_set_state span_%d->calls.newtate=GSM_CALL_STATE_UP\n",span->span_id);
             span->heat = 0;
             break;
          }
          case GSM_CALL_STATE_TERMINATING://remote hangup
          {
                gsm_log(GSM_LOG_DEBUG,"gsm_call_set_state span_%d->calls.newtate=GSM_CALL_STATE_TERMINATING\n",span->span_id);
                status = GSM_CALL_STATE_TERMINATING;
                gsm_call_hangup_ind(span->span_id, 0, 1);
                
          }
          break;
          case GSM_CALL_STATE_TERMINATING_CMPL:
          {
                span->event_call_state = 0;
                span->alarm = 0;
                memset(&span->calls, 0, sizeof(span->calls));
                gsm_log(GSM_LOG_DEBUG,"CLEANUP CALLS 1 span_%d->calls.newtate=%d\n",span->span_id,span->calls.newstate);
              
          }
          break;
          case GSM_CALL_STATE_HANGUP:
          {
                 gsm_log(GSM_LOG_DEBUG," GSM_CALL_STATE_HANGUP id=%d\n",span->calls.data.callid);
                 char buf[100] = {0};
                 snprintf(buf,sizeof(buf),"%s%s","ATH",GSM_CMD_END);
                 res = gsm_write_cmd(span, buf, strlen(buf));
                 gsm_call_set_state(span, GSM_CALL_STATE_HANGUP_CMPL);
          }
          break;
          case GSM_CALL_STATE_HANGUP_CMPL:
          {
                status = GSM_CALL_STATE_HANGUP_CMPL;
                span->event_call_state = 0;
                span->alarm = 0;
                gsm_call_hangup_cfm(span->span_id, span->calls.data.callid,status);
                memset(&span->calls, 0, sizeof(span->calls));
                gsm_log(GSM_LOG_DEBUG,"CLEANUP CALLS 2 span_%d->calls.newtate=%d\n",span->span_id,span->calls.newstate);
          }
           break;
          default:
            break;
     }
     return 0;
 }

 int gsm_hangup_req(unsigned int span_id,  unsigned char callid)
 {
	 gsm_log(GSM_LOG_DEBUG,"gsm_hangup_req start\n");
     struct gsm_span *span = NULL;
     if( (span_id <= 0)  || (span_id > GSM_MAX_SPANS))
         return 1;
     span = gsm_get_span(span_id);
     if(!span)
        return 1;
     if(span->span_state < GSM_SPAN_STATE_RUNNING)
         return 1;
      gsm_call_set_state(span, GSM_CALL_STATE_HANGUP);
  	  gsm_log(GSM_LOG_WARNING,"gsm_hangup_req end\n");
      return 0;
 }

 int gsm_hangup_cfm(unsigned int span_id,  unsigned char callid)
 {
	 gsm_log(GSM_LOG_DEBUG,"gsm_hangup_cfm start span_id=%d callid=%d\n",span_id,callid);
     struct gsm_span *span = NULL;
     if( (span_id <= 0)  || (span_id > GSM_MAX_SPANS))
        return 1;
     span = gsm_get_span(span_id);
     if(!span)
        return 1;
     if(span->span_state < GSM_SPAN_STATE_RUNNING)
         return 1;
     gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING_CMPL);
     return 0;
 }


 int gsm_call_cfm(unsigned int span_id,  unsigned char callid)
 {
	 gsm_log(GSM_LOG_DEBUG,"gsm_call_cfm start span_id=%d callid=%d\n",span_id,callid);
     struct gsm_span *span = NULL;
     if( (span_id <= 0)  || (span_id > GSM_MAX_SPANS))
        return 1;
     span = gsm_get_span(span_id);
     if(!span)
        return 1;
     if(span->span_state < GSM_SPAN_STATE_RUNNING)
         return 1;
     gsm_call_set_state(span, GSM_CALL_STATE_ANSWERED);
      return 0;
 }

 int gsm_call_req(unsigned int span_id,  struct gsm_call_data  *call_data)
 {
    gsm_log(GSM_LOG_DEBUG,"gsm_call_req\n");
    struct gsm_span *span = NULL ;
    int res = 0;
    if( (span_id <= 0)  || (span_id > GSM_MAX_SPANS))
        return 1;
    span = gsm_get_span(span_id);
    if(!span || !call_data)
        return 1;
    if(span->span_state < GSM_SPAN_STATE_RUNNING)
         return 1;
    memcpy(&span->calls.data, call_data, sizeof(span->calls.data));
    res = gsm_span_create_call(span, 0, GCOM_DIRECTION_OUTGOING);
    if(res)
        return res;
    gsm_call_set_state(span, GSM_CALL_STATE_DIALING);
    return 0;
 }

 int gsm_span_create_call(struct gsm_span *span, unsigned int call_id,int direct)
 {
    gsm_log(GSM_LOG_DEBUG,"gsm_span_create_call\n");
    unsigned int id = 0;
    if(call_id)//not zero
    {
        if(span->calls.newstate > GSM_CALL_STATE_INIT)
        {
            gsm_log(GSM_LOG_DEBUG, "gsm module spanno=%d is busy already have call\n",span->span_id);
            return 1;
        }
        id = call_id;
    }
    else
    {
        id = gsm_find_span_callid(span);
        if(id == 0)
        {
            gsm_log(GSM_LOG_DEBUG,"span id=%d is in max calls now\n",span->span_id);
            return 1;
        }
    }
     span->calls.span = span;
     span->calls.data.callid = id;
     gsm_log(GSM_LOG_WARNING,"gsm_span_create_call id=%d\n",id);
     span->calls.data.direct = direct;
     span->calls.newstate = GSM_CALL_STATE_INIT;
     span->calls.direction = direct;
     gsm_log(GSM_LOG_WARNING,"gsm_span_create_call FINISH 0\n");
     return 0;
 }

 int gsm_find_span_callid(struct gsm_span *span)
 {
    int i = 0;
    if(span->calls.newstate == 0)
         return i+1;
     return 0;
 }

 int gsm_find_span_call_by_state(struct gsm_span* span, gsm_call_state_t state)
 {
    int i = 0;
    if(span->calls.newstate == state)
        return i+1;
    return 0;
 }
