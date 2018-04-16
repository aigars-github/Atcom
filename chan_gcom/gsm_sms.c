#include <stdlib.h>
#include <stdio.h>
#include "gsm_internal.h"
#include "libgsmcom.h"
#include "gsm_sms.h"


GSM_SCHEDULED_CB(gsm_sched_check_sms_state);
GSM_SCHEDULED_CB(gsm_sched_check_sms_state0);
GSM_SCHEDULED_CB(gsm_sched_check_sms_state1);

static int  gsm_send_sms_body(struct gsm_span *span);

int check_num_is_digit(const char *num)
{
    const char tab[]="0123456789*#";
    if(!num)
        return 1;
    int len = strlen(num);
    int i = 0;
    if(num[0] != '+' && !strchr(tab,num[0]))
        return 1;
    for(i = 1; i<len; i++)
    {
        if(!strchr(tab,num[i]))
            return 1;
    }
    return 0;
}

int gsm_encode_pdu_body(struct gsm_span *span, struct gsm_sms_event_data *sms)
{
    int len = 0;
    int pdu_header_len = 0;
    unsigned char pdu_msg[GSM_MAX_SMS_LEN] = {0};
    unsigned char *buf = pdu_msg;
    unsigned char utf8_data[GSM_MAX_SMS_LEN] = {0};
    int udh_len = 0;
    unsigned char *udh_loc = NULL;
    int i = 0;
    if(!span || !sms)
        return 1;
     if((strlen(sms->smsc)>0)&&check_num_is_digit(sms->smsc))
         return 1;
     if(gsm_encode_pdu_smsc(sms->smsc, &buf,  &len))
        return 1;
     pdu_header_len = len;
      gsm_encode_pdu_submit_ref(0x31, 0x00, &buf, &len ); 
     if (gsm_encode_pdu_to_num(sms->to, &buf, &len))
        return 1;
     gsm_encode_pdu_pid(sms->pid, &buf, &len);
     memcpy(utf8_data, sms->content.message, sizeof(utf8_data));
     gsm_encode_pdu_dcs(sms->dcs, &buf, &len);
     gsm_encode_pdu_vp(0x00, &buf, &len);
     udh_loc = buf;
     *udh_loc =  sms->content.msg_len;
     switch (sms->dcs) 
     {
      case 0:
      {
       unsigned char *p_udh_loc = udh_loc + 1;
       int utf8_len = strlen((char *)utf8_data);
       int ret =0;
       int offset = octet_to_septet(udh_len);
       ret = gsmEncode7bit(utf8_data, &p_udh_loc, utf8_len,offset);
       if(ret)
          return 1;
       *udh_loc = octet_to_septet(udh_len)+utf8_len;
       len += septet_to_octet(utf8_len + octet_to_septet(udh_len)) - udh_len;
       len++;
      }
      break;
     case 8:
     {
      len += sms->content.msg_len + udh_len+1;
       break;
      }
      break;
     default:
      return 1;
     }
     sms->pdu_len = len - pdu_header_len;
     memset(sms->body, 0, sizeof(sms->body));
     for (i = 0; i < (len - sms->content.msg_len ) ; i++)
      sprintf((char *)&sms->body[i*2], "%02x", (0xFF) & pdu_msg[i]);
     memcpy((char*)(sms->body+i*2), utf8_data, sms->content.msg_len*2);
     sms->content.msg_len= len*2;
     return 0;
   }

  static int  gsm_send_sms_body(struct gsm_span *span)
  {
   char cmd[1024] = {0};
   struct gsm_sms_event_data *sms = NULL;
   int len, write_len;
    if(!span)
        return 1;
    sms = &span->smss;
    gsm_log(GSM_LOG_DEBUG,"gsm_send_sms_body start sms->body=%s\n",sms->body);
    sms->msg_len = strlen(sms->body);
    sms->send_len = 0; 
    while (sms->send_len < sms->msg_len)
    {
      memset(cmd, 0, sizeof(cmd));
      len = sms->msg_len - sms->send_len;
      if (len <= 0)
         break;
      if (len > sizeof(cmd))
        len = sizeof(cmd) -1;
      memcpy(cmd, &sms->body[sms->send_len], len);
      write_len = gsm_span_write(span->span_id, cmd, len);
      sms->send_len += write_len;
      if (write_len <= 0)
      {
          gsm_log(GSM_LOG_WARNING," sendsms body but fail\n");
          break;
      }
     }
    gsm_set_sms_state(span, GSM_SMS_STATE_SEND_END);
    return 0;
  }

