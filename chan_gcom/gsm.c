#include <stdlib.h>
#include <stdio.h>

#include "libgsmcom.h"
#include "gsm_internal.h"
#include "gsm_sched.h"
#include "gsm_queue.h"

struct gsm_span g_spans[GSM_MAX_SPANS];
struct gsm_interface g_interface;
static int gsm_configure_span(int  span_id);
 int  gsm_sig_status_up(gsm_net_stat_t stat)
 {
        switch(stat)
        {
                case GSM_NET_NOT_REGISTERED:
                case GSM_NET_NOT_REGISTERED_SEARCHING:
                case GSM_NET_REGISTRATION_DENIED:
                case GSM_NET_UNKNOWN:
                        return 1;
                case GSM_NET_REGISTERED_HOME:
                case GSM_NET_REGISTERED_ROAMING:
                        return 0;
                case GSM_NET_INVALID:
                        return 1;
        }
        return 1;
 }

 struct gsm_span * gsm_get_span(unsigned int span_id)
 {
    struct gsm_span *span = NULL;
    if(span_id <= 0 || span_id > GSM_MAX_SPANS)
        return NULL;
    span = &g_spans[span_id-1];
    return span;
 }


 static int gsm_configure_span(int  span_id)
 {
     if(span_id <=0 || span_id > GSM_MAX_SPANS)
        return 1 ;
     struct gsm_span *span = gsm_get_span(span_id);
     if(!span)
           return 1;
     span->span_state = GSM_SPAN_STATE_START;
     span->calls.newstate = 0;
     span->smss.stats = 0;
     span->start_count = 0;
     span->heat = 0;
     span->event_call_state =0;
     span->load = 0;
    gsm_set_module_at_cmd(span, GSM_SET_ECHO);
    return 0;
 }

 void gsm_span_run( unsigned int  span_id, int load )
 {
	if(span_id <=0 || span_id > GSM_MAX_SPANS)
        return ;
     struct gsm_span *span = gsm_get_span(span_id);
     span->span_id = span_id;
     if((span->span_state == GSM_SPAN_STATE_INIT) || (load !=1))
     {
        gsm_log(GSM_LOG_DEBUG,"gsm span run and configure spanno=%d\n",span_id);
        gsm_configure_span(span_id);
     }
     else if( (span->span_state == GSM_SPAN_STATE_START)  || (span->span_state == GSM_SPAN_STATE_REGNET))
     {
	  span->heat++;
      if((span->heat == 6) &&(span->start_count <=50))
	  {
	    span->start_count++;
        span->heat=0;
        if(span->module.state == GSM_SET_ECHO)
	       gsm_set_module_at_cmd(span, GSM_SET_ECHO);
        else if((span->span_state == GSM_SPAN_STATE_REGNET))
           gsm_set_module_at_cmd(span, GSM_GET_CREG);
 	  }
    }
    else if(span->span_state == GSM_SPAN_STATE_RUNNING)
    {
        span->heat++;
        if((strlen(span->module.sim_info.smsc) < 3)) //must restart
           span->span_state = GSM_SPAN_STATE_INIT;

    }
    if(span && ( span->span_state >= GSM_SPAN_STATE_START ))
    {
         gsm_span_do_events(span);
         gsm_span_do_sched(span);
    }
 }

 static int sig_gsm_prepare_queue(struct gsm_span *span)
 {
     int res = 0;
     res = GSM_INIT_EVENT_QUEUE(&span->event_queue);
     if(res)
       return res;
     res = GSM_INIT_SCHED_QUEUE(&span->sched_queue);
     if(res)
       return res;
     return res;
 }

 static int gsm_span_load_config(struct gsm_cfg *config, int span_id)
 {
    struct gsm_span *span = NULL;
    if(!config)
        return 1;
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS))
        return 1;
    span = (struct gsm_span *)gsm_get_span(span_id);//&g_spans[span_id -1];
    if(!span)
        return 1;
    memcpy(&span->config, config, sizeof(struct gsm_cfg));
    return 0;
 }


 int gsm_span_schedule_next(unsigned int span_id)
 {
        struct gsm_span *span = NULL;
        int res = 0;
        span = gsm_get_span(span_id);
        if(!span)
            return -1;
        res = gsm_sched_wait(&span->sched_queue);
        return res;
 }

 static int gsm_interface_register(gsm_interface_t *gsm_interface)
 {
    if(!gsm_interface)
        return 1;
    if(!gsm_interface->gsm_malloc ||
        !gsm_interface->gsm_calloc ||
        !gsm_interface->gsm_free ||
        !gsm_interface->gsm_span_write )
        return 1;
    memcpy(&g_interface, gsm_interface, sizeof(struct gsm_interface));
    return 0;
 }

 void * gsm_malloc(unsigned int size)
 {
    if(g_interface.gsm_malloc)
        return g_interface.gsm_malloc(size);
    else
        return NULL;
 }

 void * gsm_calloc(unsigned int mem_num, unsigned int size)
 {
    if(g_interface.gsm_calloc)
        return g_interface.gsm_calloc(mem_num, size);
    else 
        return NULL;
 }

 void  gsm_free(void *p)
 {
    if(g_interface.gsm_free)
        return g_interface.gsm_free(p);
    else 
    {
         if(p)
            free(p);
         p = NULL;
    }
 }

 int gsm_span_write(unsigned int span_id, void *buf, int len)
 {
    return g_interface.gsm_span_write(span_id, buf, len);
 }
 
 int gsm_sms_ind(unsigned int span_id, struct gsm_sms_event_data *event)
 {
    if(g_interface.gsm_sms_ind)
        return g_interface.gsm_sms_ind(span_id, event);
    return 1;
 }

 int gsm_sms_report(unsigned int span_id, struct gsm_sms_event_data *sms)
 {
    if(g_interface.gsm_sms_report)
        return g_interface.gsm_sms_report(span_id,  sms);
    return 1;
 }

 int gsm_call_ind(unsigned int span_id, struct gsm_call_data *event)
 {
    if(g_interface.gsm_call_ind)
        return g_interface.gsm_call_ind(span_id, event);
    return 1;
 }

 int gsm_call_report(unsigned int span_id, unsigned int call_id, int result)
 {
    if(g_interface.gsm_call_report)
        return g_interface.gsm_call_report(span_id, call_id,  result);
    return 1;
 }

 int gsm_call_hangup_ind(unsigned int span_id,  int cause, unsigned char callid)
 {
    if(g_interface.gsm_call_hangup_ind)
        return g_interface.gsm_call_hangup_ind(span_id, cause, callid);
    return 1;
 }

 int gsm_call_hangup_cfm(unsigned int span_id, unsigned int call_id, int result)
 {
    if(g_interface.gsm_call_hangup_cfm)
        return g_interface.gsm_call_hangup_cfm(span_id, call_id,  result);
    return 1;
 }

 int init_lib_gsm_stack(gsm_interface_t *gsm_interface)
 {
    int res = 0;
    if(!gsm_interface)
        return 1;
    res = gsm_interface_register( gsm_interface );
    if(res)
        return 1;
    memset(g_spans, 0, GSM_MAX_SPANS * sizeof(struct gsm_span ));//jwj add 2012-6-12
    return res;
 }

 int gsm_req_span_siminfo_smsc(int span_id, char *smsc, int len)
 {
    struct gsm_span *span_t = NULL;
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS) )
        return 1;
    span_t = gsm_get_span( span_id);
    if(!span_t)
       return 1;
   if(strlen(span_t->module.sim_info.smsc) < 2)
      memcpy(smsc, "unknown", len-1);
   else
      memcpy(smsc, span_t->module.sim_info.smsc, len-1);
   return 0;
 }
 
 int gsm_req_span_siminfo_imei(int span_id, char *imei, int len)
 {
    struct gsm_span *span_t = NULL;
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS) )
        return 1;
     span_t = gsm_get_span( span_id);
     if(!span_t)
        return 1;
     if(strlen(span_t->module.sim_info.imei) < 2)
        memcpy(imei, "unknown", len-1);
     else
        memcpy(imei, span_t->module.sim_info.imei, len-1);
    return 0;
 }
 
 int gsm_req_span_siminfo_imsi(int span_id, char *imsi, int len)
 {
    struct gsm_span *span_t = NULL;
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS) )
        return 1;
     span_t = gsm_get_span( span_id);
     if(!span_t)
        return 1;
     if(strlen(span_t->module.sim_info.imsi) < 2)
        memcpy(imsi, "unknown", len-1);
     else
        memcpy(imsi, span_t->module.sim_info.imsi, len-1);
    return 0;
 }
 
 int gsm_req_span_siminfo_version(int span_id, char *version, int len)
 {
    struct gsm_span *span_t = NULL;
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS) )
        return 1;
     span_t = gsm_get_span( span_id);
     if(!span_t)
        return 1;
     if(strlen(span_t->module.sim_info.version) < 2)
        memcpy(version, "unknown", len-1);
     else
        memcpy(version, span_t->module.sim_info.version, len-1);
    return 0;
 }

 int gsm_req_span_csq(int span, int *csq)
 {
    struct gsm_span *span_t = NULL;
    if( (span <=0) || (span > GSM_MAX_SPANS) )
        return 1;
     span_t = gsm_get_span( span);
     if(!span_t)
        return 1;
     *csq = span_t->csq;
     return 0;
 }

 int gsm_req_span_stats(int span, char *status, char *active,int len)
 {
    struct gsm_span *span_t = NULL;
    if((span <=0) || (span > GSM_MAX_SPANS))
        return 1;
    span_t = gsm_get_span( span);
    if(!span_t)
        return 1;
    switch(span_t->span_state)
    {
        case GSM_SPAN_STATE_INIT:
            memcpy(status, "DOWN",len-1);
            memcpy(active, "INACTIVE",len-1);
            break;
        case GSM_SPAN_STATE_REGNET:
            memcpy(status, "REGISTER ",len-1);
            memcpy(active, "ACTIVE",len-1);
           break;
        case GSM_SPAN_STATE_START:
                memcpy(status, "UNREGISTER ",len-1);
                if(span_t->load == 1)
                    memcpy(active, "ACTIVE",len-1);
                else
                     memcpy(active, "INACTIVE",len-1);
                break;
        case GSM_SPAN_STATE_RUNNING:
            memcpy(active, "ACTIVE",len-1);
            if(span_t->calls.newstate > 0)
                memcpy(status, "INCALL ",len-1);
            else if(span_t->smss.stats > 0)
                memcpy(status, "INSMS ",len-1);
            else
                memcpy(status, "UP ",len-1);
            break;
        default:
            break;
     }
     return 0;
 }

 int gsm_set_span_status(struct gsm_span *span, int status)
 {
    if(!span)
        return 1;
    span->span_state = status;
    switch(span->span_state)
    {
        case GSM_SPAN_STATE_START:
            gsm_set_module_at_cmd(span, GSM_GET_CGMR);
            break;  
        default:
            break;
    }
    return 0;
 }

 int  gsm_switch_start_state_cmd(struct gsm_span *span, int cmd)
 {
    if(!span)
        return 1;
    switch(cmd)
    {
         case GSM_SET_ECHO:
            gsm_set_module_at_cmd(span, GSM_SET_CLIP);
            break;
         case GSM_SET_CLIP:
             gsm_set_module_at_cmd(span, GSM_SET_ATX);
             break;
         case GSM_SET_ATX:
             gsm_set_module_at_cmd(span, GSM_SET_QAUDCH);
             break;
         case GSM_SET_QAUDCH:
              gsm_set_module_at_cmd(span, GSM_SET_QMOSTAT);
             break;
         case GSM_SET_QMOSTAT:
             gsm_set_module_at_cmd(span, GSM_SET_QSIMDET);
             break;
         case GSM_SET_QSIMDET:
             gsm_set_module_at_cmd(span, GSM_SET_QSIDET);
             break;
         case GSM_SET_QSIDET:
             gsm_set_module_at_cmd(span, GSM_SET_QECHO);
             break;
        case GSM_SET_QECHO:
             gsm_set_module_at_cmd(span, GSM_SET_CNMI);
             break;
        case GSM_SET_CNMI:
             gsm_set_module_at_cmd(span, GSM_SET_CMEE);
             break;
        case GSM_SET_CMEE:
             gsm_set_module_at_cmd(span, GSM_SET_CREG);
             break;
        case GSM_GET_CGMR:
             gsm_set_module_at_cmd(span, GSM_GET_CGSN);
             break;
        case GSM_GET_CGSN:
             gsm_set_module_at_cmd(span, GSM_GET_CIMI);
             break;
        case GSM_GET_CIMI:
             gsm_set_module_at_cmd(span, GSM_GET_CSCA);
             break;
        case GSM_GET_CSCA:
             gsm_set_module_at_cmd(span, GSM_GET_CPIN);
             break;
        default:
             break;
    }
    return 0;
  }

 //AT+QSIDIT=0
