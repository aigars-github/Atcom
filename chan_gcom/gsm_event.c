#include <stdlib.h>
#include <stdio.h>
#include "gsm_internal.h"
#include "libgsmcom.h"
#include "gsm_sms.h"
#include "gsm_queue.h"

GSM_SCHEDULED_CB(gsm_cmd_timeout);
GSM_SCHEDULED_CB(gsm_cmd_complete);

int gsm_parse_event_data(struct gsm_span *span, char *token, int len );


 int gsm_write_cmd(struct gsm_span *span, void *data, unsigned int len)
 {
    int res = 0;
    if(!span || !data)
        return -1;
    res = gsm_span_write(span->span_id, data, len);
    if (res < len) 
    { 
         gsm_log(GSM_LOG_WARNING,"gsm write cmd=%s failed res=%d\n",(char*)data,res);
         return -1;
    }
    memcpy(span->last_cmd, (char *)data, sizeof(span->last_cmd));
    return res;
 }

 void gsm_read_events(unsigned int span_id, const char *buf, int len )
 {
	int res = 0;
    struct gsm_span *span = NULL;
    event_fifo_queue *eventq = NULL;
    char tmp[512] = {0};
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS) )
        return;
    span = gsm_get_span( span_id);
    if(!span)
        return ;
    eventq = &span->event_queue;
    memcpy(tmp, buf, sizeof(tmp));
    res = write_to_event_queue(eventq,tmp);
 }

 void gsm_span_do_events(struct gsm_span *span)
 {
    int res = 0;
    char rd_buf[512] = {0};
    event_fifo_queue *queue = NULL;
    if(!span)
       return;
    queue = &span->event_queue;
    res = read_from_event_queue(queue, rd_buf,sizeof(rd_buf));
    if(res)
       return ;
    memcpy(span->event_buf, rd_buf, sizeof(span->event_buf));
    res = gsm_parse_event_data(span,rd_buf, strlen(rd_buf));
 }

 static int do_ring_event(struct gsm_span *span)
 {
	int res = 0;
    unsigned int call_id = 0;
    if(!span)
    	return 1;
    res = gsm_find_span_call_by_state(span, GSM_CALL_STATE_DIALING);
    if(res)
    {
       gsm_log(GSM_LOG_WARNING," handle_gsm_notify_ring but have call dialing\n");
       return 1;
    }
    res = gsm_find_span_call_by_state(span, GSM_CALL_STATE_DIALED);
    if(res)
    {
        gsm_log(GSM_LOG_WARNING,"handle_gsm_notify_ring but call dialed\n");
        return 1;
    }
    res = gsm_span_create_call(span, call_id, GCOM_DIRECTION_INCOMING);
    if(res)
    {
        gsm_log(GSM_LOG_WARNING,"handle_gsm_notify_ring but gsm_span_create_call fail\n");
        return 1;
    }
    gsm_call_set_state(span, GSM_CALL_STATE_DIALING);
    return 0;
 }

 static int do_clcc_event(struct gsm_span *span)
 {
    if(!span)
         return 1;
    span->event_call_state = 1;
    return 0;
 }

 static int do_clip_event(struct gsm_span *span, char *buf)
 {
    int res = 0;
    gsm_span_call_t *span_call = NULL;
    char *cmdtokens[10];
    unsigned num, i,j;
    char numbuf[50] = {0};
    if(!span)
        return 1;
    gsm_match_prefix(buf, "+CLIP: ");     
    res = gsm_find_span_call_by_state(span, GSM_CALL_STATE_DIALED);
    if(res)
    {
         span_call =  &span->calls;
         if(span_call->recv_clip && span_call)
         {
            gsm_log(GSM_LOG_WARNING,"handle_gsm_notify_clip but clip recved\n");
            return 1;
         }
    }
    res = gsm_find_span_call_by_state(span, GSM_CALL_STATE_DIALING);
    if(!res)
    {
        gsm_log(GSM_LOG_WARNING,"handle_gsm_notify_clip but without clip\n");
        return 1;
    }
     span_call =  &span->calls;
     gsm_log(GSM_LOG_WARNING, "clip and find GSM_CALL_STATE_DIALING\n");
     if(span_call->recv_clip || !span_call)
        return 1;
     num = gsm_cmd_entry_tokenize(buf, cmdtokens, gsm_array_len(cmdtokens));
     if (num < 1)
     {
         gsm_free_tokens(cmdtokens);
         return 1;
     }
     if (strlen(cmdtokens[0]) <= 0)
     {
          gsm_free_tokens(cmdtokens);
          return 1;
     }
     if(strlen(cmdtokens[0]) > sizeof(span_call->data.calling_num))
          num = sizeof(span_call->data.calling_num);
     else
          num = strlen(cmdtokens[0]) ;
     gsm_log(GSM_LOG_WARNING, " clip and find numlen=%d num=%s\n",num,cmdtokens[0]);
     memcpy(numbuf, cmdtokens[0], sizeof(numbuf));
     j=0;
     for(i=0; i<num; i++)
     {
       if((numbuf[i] == '"') && ((i==0)||(i==(num-1))))
        continue;
       else
       {
          span_call->data.calling_num[j] = numbuf[i];
          if(span_call->data.calling_num[j] > 57)
          {
              gsm_free_tokens(cmdtokens);
              return 1;
          }
          else if( (span_call->data.calling_num[j] != 43)  && ( span_call->data.calling_num[j] <48))
          {
               gsm_free_tokens(cmdtokens);
               return 1;
          }
          j++;
      }
   }
   gsm_free_tokens(cmdtokens);
   span_call->recv_clip = 1;
   gsm_call_set_state(span, GSM_CALL_STATE_DIALED);
   return 0;
 }

 static int do_creg_event(struct gsm_span *span, char *buf)
 {
    int stat;
    unsigned count = 0;
    char *cmdtokens[8];
    int consumed_tokens = 0;
    if(!span)
        return 1;
     gsm_match_prefix(buf, "+CREG: ");
     count = gsm_cmd_entry_tokenize(buf, cmdtokens, gsm_array_len(cmdtokens));
     if (count < 0)
            consumed_tokens = 1;
     else
     {
    	if((count == 3) || (count == 1))
             stat = atoi(cmdtokens[0]);
		else 
           stat = atoi(cmdtokens[1]);
        consumed_tokens = 0;
     }
     gsm_free_tokens(cmdtokens);
     if(!consumed_tokens)
     {
         switch(stat)
         {
                case 0:
                case 2:
                case 3:
                case 4:
                    if (span->span_state >= GSM_SPAN_STATE_REGNET)
                        span->span_state= GSM_SPAN_STATE_START;
                    break;
                case 1:
                case 5:
                    if (span->span_state >= GSM_SPAN_STATE_START)
                        span->span_state= GSM_SPAN_STATE_RUNNING;
                    break;
                default:
                    break;
            }
        }
        return consumed_tokens;
 }

 static int  do_cmt_test()
 {
 }

 static int do_cmt_event(struct gsm_span *span )
 {
    gsm_log(GSM_LOG_DEBUG, "do_cmt_event START span->rx_sms_state=%d strlen(span->event_buf)=%d\n",span->rx_sms_state,strlen(span->event_buf));
    int res = 0;
    char *cmd[10];
    int i = 0;
    int num = 0;
    char savebuf[GSM_MAX_ATCMD_LEN] = {0};
    if(!span)
        return 1;
    if( span->rx_sms_state == 0)
    {
        span->rx_sms_state = 1;
        memset(span->sms_buf, 0, sizeof(span->sms_buf));
        memcpy(span->sms_buf, span->event_buf,strlen(span->event_buf));
        gsm_log(GSM_LOG_WARNING, "recv pdu do_cmt_event and  smsbuf=%s\n",span->sms_buf);
        gsm_log(GSM_LOG_WARNING, "do_cmt_event END 1\n");
        return 0;
    }
    else 
    {
    	memcpy(span->sms_buf+strlen(span->sms_buf),span->event_buf,strlen(span->event_buf));
    	span->rx_sms_state = 0;
    }
    gsm_sms_type_t type=gsm_check_sms_mode(span->sms_buf);
    gsm_log(GSM_LOG_DEBUG, "do_cmt_event type=%d\n",type);
    if(type==GSM_SMS_PDU)
    {
        gsm_log(GSM_LOG_DEBUG, "recv pdu sms and handle\n");
        char tmp[256],tmp1[32],len[32],*ret;
        ret = gsm_get_dot_string(span->sms_buf, tmp, ':'); // +CMT:
        ret = gsm_get_dot_string(ret, tmp1, ','); // +CMT:_,23\n
        ret = gsm_get_dot_string(ret, len, '\r');
        if(*ret==0x0A)
       	 ret++;
        if((*(ret+strlen(ret)-1))==0x0A)
       	 *(ret+strlen(ret)-1)=0;
        if((*(ret+strlen(ret)-1))==0x0D)
       	 *(ret+strlen(ret)-1)=0;
        gsm_log(GSM_LOG_DEBUG, "recv pdu sms and handle tmp=%s,tmp1=%s,len=%s ret=<%s> r\n",tmp,tmp1,len,ret);
        gsm_handle_incoming_pdu_sms(span,ret, atoi(len));
   }
   else if(type==GSM_SMS_TXT)
   { 
        gsm_log(GSM_LOG_WARNING, "recv txt sms and handle\n");
        char tmp[256],sender[256],sc[256],timestamp[256],body[256],*ret;
        ret = gsm_get_dot_string(span->sms_buf, tmp, ':');
        ret = gsm_get_dot_string(ret, sender, ',');
        ret = gsm_get_dot_string(ret, sc, ',');
        ret = gsm_get_dot_string(ret, timestamp, '\r');
        if(*ret==0x0A)
       	 ret++;
        if((*(ret+strlen(ret)-1))==0x0A)
       	 *(ret+strlen(ret)-1)=0;
        if((*(ret+strlen(ret)-1))==0x0D)
       	 *(ret+strlen(ret)-1)=0;
        gsm_handle_incoming_txt_sms(span, sender,sc,timestamp ,ret);
    }
    return res;
 }



 static int do_busy_event(struct gsm_span *span)
 {
    if(!span)
           return 1;
    gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    return 0;
 }

 static int do_no_dialtone_event(struct gsm_span *span)
 {
    if(!span)
           return 1;
    gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    return 0;
 }

 static int do_error_event(struct gsm_span *span)
 {
    int res = 0;
    if(!span)
           return 1;
    if((span->calls.newstate >0) && (span->calls.newstate < GSM_CALL_STATE_UP))
        gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    else if(span->smss.stats > GSM_SMS_STATE_START)
        gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
    else if( (span->span_state == GSM_SPAN_STATE_START))
    {
        if(span->cmd_err < 3)
        {
            res = gsm_write_cmd(span, span->last_cmd, strlen(span->last_cmd));
            if(res == -1)
                return 1;
            span->cmd_err++;
        }
        else
            gsm_switch_start_state_cmd(span, span->module.state);
    }
    return 0;
 }

 static int do_no_answer_event(struct gsm_span *span)
 {
    if(!span)
           return 1;
    gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    return 0;
 }

 static int do_no_carrier_event(struct gsm_span *span)
 {
    if(!span)
           return 1;
    gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    return 0;
 }

 static int do_cms_error_event(struct gsm_span *span)
 {
    int res = 0;
    if((span->calls.newstate >0) && (span->calls.newstate < GSM_CALL_STATE_UP))
        gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    else if(span->smss.stats > GSM_SMS_STATE_START)
        gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
    else if( (span->span_state == GSM_SPAN_STATE_START))
    {
        if(span->cmd_err < 3)
        {
            res = gsm_write_cmd(span, span->last_cmd, strlen(span->last_cmd));
            if(res == -1)
                return 1;
            span->cmd_err++;
        }
        else
            gsm_switch_start_state_cmd(span, span->module.state);
    }
    return 0;
 }

 static int do_cme_error_event(struct gsm_span *span)
 {
    int res = 0;
    if((span->calls.newstate >0) && (span->calls.newstate < GSM_CALL_STATE_UP))
        gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
    else if(span->smss.stats > GSM_SMS_STATE_START)
        gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
    else if( (span->span_state == GSM_SPAN_STATE_START))
    {
        if(span->cmd_err < 3)
        {
            res = gsm_write_cmd(span, span->last_cmd, strlen(span->last_cmd));
            if(res == -1)
                return 1;
            span->cmd_err++;
        }
        else
            gsm_switch_start_state_cmd(span, span->module.state);
    }
    return 0;
 }

 static int do_down_event(struct gsm_span *span)
 {
    return 0;
 }

 static int do_answer_event(struct gsm_span *span)
 {
    if(!span)
           return 1;
     gsm_log(GSM_LOG_DEBUG,"do_answer_event span_%d->calls.newtate=%d\n",span->span_id,span->calls.newstate);
    if(span->calls.newstate>0)
     gsm_call_set_state(span, GSM_CALL_STATE_ANSWERED);
    return 0;
 }


 static int do_cpin_req(struct gsm_span *span, char *buf)
 {
     if(!span || !buf)
           return 1;
    if(strstr(buf, "READY") &&  (span->span_state == GSM_SPAN_STATE_REGNET))
        gsm_set_module_at_cmd(span, GSM_GET_CREG);
    else if(strstr(buf, "READY") && (span->span_state == GSM_SPAN_STATE_START))
       gsm_set_module_at_cmd(span, GSM_GET_CREG);
    else if(strstr(buf, "SIM PIN"))
        gsm_set_module_at_cmd(span, GSM_SIM_PIN_REQ);
    else
        gsm_set_module_at_cmd(span, GSM_GET_CREG);
    return 0;
 }

 static int do_get_cgmr(struct gsm_span *span, char *buf)
 {
     int res = 0;
     if(!span || !buf)
           return 1;
     res = gsm_match_prefix(buf, "Revision: ");
     if(res)
     {
        gsm_log(GSM_LOG_WARNING, "do_get_cgmr fail spanid=%d\n",span->span_id);
        return 1;
    }
    memcpy(span->module.sim_info.version, buf, sizeof(span->module.sim_info.version));
    if((span->span_state == GSM_SPAN_STATE_START))
     gsm_set_module_at_cmd(span, GSM_GET_CGSN);
    return 0;
 }


 static int do_get_creg(struct gsm_span *span, char *buf)
 {
     char *cmdtokens[4];
     int stat;
     int num = 0;
     if(!span || !buf)
           return 1;
     num = gsm_match_prefix(buf, "+CREG: ");
     if(num)
     {
        gsm_log(GSM_LOG_WARNING, "do_get_creg fail spanid=%d\n", span->span_id);
        gsm_set_module_at_cmd(span, GSM_GET_CSQ);
        return 1;
     }
     num = gsm_cmd_entry_tokenize(buf, cmdtokens, gsm_array_len(cmdtokens));
     if(num < 1)
     {
         gsm_set_module_at_cmd(span, GSM_GET_CSQ); 
         return 1;
     }
     stat = atoi(cmdtokens[1]);
     if(stat == 1)
        gsm_set_span_status(span, GSM_SPAN_STATE_REGNET); 
     gsm_set_module_at_cmd(span, GSM_GET_CSQ);
     return 0;
  }

 
 static int do_get_cgsn(struct gsm_span *span, char *buf)
 {
     int len = 0;
     int i = 0;
     if(!span || !buf)
           return 1;
    len = strlen(buf);
    for(i = 0; i < len; i++)
    {
        if((buf[i] == '\r') || (buf[i] == '\n'))
            break;
        if(( buf[i] < '0' ) || ( buf[i] > '9' ))
        {
            gsm_log(GSM_LOG_WARNING, "span id=%d get cgsn fail len=%d\n", span->span_id,len);
            return 1;
        }
   }
   memcpy(span->module.sim_info.imei, buf, sizeof(span->module.sim_info.imei));
   gsm_set_module_at_cmd(span, GSM_GET_CIMI);
   return 0;
 }


 static int do_get_cimi(struct gsm_span *span, char *buf)
 {
     int len = 0;
     int i = 0;
     if(!span || !buf)
           return 1;
    len = strlen(buf);
    for(i = 0; i < len; i++)
    {
        if( (buf[i] == '\r') ||  (buf[i] == '\n'))
            break;
        if(( buf[i] < '0' ) || ( buf[i] > '9' ))
        {
            gsm_log(GSM_LOG_WARNING, "span id=%d get cimi fail len=%d\n", span->span_id,len);
            return 1;
        }
   }
   memcpy(span->module.sim_info.imsi, buf, sizeof(span->module.sim_info.imsi));
   gsm_set_module_at_cmd(span, GSM_GET_CSCA);
   return 0;
 }


 static int do_get_csca(struct gsm_span *span, char *buf)
 {
     char *cmdtokens[5];
     int num;
     int len = 0;
     int i,j;
     if(!span || !buf)
           return 1;
     num = gsm_match_prefix(buf, "+CSCA: ");
     if(num)
     {
        gsm_log(GSM_LOG_WARNING, "do_get_csca fail spanid=%d\n", span->span_id);
        return 1;
     }
     num = gsm_cmd_entry_tokenize(buf, cmdtokens, gsm_array_len(cmdtokens));
     if(num < 1)
       return 1;
     len = strlen(cmdtokens[0]);
     j = 0;
     for(i = 0; i< len; i++)
     {
        if(cmdtokens[0][i] != '"')
        {
            span->module.sim_info.smsc[j] = cmdtokens[0][i];
            j++;
        }
    }
    if(span->span_state == GSM_SPAN_STATE_START)
        gsm_set_module_at_cmd(span, GSM_GET_CPIN);
    return 0;
 }

 static int do_get_csq(struct gsm_span *span, char *buf)
 {
    char *cmdtokens[5];
    int num;
    num = gsm_match_prefix(buf, "+CSQ: ");
    if(num)
    {
        gsm_log(GSM_LOG_WARNING, "do_get_csq fail spanid=%d\n", span->span_id);
        return 1;
    }
    num = gsm_cmd_entry_tokenize(buf, cmdtokens, gsm_array_len(cmdtokens));
    if(num < 1)
        return 1;
    span->csq = atoi(cmdtokens[0]);
    return 0;
 }

 int gsm_parse_event_data(struct gsm_span *span, char *token, int len )
 {
    char buf[512] = {0};
    if(!span || !token)
        return 0;
    memcpy(buf, token, sizeof(buf));
    if(!strncasecmp(buf, "RING",  strlen("RING")))
       do_ring_event(span);
    else if(!strncasecmp(buf, "+CLIP",  strlen("+CLIP")))
        do_clip_event(span, buf);
    else if(!strncasecmp(buf, "+CREG",  strlen("+CREG")))
        do_creg_event(span, buf);
    else if(!strncasecmp(buf, "+CMT:",  strlen("+CMT:")) ||  (span->rx_sms_state == 1))
        do_cmt_event(span );
    else if(!strncasecmp(buf, "+CSQ:",  strlen("+CSQ:")))
        do_get_csq(span,buf);
    else if(!strncasecmp(buf, "BUSY",  strlen("BUSY")))
        do_busy_event(span);
    else if(!strncasecmp(buf, "NO DIALTONE",  strlen("NO DIALTONE")))
        do_no_dialtone_event(span);
    else if(!strncasecmp(buf, "ERROR",  strlen("ERROR")))
        do_error_event(span);
    else if(!strncasecmp(buf, "NO ANSWER",  strlen("NO ANSWER")))
        do_no_answer_event(span);
    else if(!strncasecmp(buf, "NO CARRIER",  strlen("NO CARRIER")))
        do_no_carrier_event(span);
    else if(!strncasecmp(buf, "+CMS ERROR",  strlen("+CMS ERROR")))
        do_cms_error_event(span);
    else if(!strncasecmp(buf, "+CME ERROR",  strlen("+CME ERROR")))
        do_cme_error_event(span);
    else if(!strncasecmp(buf, "NORMAL POWER DOWN",  strlen("NORMAL POWER DOWN")))
        do_down_event(span);
    else if(!strncasecmp(buf, "MO CONNECTED",  strlen("MO CONNECTED")))
        do_answer_event(span);
    else if(!strncasecmp(buf,"> ", strlen("> ")) && (span->smss.stats == GSM_SMS_STATE_SEND_HEADER))
        gsm_set_sms_state(span, GSM_SMS_STATE_SEND_BODY);
    else if(!strncasecmp(buf,"+CMGS:",strlen("+CMGS:")) && (span->smss.stats == GSM_SMS_STATE_SEND_END))
        gsm_set_sms_state(span, GSM_SMS_STATE_COMPLETE);
    else if(!strncasecmp(buf, "OK",  strlen("OK")) &&  (span->smss.stats == GSM_SMS_STATE_START))
        gsm_set_sms_state(span, GSM_SMS_STATE_SEND_HEADER);
    else if(!strncasecmp(buf, "OK",  strlen("OK") ) && (span->module.state== GSM_SET_ECHO) && (span->span_state <= GSM_SPAN_STATE_START))
        gsm_set_module_at_cmd(span, GSM_SET_CLIP);
    else if(!strncasecmp(buf, "OK",  strlen("OK") ) &&  (span->module.state== GSM_SET_CREG) && (span->span_state <= GSM_SPAN_STATE_START))
        gsm_set_span_status(span, GSM_SPAN_STATE_START);
    else if((span->module.state== GSM_GET_CPIN) && (span->span_state == GSM_SPAN_STATE_START) && !strncasecmp(buf, "+CPIN:",  strlen("+CPIN:")))
        do_cpin_req(span, buf);
    else if(!strncasecmp(buf, "+CPIN:",  strlen("+CPIN:") ))
         do_cpin_req(span, buf);
    else if(!strncasecmp(buf, "Revision:",  strlen("Revision:") ))
         do_get_cgmr(span, buf);
    else if((span->module.state== GSM_GET_CGSN) &&  (span->span_state == GSM_SPAN_STATE_START) && (buf[0] > '0')&&(buf[0] <= '9')&&  (strlen(buf) > 4))
         do_get_cgsn(span, buf);
    else if((span->module.state== GSM_GET_CIMI) && (span->span_state == GSM_SPAN_STATE_START) && (buf[0] > '0')&&(buf[0] <= '9') && (strlen(buf) > 4))
         do_get_cimi(span, buf);
    else if((span->module.state== GSM_GET_CREG) && (span->span_state == GSM_SPAN_STATE_START) && !strncasecmp(buf, "+CREG",  strlen("+CREG") ))
         do_get_creg(span, buf);
    else if(!strncasecmp(buf, "+CSCA:",  strlen("+CSCA:")))
         do_get_csca(span, buf);
    else if(!strncasecmp(buf, "+CLCC:",  strlen("+CLCC:")))
         do_clcc_event(span);
    else if(!strncasecmp(buf, "OK",  strlen("OK") ))
    {
         if(span->span_state == GSM_SPAN_STATE_START)
         {
             if((span->module.state != GSM_GET_CSCA ) &&
                (span->module.state != GSM_GET_CIMI )  &&
                (span->module.state != GSM_GET_CGSN ) &&
                (span->module.state != GSM_GET_CGMR ))
                   gsm_switch_start_state_cmd(span, span->module.state);
         }
         else if(!strncasecmp(span->last_cmd, "AT+CLCC",  strlen("AT+CLCC")))
         {
            if((span->event_call_state == 0))
            {
               span->alarm++;
               if(span->alarm>=1)
               {
                   span->alarm = 0;
                   gsm_call_set_state(span, GSM_CALL_STATE_TERMINATING);
                }
            }
        }
        span->cmd_err = 0;
    }
    return 0;
 }


 void gsm_span_do_sched(struct gsm_span *span)
 {
    int res = 0;
    if(!span)
        return ;
    res =  gsm_sched_runq(&span->sched_queue);
 }

 GSM_SCHEDULED_CB(gsm_cmd_complete)
 {
    struct gsm_span *span = (struct gsm_span *)data;
    if(span)
        span->cmd_busy = 0;
    return 0;
 }

 GSM_SCHEDULED_CB(gsm_cmd_timeout)
 {
    return 0;
 }

 int GSM_INIT_EVENT_QUEUE(event_fifo_queue*eventq)
 {
     int res =0;
     if(!eventq)
         return 1;
     memset(eventq, 0, sizeof(event_fifo_queue));
     res = gsm_mutex_init( &eventq->lock );
     eventq->len = GSM_MAX_EVENT_QUEUE_LEN; 
     eventq->rd_idx= 0;
     eventq->wr_idx= 0;
     return res;
 }

 int  GSM_INIT_SCHED_QUEUE(sched_fifo_queue *schedq)
 {
    int res = 0;
    if(!schedq)
        return 1;
    memset(schedq, 0, sizeof(sched_fifo_queue));
    res = gsm_mutex_init( &schedq->lock );
    schedq->len = GSM_MAX_SCHED_QUEUE_LEN;
    return res;
 }