GSM_SCHEDULED_CB(gsm_sched_check_sms_state)
{
    struct gsm_span *span = (struct gsm_span*)data;

    if(!span)
        return 1;
    if(span->smss.stats == GSM_SMS_STATE_SEND_HEADER)
    {
        gsm_log(GSM_LOG_WARNING, "gsm_sched_check_sms_state and set fail\n");
        gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
    }
    return 0;
}

GSM_SCHEDULED_CB(gsm_sched_check_sms_state0)
{
    struct gsm_span *span = (struct gsm_span*)data;

    if(!span)
        return 1;
    if(span->smss.stats == GSM_SMS_STATE_START)
    {
        gsm_log(GSM_LOG_WARNING, "gsm_sched_check_sms_state and set fail\n");
        gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
    }
    return 0;
}


GSM_SCHEDULED_CB(gsm_sched_check_sms_state1)
{
    struct gsm_span *span = (struct gsm_span*)data;

    if(!span)
        return 1;
    if(span->smss.stats == GSM_SMS_STATE_SEND_END)
    {
        gsm_log(GSM_LOG_WARNING, "gsm_sched_check_sms_state and set fail\n");
        gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
    }
    return 0;
}

int gsm_set_sms_state(struct gsm_span *span, gsm_sms_state_t  newstate)
{
     char send_cmd[50] = {0};
     int res =0;
     if(!span)
        return 1;
    span->smss.stats= newstate;
    switch (newstate)
    {
        case GSM_SMS_STATE_INIT:
            break;
        case GSM_SMS_STATE_READY:
        {
            gsm_log(GSM_LOG_DEBUG, "sendsms state=GSM_SMS_STATE_READY\n");
           if(!span->smss.gsm_sms_mode)
              res =  gsm_encode_pdu_body(span, &span->smss);
           if(res)
           {
            gsm_log(GSM_LOG_WARNING, "sendsms gsm_encode_pdu_body fail\n");
             gsm_set_sms_state(span, GSM_SMS_STATE_FAIL);
           }
           else
            gsm_set_sms_state(span, GSM_SMS_STATE_START);
         }
         break;
        case GSM_SMS_STATE_START:   //change cmgf mode  sms_queue
        {
         gsm_log(GSM_LOG_DEBUG, "sendsms state=GSM_SMS_STATE_START\n");
         if(span->smss.gsm_sms_mode)
            snprintf(send_cmd, sizeof(send_cmd), "%s%s", "AT+CMGF=1",GSM_CMD_END);
         else
           snprintf(send_cmd, sizeof(send_cmd), "%s%s", "AT+CMGF=0",GSM_CMD_END);
         res = gsm_write_cmd(span, send_cmd, strlen(send_cmd));
         span->timer = gsm_sched_add(&span->sched_queue, 3000, gsm_sched_check_sms_state0, (void*)span);
        }
         break;
        case GSM_SMS_STATE_SEND_HEADER://cmd queue
        {
         gsm_log(GSM_LOG_DEBUG, "sendsms state=GSM_SMS_STATE_SEND_HEADER\n");
         if(span->smss.gsm_sms_mode)
           snprintf(send_cmd, sizeof(send_cmd), "%s\"%s\"%s", "AT+CMGS=",span->smss.to,GSM_CMD_END);
         else
           snprintf(send_cmd, sizeof(send_cmd), "%s%d%s", "AT+CMGS=", span->smss.pdu_len,GSM_CMD_END);
         gsm_sched_del(&span->sched_queue, span->timer);
         res = gsm_write_cmd(span, send_cmd, strlen(send_cmd));
         span->timer = gsm_sched_add(&span->sched_queue, 6000, gsm_sched_check_sms_state, (void*)span);

        }
        break;
        case GSM_SMS_STATE_SEND_BODY:
        {
           gsm_log(GSM_LOG_DEBUG, "sendsms state=GSM_SMS_STATE_SEND_BODY\n");
           gsm_sched_del(&span->sched_queue, span->timer);
           gsm_send_sms_body(span);
        }
        break;
        case GSM_SMS_STATE_SEND_END:   //CTROL+Z  send end
        {
           sprintf(send_cmd, "%c\r\n", 0x1a);
           gsm_log(GSM_LOG_WARNING,"sendsms and send end char\n");
           res = gsm_write_cmd(span, send_cmd, strlen(send_cmd));
           span->timer = gsm_sched_add(&span->sched_queue, 9000, gsm_sched_check_sms_state1, (void*)span);
        }
        break;
        case GSM_SMS_STATE_COMPLETE:
        {    
            span->smss.status.success = 1;
            if (span->smss.gsm_sms_mode && (!span->smss.queue_flag)&&(!span->smss.long_flag))
                 sleep(1);
            gsm_sched_del(&span->sched_queue, span->timer);
            gsm_log(GSM_LOG_WARNING,"sendsms ok\n");
            gsm_sms_report(span->span_id, &span->smss);
            memset(&span->smss, 0, sizeof(span->smss));
           
        }
        break;
        case GSM_SMS_STATE_FAIL:
        {   
             snprintf(send_cmd, sizeof(send_cmd), "%c", 0x1b);
             res = gsm_write_cmd(span, send_cmd, strlen(send_cmd));
             span->smss.status.success = 0;
             gsm_sms_report(span->span_id, &span->smss);
             memset(&span->smss, 0, sizeof(span->smss));
             gsm_sched_del(&span->sched_queue, span->timer);
             gsm_log(GSM_LOG_WARNING,"sendsms fail and destory and send esc state=%d\n",span->smss.stats);
        }
        break;
        default:
            return 1;
    }
    return 0;
  }


 int gsm_sms_req(unsigned int span_id,  struct gsm_sms_event_data  *sms_data)
 {
    struct gsm_span *span = NULL;
    if(span_id <= 0 || span_id > GSM_MAX_SPANS)
        return 1;
    span = gsm_get_span( span_id);
    if(!span || !sms_data)
        return 1;
    if(span->span_state < GSM_SPAN_STATE_RUNNING)
         return 1;
    memcpy(&span->smss, sms_data, sizeof(span->smss));
    if(span->smss.stats >0 || (span->calls.newstate > 0))
    {
        gsm_log(GSM_LOG_WARNING, "gsm module spanno=%d is busy\n",span->span_id);
        return 1;
    }
    gsm_set_sms_state(span, GSM_SMS_STATE_READY);
   return 0;
 }

 char *gsm_get_dot_string(char *in, char *out, char flag)
 {
 	char *ret;
 	int len;
 	int i,j;
 	if (NULL == in && NULL == out)
 		return 0;
 	ret = strchr(in, flag);
 	if (ret != NULL)
 	{
 	 len = ret - in;
 	 if (len >= 255)
 			return NULL;
 	 for(i=0,j=0;i<len;i++)
 	 {
 		if(in[i]!='"')
 			out[j++]=in[i];
 	 }
	 out[j] = '\0';
 	 return ret + 1;
 	}
 	return NULL;
  }

  gsm_sms_type_t gsm_check_sms_mode(char *smsbody)
  {
 	char tmp[256];
 	char *p;
 	char *ret;
 	gsm_sms_type_t mode = GSM_SMS_PDU;
 	if (NULL == smsbody) {
 		return GSM_SMS_UNKNOWN;
 	}
 	p = smsbody;
 	ret = gsm_get_dot_string(p, tmp, ':'); // +CMT:
 	if (!ret) {
 		return GSM_SMS_UNKNOWN;
 	}
 	if (strstr(tmp, "+CMT")==NULL) {
 		return GSM_SMS_UNKNOWN;
 	}
 	ret = gsm_get_dot_string(ret, tmp, ',');
 	if (!ret) {
 		return GSM_SMS_UNKNOWN;
 	}
 	if (ret[1] == '\"') {
 		return GSM_SMS_TXT;
 	}
  	return mode;
  }



 int gsm_handle_incoming_txt_sms(struct gsm_span *span, char *from, char *scts,char* timestamp, char *content)
 {
   gsm_log(GSM_LOG_DEBUG, "gsm_handle_incoming_txt_sms from=%s scts=%s timestamp=%s content=%s",from,scts,timestamp,content);
   struct gsm_sms_event_data data;
   if(!span || !from || !scts || !content)
       return 1;
   memset(&data, 0, sizeof(struct gsm_sms_event_data));
   memcpy(data.from, from, sizeof(data.from));
   sscanf(timestamp,"%d/%d/%d %d:%d:%d+%d",&data.timestamp.year, &data.timestamp.month, &data.timestamp.day,&data.timestamp.hour, &data.timestamp.minute, &data.timestamp.second, &data.timestamp.timezone);
   memcpy(data.content.message, content, GSM_MAX_CONTENT_LEN);
   data.gsm_sms_mode = 1;
   if(g_interface.gsm_sms_ind)
       g_interface.gsm_sms_ind(span->span_id, &data);
   return 0;
 }


 int  gsm_handle_incoming_pdu_sms(struct gsm_span *span, char *content, int len)
 {
   gsm_log(GSM_LOG_DEBUG, "gsm_handle_incoming_pdu_sms content=%s len=%d\n",content,len);
   struct gsm_sms_event_data data; //
   char pdu_msg[GSM_MAX_ATCMD_LEN] = {0};
   char raw_msg[GSM_MAX_ATCMD_LEN] = {0};
   char *p = NULL;
   int msg_len = 0;
   char numbuf[GSM_CALL_NUM_LEN] = {0};
   int num_len = 0;
   int from_len = 0;
   unsigned char first_oct = 0;
   unsigned char  tp_udl,udhl;
   unsigned char type,smsc_type;
   unsigned char plan,smsc_plan;
   char seq = 0;
   memset(&data, 0, sizeof(struct gsm_sms_event_data));
   if(!span || !content)
   {
       gsm_log(GSM_LOG_WARNING,"gsm_handle_incoming_pdu_sms but have no span or content\n");
       return 1;
   }
   memcpy(pdu_msg, content, sizeof(pdu_msg));
   p = pdu_msg;
   p = pdu_get_smsc_len(p, (char*)&num_len);
   if(num_len > 0)
   {
    p = pdu_get_addr_type(p, (char*)&smsc_type,(char*)&smsc_plan);
    p = pdu_get_addr_num(p, num_len, numbuf,0);
    gsm_reverse_num(numbuf, data.smsc ,1);
   }
   p = pdu_get_first_oct(p, &first_oct);
   p = pdu_get_oa_len(p,(char *)&from_len);
   if (from_len> 0)
   {
      p = pdu_get_addr_type(p, (char*)&type,(char*)&plan);
      p = pdu_get_addr_num(p, from_len, numbuf,1);
      gsm_reverse_num(numbuf, data.from, 1);
   }
   p = pdu_get_pid(p,(char *)&data.pid);
   p = pdu_decode_ud_dcs(p,  &data.dcs);
   p = pdu_get_timestamp(p, &data.timestamp);
   p = pdu_get_ud_len(p, &tp_udl);
   if(first_oct & 0x40)
   {
        gsm_log(GSM_LOG_WARNING, "recv newsms but have more head\n");
        p = pdu_decode_udh(p, &udhl, &seq);
   }
   msg_len =tp_udl- udhl ;
   gsm_string2byte(p, msg_len, (unsigned char*)raw_msg);
   switch( data.dcs )
   {
        case 0:  
            if(msg_len)
            gsm_to8Bit( (unsigned char*)raw_msg, (unsigned char*) data.content.message, msg_len, seq);//8bit now
            break;
        case 4:
            
            break;
        case 8:
            if(msg_len)
            {
             memset(data.content.message, 0 , sizeof(data.content.message));
              sms_decode_language(raw_msg, msg_len, data.content.message, 512, 0);
             msg_len = 10;
            }
            break;
        default:
            break;
   }
   data.content.charset = GSM_SMS_CONTENT_ENCRYPT_NONE;
   data.gsm_sms_mode = 0;
   if(g_interface.gsm_sms_ind)
       g_interface.gsm_sms_ind(span->span_id, &data);
   return 0;
  }