//AT+QAPS=1,4,0,"0.253.32512.31.57351.31.400.0.80.4325.99.0.32513.0.0.8192"
 int gsm_set_module_at_cmd(struct gsm_span *span, int cmd)
 {
    int res   =  0;
    if(!span)
        return 1;
    span->module.state = cmd;
    switch(cmd)
    {
        case GSM_SET_ECHO:
            res = gsm_write_cmd(span, "ATE0\r\n", strlen("ATE0\r\n"));
            break;
        case GSM_SET_CLIP:
            res = gsm_write_cmd(span, "AT+CLIP=1\r\n", strlen("AT+CLIP=1\r\n"));
            break;
        case GSM_SET_ATX:
            res = gsm_write_cmd(span, "ATX4\r\n", strlen("ATX4\r\n"));
            break;
        case GSM_SET_QAUDCH:
            res = gsm_write_cmd(span, "AT+QAUDCH=0\r\n", strlen("AT+QAUDCH=0\r\n"));
            break;
        case GSM_SET_QMOSTAT:
            res = gsm_write_cmd(span, "AT+QMOSTAT=1\r\n", strlen("AT+QMOSTAT=1\r\n"));
            break;
        case GSM_SET_QSIMDET:
            res = gsm_write_cmd(span, "AT+QSIMDET=1,1\r\n", strlen("AT+QSIMDET=1,1\r\n"));
            break;
        case GSM_SET_QSIDET:
            res = gsm_write_cmd(span, "AT+QSIDIT=0\r\n", strlen("AT+QSIDIT=0\r\n"));
            break;
        case GSM_SET_QECHO:
            res = gsm_write_cmd(span, "AT+QAPS=1,4,0,\"0.253.32512.31.57351.31.400.0.80.4325.99.0.32513.0.0.8192\"\r\n", strlen("AT+QAPS=1,4,0,\"0.253.32512.31.57351.31.400.0.80.4325.99.0.32513.0.0.8192\"\r\n"));
            break;
        case GSM_SET_CNMI:
            res = gsm_write_cmd(span, "AT+CNMI=2,2,0,0,0\r\n", strlen("AT+CNMI=2,2,0,0,0\r\n"));
            break;
        case GSM_SET_CMEE:
            res = gsm_write_cmd(span, "AT+CMEE=2\r\n", strlen("AT+CMEE=2\r\n"));
            break;
        case GSM_SET_CREG:
            res = gsm_write_cmd(span, "AT+CREG=1\r\n", strlen("AT+CREG=1\r\n"));
            break;
        case GSM_GET_CPIN:
            if(span->span_state == GSM_SPAN_STATE_START)
            {
                span->span_state = GSM_SPAN_STATE_REGNET;
                span->load = 1;
            }
            res = gsm_write_cmd(span, "AT+CPIN?\r\n", strlen("AT+CPIN?\r\n"));
            break;
        case GSM_SIM_PIN_REQ:
            {
              char CPIN_CMD[30] = {0};
              if(strlen(span->config.pinnum) > 2)
              {
                snprintf(CPIN_CMD, sizeof(CPIN_CMD), "AT+CPIN=%s\r\n",span->config.pinnum);
                res = gsm_write_cmd(span, CPIN_CMD, strlen(CPIN_CMD));
              }
              else
                gsm_log(GSM_LOG_WARNING, "the gsm spanno=%d is req sim pin but have no\n",span->span_id);
            }
            break;
        case GSM_GET_CGMR:
            res = gsm_write_cmd(span, "AT+CGMR\r\n", strlen("AT+CGMR\r\n"));
            break;   
        case GSM_GET_CGSN:
            res = gsm_write_cmd(span, "AT+CGSN\r\n", strlen("AT+CGSN\r\n"));
            break;
        case GSM_GET_CIMI:
            res = gsm_write_cmd(span, "AT+CIMI\r\n", strlen("AT+CIMI\r\n"));
            break;
        case GSM_GET_CREG:
            res = gsm_write_cmd(span, "AT+CREG?\r\n", strlen("AT+CREG?\r\n"));
            break;
        case GSM_GET_CSCA:
            res = gsm_write_cmd(span, "AT+CSCA?\r\n", strlen("AT+CSCA?\r\n"));
            break;
        case GSM_GET_CSQ:
            res = gsm_write_cmd(span, "AT+CSQ\r\n", strlen("AT+CSQ\r\n"));
            break;
        case GSM_SET_ATW:
            res = gsm_write_cmd(span, "AT&W\r\n", strlen("AT&W\r\n"));
            break;
        default:
            break;
    }
    return 0;
 }

 int init_sig_gsm_span(struct gsm_cfg *config, unsigned int span_id)
 {
    int res = 0;
    struct gsm_span *span = NULL;
    if(!config)
        return 1;
    if( (span_id <=0) || (span_id > GSM_MAX_SPANS) )
    {
        gsm_log(GSM_LOG_ERROR, "init sig gsm span id =%d is error\n",span_id);
        return 1;
    }
    res = gsm_span_load_config(config, span_id);
    if(res)
    {
        gsm_log(GSM_LOG_ERROR, "gsm_span_load_config is error\n");
        return res;
    }
    span = gsm_get_span( span_id);
    if(!span)
        return 1;
    span->span_id = span_id;
    res = sig_gsm_prepare_queue(span);
    return res;
 }

