#include <stdlib.h>
#include <stdio.h>
#include "gsm_queue.h"
#include "libgsmcom.h"
#include "gsm_internal.h"



 int  gsm_send_dtmf(unsigned char span_id,   char digit)
 {
	char cmd[50] = {0};
    struct gsm_span *span = NULL;
    if( (span_id <= 0)  || (span_id > GSM_MAX_SPANS))
      return 1;
    span = gsm_get_span(span_id);
    if(!span)
        return 1;
    snprintf(cmd, sizeof(cmd)-1, "AT+VTS=%c\r\n",digit);
   gsm_log(GSM_LOG_DEBUG, "gsm_send_dtmf spanno=%d dtmf=%s\n",span_id,cmd);
	return gsm_cmd_req(span_id, cmd);
 }

 int gsm_cmd_req(unsigned char  span_id, const char *at_cmd)
 {
        struct gsm_span *span = NULL;
        int res = 0;
        char buf[512] = {0};
        span = gsm_get_span(span_id);
        if(!span)
            return 1;
        if(span->span_state < GSM_SPAN_STATE_START)
         return 1;
        snprintf(buf, sizeof(buf),"%s%s",at_cmd,GSM_CMD_END);
        if(span->smss.stats > 0 ||
            ((span->calls.newstate != GSM_CALL_STATE_UP)    &&
            (span->calls.newstate > 0) &&
			(strncasecmp(at_cmd,"AT+CLCC",strlen("AT+CLCC")))))
       {
            gsm_log(GSM_LOG_DEBUG, "gsm module spanno=%d is busy\n",span->span_id);
            return 1;
       }
       res = gsm_write_cmd(span, buf, strlen(buf));
       return 0;
 }
