
#include "asterisk.h"

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/say.h"
#include "asterisk/manager.h"
#include "asterisk/astdb.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/transcap.h"
#include "asterisk/features.h"
#include "asterisk/extconf.h"
#include "asterisk/stringfields.h"
#include "asterisk/ast_version.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>


#include <libgsmcom.h>
#include "sig_gsm.h"

void sig_gsm_log(unsigned char loglevel, char *fmt, ...)
__attribute__((format (printf, 2, 0)));
void sig_gsm_log_span(unsigned char span_id, unsigned char loglevel, char *fmt, ...)
 __attribute__((format (printf, 3, 0)));

int sig_gsm_span_write( unsigned int span_id, void *buf, int len );
int sig_gsm_sms_ind( unsigned int span_id, struct gsm_sms_event_data *event);
int sig_gsm_sms_report( unsigned int span_id, struct gsm_sms_event_data *sms );
void *sig_gsm_malloc(unsigned int size);
void *sig_gsm_calloc(unsigned int mem_num, unsigned int size);
void sig_gsm_free(void *p);

static void sig_gsm_unlock_private(struct sig_gsm_chan *p);
static void sig_gsm_lock_private(struct sig_gsm_chan *p);
static void sig_gsm_lock_owner(struct sig_gsm_span *span);
static void sig_gsm_handle_exception(struct sig_gsm_span *gsm);
static void sig_gsm_handle_data(struct sig_gsm_span *gsm);



#define DCHAN_NOTINALARM  (1 << 0)
#define DCHAN_UP          (1 << 1)
#define DCHAN_AVAILABLE	(DCHAN_NOTINALARM | DCHAN_UP)

struct sig_gsm_span **gsm_spans;

#define SIG_GSM_DEADLOCK_AVOIDANCE(p) \
	do { \
		sig_gsm_unlock_private(p); \
		usleep(1); \
		sig_gsm_lock_private(p); \
	} while (0)


void init_gsm_spans(struct sig_gsm_span *span)
{
    if(!span)
        return ;
    if(span->span_id >= GSM_MAX_SPANS)
        return;
    gsm_spans[span->span_id] = span;
}


static void sig_gsm_unlock_private(struct sig_gsm_chan *p)
{
	if (p->calls->unlock_private)
		p->calls->unlock_private(p->chan_pvt);
}

static void sig_gsm_lock_private(struct sig_gsm_chan *p)
{
	if (p->calls->lock_private)
		p->calls->lock_private(p->chan_pvt);
}

void gsm_event_alarm(struct sig_gsm_span *gsm_span)
{
    gsm_span->dchanavail &= ~(DCHAN_NOTINALARM | DCHAN_UP);
    if (gsm_span->pvts->calls->set_alarm)
        gsm_span->pvts->calls->set_alarm(gsm_span->pvts->chan_pvt, 1);
}

void gsm_event_noalarm(struct sig_gsm_span *gsm_span)
{
     gsm_span->dchanavail |= DCHAN_NOTINALARM;
     if (gsm_span->pvts->calls->set_alarm)
              gsm_span->pvts->calls->set_alarm(gsm_span->pvts->chan_pvt, 0);
}

int sig_gsm_get_span_stats(int span, char *status, char *active, int len)
{
    if((span <= 0 ) || (span >= GSM_MAX_SPANS))
            return 1;
    gsm_req_span_stats(span, status, active, len);
    return 0;
}

int sig_gsm_get_span_csq(int span, int *csq)
{
    if((span <= 0 ) || (span >= GSM_MAX_SPANS))
            return 1;
    gsm_req_span_csq(span, csq);
    return 0;
}

int sig_gsm_get_span_siminfo_imei(int span_id, char *imei, int len)
{
    if((span_id <= 0 ) || (span_id >= GSM_MAX_SPANS))
      return 1;
    gsm_req_span_siminfo_imei(span_id, imei, len);
    return 0;
}

int sig_gsm_get_span_siminfo_imsi(int span_id, char *imsi, int len)
{
    if((span_id <= 0 ) || (span_id >= GSM_MAX_SPANS))
       return 1;
    gsm_req_span_siminfo_imsi(span_id, imsi, len);
    return 0;
}

int sig_gsm_get_span_siminfo_version(int span_id, char *version, int len)
{
    if((span_id <= 0 ) || (span_id >= GSM_MAX_SPANS))
       return 1;
    gsm_req_span_siminfo_version(span_id, version, len);
    return 0;
}

int  sig_gsm_get_span_siminfo_smsc(int span_id, char *smsc, int len)
{
    if((span_id <= 0 ) || (span_id >= GSM_MAX_SPANS))
       return 1;
    gsm_req_span_siminfo_smsc(span_id, smsc, len);
    return 0;
}

int gsm_str_charset2int(const char *str_charset)
{
    int charset;
    if(!strcasecmp(str_charset,"ascii"))
      charset = GSM_SMS_CONTENT_CHARSET_ASCII;
    else if(!strcasecmp(str_charset,"utf-8"))
     charset = GSM_SMS_CONTENT_CHARSET_UTF8;
    else
     charset = GSM_SMS_CONTENT_CHARSET_INVALID;
    return charset;
}

int gsm_str_smsmode2int(const char *str_mode)
{
    int mode;
    if(!strcasecmp(str_mode, "text"))
        mode = GSM_SMS_TXT;
    else
        mode = GSM_SMS_PDU;
    return mode;
}

int gsm_str_encrypt2int(const char *str_encrypt)
{
    int encrypt;
    if(!strcasecmp(str_encrypt, "ENCRYPT_NONE"))
       encrypt =  GSM_SMS_CONTENT_ENCRYPT_NONE ;
    else if(!strcasecmp(str_encrypt, "ENCRYPT_BASE64"))
       encrypt = GSM_SMS_CONTENT_ENCRYPT_BASE64;
    else
       encrypt = GSM_SMS_CONTENT_ENCRYPT_BASE64;
    return encrypt;  
}


 int  sig_gsm_decode_pdu_content(unsigned char *out_data, int *out_data_len, char *in_content,int charset)
 {
    char *buf = NULL;
    size_t out_len = GSM_MAX_CONTENT_LEN;
    size_t in_len;
    iconv_t cd;
    buf = in_content;//
    in_len = strlen(in_content);
    switch (charset)
    {
       case GSM_SMS_CONTENT_CHARSET_GB18030:
          cd = iconv_open("UTF-8", "GB18030");
          break;
       default:
         cd = iconv_open("UTF-8", "GB18030");
         break;
    }
    if (cd < 0)
    {
      ast_log(LOG_WARNING, " gsm_decode_pdu_content iconv open fail\n");
      return 1;
    }
    if (iconv(cd, &buf, &in_len, (char**)&out_data, &out_len) < 0)
    {
      ast_log(LOG_WARNING, " gsm_decode_pdu_content iconv  fail\n");
      iconv_close(cd);
      return 1;
    }
    *out_data_len = GSM_MAX_CONTENT_LEN - out_len;
    *out_data = '\0';
    iconv_close(cd);
    return 0;
 }



 int sig_gsm_EncodeUCS2( unsigned char* pSrc, unsigned char* pDst, size_t  nSrcLength, int *out_len)
 {
    int pLen = 0;
    size_t  outlen = 1040;
    iconv_t cd;
    unsigned char **pin = &pSrc;
    unsigned char *pout = NULL;
    pout = pDst;//
    if(!pSrc )
        return 1;
    cd = iconv_open( "UCS-2BE","UTF-8");
    if (cd==0)
    {
        ast_log(LOG_WARNING, "gsmEncodeUCS2 iconv open fail\n");
        return 1;
    }
    if (iconv(cd, (char**)pin, &nSrcLength, (char**)&pDst, &outlen)==-1) 
    {
        ast_log(LOG_WARNING, "gsmEncodeUCS2 iconv fail errno=%d\n",errno);
        iconv_close(cd);
 	    return 1;
    }
    pLen = 1040 - outlen;
    *out_len = pLen ;
    *pDst = '\0';
    iconv_close(cd);
    return 0;
}

 int sig_gsm_digit_begin(struct sig_gsm_chan *pvt, struct ast_channel *ast, char digit)
 {
  	struct sig_gsm_span *span = NULL;
	int count = 0;
	span = pvt->gsm;
	if(span==NULL)
        return 1;
	/* Disable DTMF detection while we play DTMF because the GSM module will play back some sort of feedback tone */
	if (count == 1) {
		char x = 0;
		ast_channel_setoption(span->pvts->owner, AST_OPTION_DIGIT_DETECT, &x, sizeof(char), 0);
	}
	gsm_send_dtmf(span->span_id, digit);
    return 0;
}

void sig_gsm_exec_at(struct sig_gsm_span *gsm, const char *at_cmd)
{
       ast_log(LOG_WARNING,"gsm exec at spanid=%d,cmd=%s\n",gsm->span_id,at_cmd);  
       gsm_cmd_req(gsm->span_id, at_cmd);
}

int sig_gsm_send_sms(unsigned int span, struct gsm_sms_event_data *event)
{

    ast_log(LOG_DEBUG,"sig_gsm_send_sms\n");
    struct gsm_sms_event_data sms_event;
    unsigned int span_id = span ;
    if( !span || !event)
    {
        ast_log(LOG_WARNING,"sig_gsm_send_sms no span return\n");
        return 1;
    }
    memset(&sms_event, 0, sizeof(struct gsm_sms_event_data));
    memcpy(&sms_event, event, sizeof(struct gsm_sms_event_data));
    if (gsm_sms_req(span_id, &sms_event))
    {
        ast_log(LOG_WARNING," gsm_sms_req no  return\n");
        return 1;
    }
    ast_log(LOG_ERROR,"AK sig_gsm_send_sms end\n");
    return 0;
}

void gsm_span_process_read(unsigned char span_id, void *data, int  len)
{

    char cmd[GSM_MAX_ATCMD_LEN] = {0};
    char *p = NULL;
    if((span_id <= 0 ) || (span_id >= GSM_MAX_SPANS))
      return;
    if(len >= GSM_MAX_ATCMD_LEN)
      return;
    for (p = strtok(data, "\n"); p; p = strtok(NULL, "\n"))
    {
      sprintf(cmd,"%s\n",p);
      gsm_read_events( span_id, (const char*)cmd, strlen(cmd));
    }
 }

 void sig_gsm_init_gsm(struct sig_gsm_span *gsm)
 {
	memset(gsm, 0, sizeof(*gsm));
	gsm->master = AST_PTHREADT_NULL;
    gsm->mon = AST_PTHREADT_NULL;
    gsm->fd = -1;
 }

 static void gsm_set_new_owner(struct sig_gsm_chan *p, struct ast_channel *new_owner)
 {
	p->owner = new_owner;
	if (p->calls->set_new_owner)
		p->calls->set_new_owner(p->chan_pvt, new_owner);
 }

 static struct ast_channel * gsm_new_ast_channel(struct sig_gsm_chan *p, int state, int startpbx, enum gsm_sub sub, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor,struct ast_callid *callid)
 {
    ast_log(LOG_DEBUG,"gsm_new_ast_channel startpbx=%d\n",startpbx);
    struct ast_channel *c = NULL;
	if (!p || !p->calls->new_ast_channel)
		return NULL;
	c = p->calls->new_ast_channel(p->chan_pvt, state, startpbx, sub,assignedids, requestor,callid);
	if (c)
		ast_channel_call_forward_set(c,p->call_forward);
    else
        return NULL;
	p->subs[sub].owner = c;
	if (!p->owner)
		gsm_set_new_owner(p, c);
	return c;
}

 struct sig_gsm_chan *sig_gsm_chan_new(void *pvt_data, struct sig_gsm_callback *callback, struct sig_gsm_span *gsm, int logicalspan, int channo, int trunkgroup)
 {
	struct sig_gsm_chan *p;
	p = ast_calloc(1, sizeof(*p));
	if (!p)
		return p;
	p->logicalspan = logicalspan;
	p->gsmoffset = channo;
	p->mastertrunkgroup = trunkgroup;
	p->calls = callback;
	p->chan_pvt = pvt_data;
	p->gsm = gsm;
	return p;
}

 int sig_gsm_call(struct sig_gsm_chan *p, struct ast_channel *ast, char *rdest)
 {
     ast_log(LOG_DEBUG,"sig_gsm_call\n");
     int i = 1;
     int j =0;
     struct sig_gsm_span *span = NULL;
     if(!p || !ast)
            return -1;
     span = p->gsm;
     sig_gsm_lock_private(span->pvts);
	 if (span)
	 {
	   char *c;
	   c = strchr(rdest, '/');
	   if (c)
		 c++;
	   else
		 c = "";
	   if (strlen(c) < p->stripmsd)
	   {
           ast_log(LOG_WARNING,"the number %s is short than %d\n",c,p->stripmsd);
           sig_gsm_unlock_private(span->pvts);
           return -1;
	   }
       if( span->pvts->subs[GSM_SUB_REAL].allocd)
       {
           ast_log(LOG_WARNING,"the sub_Real allocd is exist\n");
           sig_gsm_unlock_private(span->pvts);
           return -1;
        }
        else
        {
            for (j = 0; j < ARRAY_LEN(span->pvts->subs); j++)
            {
               if (span->pvts->subs[j].allocd)
               {
                   if (span->pvts->subs[j].call_id == i)
                   {
                       i++;
                       continue;
                   }
               }
           }

        }
       struct gsm_call_data calldata;
       char *dial_cmd = NULL;
       memset(&calldata, 0, sizeof(struct gsm_call_data));
       calldata.span_id = p->gsm->span_id;
       memcpy(calldata.called_num, c, sizeof(calldata.called_num));
       calldata.direct = GCOM_DIRECTION_OUTGOING;
       calldata.type = GSM_CALL_TYPE_VOICE;
       calldata.switchtype = p->gsm->conf.switchtype;
       calldata.callid = i;
       span->pvts->remotehangup = 0;
       span->pvts->subs[GSM_SUB_REAL].allocd = 1;
       span->pvts->subs[GSM_SUB_REAL].owner= ast;
       span->pvts->subs[GSM_SUB_REAL].call_id= i;
       span->pvts->owner= ast;
       ast_log(LOG_DEBUG,"module take gsm_call_req,rdest=%s dialcmd=%s\n",rdest,dial_cmd);
       if(!ast_channel_trylock(span->pvts->owner))
             ast_log(LOG_WARNING," first gsm_call_req lock chan ok\n");
       ast_channel_unlock(span->pvts->owner);
       j = gsm_call_req(p->gsm->span_id, &calldata);
       if(j)
       {
         sig_gsm_unlock_private(span->pvts);
         return -1;
       }
       ast_setstate(ast, AST_STATE_DIALING);
       sig_gsm_unlock_private(span->pvts);
	}
    return 0;
 }


 static void sig_gsm_open_media(struct sig_gsm_chan *p)
 {
	if (p->calls->open_media)
		p->calls->open_media(p->chan_pvt);
 }

 static int sig_gsm_set_echocanceller(struct sig_gsm_chan *p, int enable)
 {
	if (p->calls->set_echocanceller)
		return p->calls->set_echocanceller(p->chan_pvt, enable);
	else
		return -1;
 }

 int sig_gsm_answer(struct sig_gsm_chan *p, struct ast_channel *ast)
 {
    ast_log(LOG_DEBUG,"sig_gsm_answer\n");
    int res = 0;
    sig_gsm_open_media(p);
    res = gsm_call_cfm(p->gsm->span_id, p->subs[GSM_SUB_REAL].call_id);
    ast_setstate(ast, AST_STATE_UP);
    return res;
 }

 int sig_gsm_hangup(struct sig_gsm_chan *p, struct ast_channel *ast)
{
    int res = 0;
    struct sig_gsm_span *gsm = NULL;
    if(!p)
        return -1;
    gsm = p->gsm;
    ast_log(LOG_DEBUG, "Span %d: Call Hung up\n", gsm->span_id);
    if (!gsm->pvts->subs[GSM_SUB_REAL].allocd)
    {
      ast_log(LOG_NOTICE, "Span %d: Call already hung-up\n", gsm->span_id );
      return -1;
    }
    if (gsm->pvts->remotehangup)
    {
        ast_log(LOG_WARNING," sig_gsm_hangup but remotehangup\n");
        gsm_hangup_cfm(gsm->span_id, gsm->pvts->subs[GSM_SUB_REAL].call_id);
        memset(&gsm->pvts->subs[GSM_SUB_REAL], 0, sizeof(gsm->pvts->subs[0]));
        gsm->pvts->owner = NULL;
    }
    else
    {
         ast_log(LOG_WARNING,"sig_gsm_hangup but localhangup\n");
         gsm_hangup_req(gsm->span_id, gsm->pvts->subs[GSM_SUB_REAL].call_id);
    }
    ast_log(LOG_WARNING,"sig_gsm_hangup DONE \n"); 
    return res;
}

//call the same as fxo: gcom_request--> sig_gsm_request 
 struct ast_channel * sig_gsm_request(struct sig_gsm_chan *p, int *callwait, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
 {
    ast_log(LOG_DEBUG,"sig_gsm_request\n");
    struct ast_channel *ast = NULL;
    struct sig_gsm_span *gsm = p->gsm;
    char status[GSM_SPAN_STAUTS_LEN] = {0};
    char active[GSM_SPAN_STAUTS_LEN] = {0};
 	*callwait = (p->owner != NULL);
    if(!gsm)
      return NULL;
    gsm_req_span_stats(gsm->span_id, status, active, GSM_SPAN_STAUTS_LEN);
    ast_log(LOG_DEBUG,"sig_gsm_request status=%s\n",status);
    if((strncasecmp(status,"UP",strlen("UP"))) ||  (gsm->pvts->subs[GSM_SUB_REAL].allocd) ||    (p->owner))
    {
       gsm_cmd_req(gsm->span_id, "AT+CLCC\r\n");
 	   return NULL;
	}
	p->outgoing = 1;
	ast = gsm_new_ast_channel(p, AST_STATE_RESERVED, 0,p->owner ? GSM_SUB_CALLWAIT : GSM_SUB_REAL,assignedids, requestor,NULL);
	if (!ast)
		p->outgoing = 0;
	return ast;
 }

 int sig_gsm_span_write( unsigned int span_id, void *atbuf, int len )
 {
    int res = 0;
    struct sig_gsm_span *gsm = gsm_spans[span_id];
    char buf[len+2];
    if(!atbuf || !gsm)
    {
        ast_log(LOG_WARNING,"sig gsm span write atbuf or gsm fail spanid=%d\n",span_id);
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    memcpy(buf, atbuf, len);
    memcpy(gsm->last_cmd,atbuf, sizeof(gsm->last_cmd));
    len += 2;
    res = write(gsm->fd, buf, len);
    if (res < 0)
        ast_log(LOG_ERROR, "Span %d:Write failed: %s\n", gsm->span_id, strerror(errno));
    else  if (res != len)
        ast_log(LOG_ERROR, "Span %d:Short write %d (len:%d)\n", gsm->span_id + 1, res, len);
    return res;
 }


 static void gsm_queue_frame(struct sig_gsm_span *span, struct ast_frame *frame)
 {
     if(!span || !span->pvts || !span->pvts->owner)
            return ;
     sig_gsm_lock_owner(span);
     if (span->pvts->owner)
     {
         ast_queue_frame(span->pvts->owner, frame);
         ast_channel_unlock(span->pvts->owner);
     }
     else
        ast_log(LOG_WARNING,"gsm_queue_frame owner is null\n");
 }


 static void gsm_queue_control(struct sig_gsm_span *span, int subclass)
 {
       struct ast_frame f = {AST_FRAME_CONTROL, };
       struct sig_gsm_chan *p = span->pvts;
       if(!span || !p)
            return;
       if (p->calls->queue_control)
               p->calls->queue_control(p->chan_pvt, subclass);
      f.subclass.integer = subclass;
      gsm_queue_frame(span, &f);
 }

  static void sig_gsm_deadlock_avoidance_private(struct sig_gsm_chan *p)
  {
           /* Fallback to the old way if callback not present. */
        if (p->calls->deadlock_avoidance_private)
                p->calls->deadlock_avoidance_private(p->chan_pvt);
        else
           SIG_GSM_DEADLOCK_AVOIDANCE(p);
 }


 static void sig_gsm_lock_owner(struct sig_gsm_span *span)
 {
       for (;;)
       {
               if (!span->pvts->owner) /* There is no owner lock to get. */
                       break;
               if (!ast_channel_trylock(span->pvts->owner)) /* We got the lock */
                      break;
               ast_mutex_unlock(&span->lock);
               //SIG_GSM_DEADLOCK_AVOIDANCE(span->pvts);
               sig_gsm_deadlock_avoidance_private(span->pvts);
               ast_mutex_lock(&span->lock);
       }
 }


 static void sig_gsm_set_dialing(struct sig_gsm_chan *p, int is_dialing)
 {
       if (p->calls->set_dialing)
               p->calls->set_dialing(p->chan_pvt, is_dialing);
 }

 static int sms_write_sendlog(char *send_msg)
 {
    FILE *file= NULL;
    time_t t;
    char rd_time[50] = {0};
    struct stat status;
    int res;
    //int my_umask;
    char sendpath[100] = {0};
    char sendfile[100] = {0};
    time(&t);
    snprintf(sendpath,sizeof(sendpath)-1,"%s","/var/log/asterisk/sendsms");
    snprintf(sendfile,sizeof(sendfile)-1,"%s","/var/log/asterisk/sendsms/sendsms_log");
    if( 0 != stat((char*)sendpath,&status))
    {
         switch(errno)
         {
             case ENOENT:
                res = mkdir((char *)sendpath,0774);//S_IRWXU|S_IRWXG|S_IRWXO);
                if(res == -1)
                {
                    ast_log(LOG_WARNING,"mkdir sendsms log path find a error=%s\n",strerror(errno));
                    return 1;
                }
                else
                {
                     if(( file = fopen(sendfile, "w")) == NULL) 
                     {
		               ast_log(LOG_ERROR, "Cannot open file : /var/log/asterisk/sendsms/sendsms_log\n");
		               return 1;
                     }
                     fclose(file);
                }
                break;
             default:
                ast_log(LOG_WARNING,"crate sendsms log path find a error=%s\n",strerror(errno));
                break;
         }
     }
    if(( file = fopen(sendfile, "at")) == NULL) 
    {
		ast_log(LOG_ERROR, "Cannot open file : /var/log/asterisk/sendsms/sendsms_log\n");
		return 1;
    }
    strftime( rd_time, sizeof(rd_time), "%F %T", localtime(&t) );
    fprintf(file, "----SMS_START----\n"
                     "send_time:%s\n"
                     "%s\n"
                     "----SMS_END----\n\n",
                     rd_time,send_msg);
    fflush(file);
    fclose(file);
    return 0;
 }



 int sig_gsm_sms_fail_log(char* cause)
 {
     char send_msg[300] = {0};
      if(!cause)
        return -1;
     snprintf(send_msg,sizeof(send_msg)," sendsms is fail,because %s\n",cause);
     sms_write_sendlog(send_msg);
     return 0;
 }


int sig_gsm_span_sms_fail_log(unsigned int span_id,char *to, int len,char *cause)
{
     struct sig_gsm_span *span = gsm_spans[span_id];
     char send_msg[300] = {0};
     if(!span || !cause)
       return 1;
     if(len == 0)
         snprintf(send_msg,sizeof(send_msg),"span no=%d sendsms is all fail, because %s\n",span_id,cause);
     else if(len >0 && (to))
         snprintf(send_msg,sizeof(send_msg),"span no=%d to %s sendsms is fail,because %s\n",span_id,to,cause);
     else
          snprintf(send_msg,sizeof(send_msg),"span no=%d sendsms is  fail because %s\n",span_id,cause);
     sig_gsm_lock_private(span->pvts);
     sms_write_sendlog(send_msg);
     sig_gsm_unlock_private(span->pvts);
     return 0;
}


int sig_gsm_sms_report( unsigned int span_id, struct gsm_sms_event_data *sms )
{
    int res = 0;
    char send_msg[300] = {0};
    int len = 0;
    unsigned int smsid = sms->smsid;
    int result = sms->status.success;
    struct sig_gsm_span *span = gsm_spans[span_id];
    if(!span)
        return 1;
    if (result) 
           ast_log(LOG_WARNING, "Span %d: SMS sent OK (id:%d)\n", span->span_id , smsid);
    else
           ast_log(LOG_WARNING, "Span %d: Failed to send SMS (id:%d)\n",span->span_id,smsid);
    len += sprintf(&send_msg[len],
    		  "Span: %d\r\n"
              "SMS_TO:%s\r\n"
              "RESULT:%s\r\n",
              span->span_id,
              sms->to,
              (result==1)?"OK":"FAIL");
    span->sms_stats = 1;
    sig_gsm_lock_private(span->pvts);
    sms_write_sendlog(send_msg);
    sig_gsm_unlock_private(span->pvts);
    return res;
 }

 static int sms_write_recvlog(char *new_msg)
 {
    FILE *file= NULL;
    time_t t;
    char rd_time[50] = {0};
    struct stat status;
    int res;
    char writepath[100] = {0};
    char writefile[100] = {0};
    time(&t);
    snprintf(writepath,sizeof(writepath)-1,"%s","/var/log/asterisk/recvsms");
    snprintf(writefile,sizeof(writefile)-1,"%s","/var/log/asterisk/recvsms/recvsms_log");
    if( 0 != stat((char*)writepath,&status))
    {
      switch(errno)
      {
       case ENOENT:
        res = mkdir((char *)writepath,0774);//S_IRWXU|S_IRWXG|S_IRWXO);
        if(res == -1)
        {
           ast_log(LOG_WARNING,"mkdir recvsms log path find a error=%s\n",strerror(errno));
           return 1;
        }
        else
        {
          if(( file = fopen(writefile, "w")) == NULL)
          {
		      ast_log(LOG_ERROR, "Cannot open file : /var/log/asterisk/sendsms/recvsms_log\n");
		      return 1;
          }
          fclose(file);
        }
        break;
       default:
        ast_log(LOG_WARNING,"crate recvsms log path find a error=%s\n",strerror(errno));
        break;
      }
    }
    if(( file = fopen(writefile, "at")) == NULL) 
    {
		ast_log(LOG_ERROR, "Cannot open file : /var/log/asterisk/sms/recvsms\n");
		return 1;
    }
    strftime( rd_time, sizeof(rd_time), "%F %T", localtime(&t) );
    fprintf( file, "----SMS_START----\n"
                     "recv_time:%s\n"
                     "%s\n"
                     "----SMS_END----\n\n",
                     rd_time,new_msg);
    fflush(file);
    fclose(file);
    return 0;
 }


int sig_gsm_sms_ind( unsigned int span_id, struct gsm_sms_event_data *event)
{
      int res = 0;
      struct sig_gsm_span *span = gsm_spans[span_id];
      char msg [800] = {0};
      int event_len = 0;
      if(!event || !span)
      {
            ast_log(LOG_WARNING,"sig_gsm_sms_ind but have no event or span\n");
            return 1;
      }
      char smstime[128] = {0};
      memset(smstime, 0, sizeof(smstime));
      sprintf(smstime,"%02d/%02d/%02d %02d:%02d:%02d %d\0",
              event->timestamp.year,
			  event->timestamp.month,
			  event->timestamp.day,
              event->timestamp.hour,
			  event->timestamp.minute,
			  event->timestamp.second,
              event->timestamp.timezone);
      memset(msg, 0, sizeof(msg));
      ast_log(LOG_WARNING,"sig_gsm_sms_ind recv new sms smstime=%s\n",smstime);
      event_len += sprintf(&msg[event_len],
    		  "Span: %d\r\n"
              "From-Number: %s\r\n"
              "Timestamp: %02d/%02d/%02d %02d:%02d:%02d %d\r\n"
              "Type: %s\r\n",
              (span->span_id),
              event->from,
              event->timestamp.year, event->timestamp.month, event->timestamp.day,
              event->timestamp.hour, event->timestamp.minute, event->timestamp.second,
              event->timestamp.timezone,
              (event->gsm_sms_mode== GSM_SMS_TXT) ? "Text": "PDU");
       if (event->gsm_sms_mode == GSM_SMS_PDU)
               event_len += sprintf(&msg[event_len],"SMS-SMSC-Number: %s\r\n",event->smsc);
       event_len += sprintf(&msg[event_len],"Content: %s\r\n\r\n",event->content.message);
       manager_event(EVENT_FLAG_CALL, "GSMIncomingSms", "%s", msg);
       sig_gsm_lock_private(span->pvts);
       sms_write_recvlog(msg);
       span = gsm_spans[span_id];
       if(span)
       {
           char *context = span->pvts->context;
		   if (ast_exists_extension(NULL, context, "sms", 1, event->from))
		   {
			   struct ast_context *c;
			   struct ast_exten *e;
			   int found=0;
			   for(c=ast_walk_contexts(NULL); c; c=ast_walk_contexts(c))
			   {
			    if(!strcasecmp(ast_get_context_name(c),context))
			    {
				   for (e=ast_walk_context_extensions(c, NULL); e; e=ast_walk_context_extensions(c, e))
				   {
					  if(!strcasecmp(ast_get_extension_name(e),"sms\0"))
					  {
						 found=1;
						 break;
					  }
				   }
			    }
			    if(found)
			    {
			     struct ast_channel *chan = gsm_new_ast_channel(span->pvts, AST_STATE_UP, 0, GSM_SUB_REAL,0, NULL,0);
		         span->pvts->remotehangup = 0;
			     span->pvts->subs[GSM_SUB_REAL].allocd = 1;
			     span->pvts->subs[GSM_SUB_REAL].owner= chan;
			     span->pvts->subs[GSM_SUB_REAL].call_id= 1;
			     span->pvts->owner= chan;
			     pbx_builtin_setvar_helper(chan,"FROM",event->from);
				 pbx_builtin_setvar_helper(chan,"TIME",smstime);
			     pbx_builtin_setvar_helper(chan,"BODY",event->content.message);
			     ast_channel_exten_set(chan,"sms");
			     ast_pbx_start(chan);

			     break;
			    }
			   }
		   }
       }
      sig_gsm_unlock_private(span->pvts);
      return res;
 }


int sig_gsm_call_report( unsigned int span_id, unsigned int call_id, int result )
{
       int res = 0;
       struct sig_gsm_span *span = gsm_spans[span_id];
       switch(result)
       {
         case GSM_CALL_STATE_RINGING:
              ast_log(LOG_WARNING,"sig_gsm_call_report ringing\n");
              sig_gsm_lock_private(span->pvts);
              sig_gsm_set_echocanceller(span->pvts, 1);
              sig_gsm_lock_owner(span);
              if (span->pvts->owner)
              {
                   ast_setstate(span->pvts->owner, AST_STATE_RINGING);
                   ast_channel_unlock(span->pvts->owner);
              }
              gsm_queue_control(span, AST_CONTROL_RINGING);
              sig_gsm_unlock_private(span->pvts);
              break;
           case GSM_CALL_STATE_ANSWERED:
              ast_log(LOG_DEBUG,"sig_gsm_call_report answer id=%d\n",span_id);
              sig_gsm_lock_private(span->pvts);
              sig_gsm_open_media(span->pvts);
              sig_gsm_set_dialing(span->pvts, 0);
              sig_gsm_set_echocanceller(span->pvts, 1);
              sig_gsm_unlock_private(span->pvts);
              break;
        }
    return res;
 }



int sig_gsm_call_ind( unsigned int span_id, struct gsm_call_data *event )
{
  	   ast_log(LOG_DEBUG,"sig_gsm_call_ind\n");
       struct sig_gsm_span *span = NULL;
       struct ast_channel *chan = NULL;
   	   pthread_t threadid;
       char *cid_num = NULL;
       char *context = NULL;
       span = gsm_spans[span_id];
       if(!event || !span)
            return 1;
       cid_num = span->pvts->cid_num;
       context = span->pvts->context;
       sig_gsm_lock_private(span->pvts);
       if (span->pvts->subs[GSM_SUB_REAL].allocd)
       {
               ast_log(LOG_ERROR, "Span %d: Got CRING/RING but we already had a call. Dropping Call.\n", span->span_id + 1);
               sig_gsm_unlock_private(span->pvts);
               return 1;
       }
       span->pvts->subs[GSM_SUB_REAL].allocd = 1;
       span->pvts->subs[GSM_SUB_REAL].call_id = event->callid;
       span->pvts->remotehangup = 0;
       if (span->pvts->use_callerid) 
       {
               strcpy(cid_num, event->calling_num);//may be error,not error point to array
               ast_copy_string(span->pvts->cid_num, event->calling_num, sizeof(span->pvts->cid_num));
       }
       sig_gsm_unlock_private(span->pvts);
   	   struct ast_callid *callid = NULL;
       int callid_created = ast_callid_threadstorage_auto(&callid);
       chan = gsm_new_ast_channel(span->pvts, AST_STATE_RING, 0, GSM_SUB_REAL,0, NULL,callid);
       sig_gsm_lock_private(span->pvts);
       if (chan && !ast_pbx_start(chan))
       {
       	       ast_log(LOG_ERROR, "Accepting call from '%s', span=%d context=%s chan=%s\n", cid_num, span->span_id,span->pvts->context,ast_channel_name(chan));
               sig_gsm_set_echocanceller(span->pvts, 1);
               sig_gsm_unlock_private(span->pvts);
        }
        else
        {
               ast_log(LOG_WARNING, "Unable to start PBX, span %d\n", span->span_id);
               if (chan)
               {
                     sig_gsm_unlock_private(span->pvts);
                     ast_hangup(chan);
               }
               else
               {
                     gsm_hangup_req(span_id, event->callid);
                     sig_gsm_unlock_private(span->pvts);
               }
        }
      return 0;
 }

 int sig_gsm_call_hangup_cfm( unsigned int span_id, unsigned int call_id, int result )
 {
      int res = 0;
      struct sig_gsm_span *span = gsm_spans[span_id];
      if(!span)
        return 1;
      ast_log(LOG_DEBUG,"Span %d: Call Release\n", span->span_id);
      sig_gsm_lock_private(span->pvts);
      if (!span->pvts->subs[GSM_SUB_REAL].allocd)
      {
            ast_log(LOG_WARNING, "Span %d: Got Release, but there was no call.\n", span->span_id);
            sig_gsm_unlock_private(span->pvts);
            return 1;
       }
       memset(&span->pvts->subs[GSM_SUB_REAL], 0, sizeof(span->pvts->subs[0]));
       span->pvts->owner = NULL;
       if(span->pvts->owner)
             ast_log(LOG_WARNING,"sig_gsm_call_hangup_cfm and clear chan fail \n");
       sig_gsm_unlock_private(span->pvts);
       return res;
 }

int sig_gsm_call_hangup_ind( unsigned int span_id,  int cause, unsigned char callid)
{
       struct sig_gsm_span *span = gsm_spans[span_id];
       if(!span)
       {
            ast_log(LOG_WARNING,"sig_gsm_call_hangup_ind have no span or event\n");
            return 1;
       }
       ast_log(LOG_DEBUG,"Span %d: Call hangup requested\n", span->span_id);
       sig_gsm_lock_private(span->pvts);
       if (!span->pvts->subs[GSM_SUB_REAL].allocd)
       {
               ast_log(LOG_ERROR, "Span %d: Got hangup, but there was not call.\n", span->span_id);
               gsm_hangup_cfm(span_id, callid);
               sig_gsm_unlock_private(span->pvts);
             return 1;
       }
       if (span->pvts->owner)
       {
               span->pvts->remotehangup = 1;
               ast_channel_hangupcause_set(span->pvts->owner, cause);
               ast_channel_softhangup_internal_flag_add(span->pvts->owner, AST_SOFTHANGUP_DEV);
       }
       else
       {
               /* Proceed with the hangup even though we do not have an owner */
               gsm_hangup_cfm(span_id, callid);
               memset(&span->pvts->subs[GSM_SUB_REAL], 0, sizeof(span->pvts->subs[GSM_SUB_REAL]));
       }
       sig_gsm_unlock_private(span->pvts);
       return 0;
 }



 static void sig_gsm_handle_exception(struct sig_gsm_span *gsm)
 {
       if (gsm->calls->handle_dchan_exception)
               gsm->calls->handle_dchan_exception(gsm);
 }

 static void sig_gsm_handle_data(struct sig_gsm_span *gsm)
 {
       char buf[1024] = {0};
       int res = 0;
       res = read(gsm->fd, buf, sizeof(buf));
       if (!res)
       {
           if (errno != EAGAIN)
           {
              ast_log(LOG_ERROR, "Span %d:Read on %d failed: %s\n", gsm->span_id , gsm->fd, strerror(errno));
              return;
           }
       }
       gsm_span_process_read(gsm->span_id, buf, res);
 }

 static void *gsm_mon(void *pgsm)
 {
   	   struct sig_gsm_span *gsm = pgsm;
       char status[GSM_SPAN_STAUTS_LEN] = {0};
       char active[GSM_SPAN_STAUTS_LEN] = {0};
       unsigned char count = 0;
       pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
       for(;;)
       {
            count++;
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            memset(status, 0, sizeof(status));
            gsm_req_span_stats(gsm->span_id, status, active, GSM_SPAN_STAUTS_LEN);
            if(((!strncasecmp(status,"INCALL",strlen("INCALL"))  || (gsm->pvts->subs[GSM_SUB_REAL].allocd))) && ((count+1)%3==0))
                 gsm_cmd_req(gsm->span_id, "AT+CLCC\r\n");
            else if(((count+1)%3==1) &&  (!strncasecmp(status,"UP",strlen("UP")) ))
                gsm_cmd_req(gsm->span_id, "AT+CSQ\r\n");
            else if(((count+1)%3==2) && (!strncasecmp(active,"ACTIVE",strlen("ACTIVE")) ) )
                gsm_cmd_req(gsm->span_id, "AT+CREG?\r\n");
            sleep(4);
     }
 }

 static void *gsm_dchannel(void *pgsm)
 {
	struct sig_gsm_span *gsm = pgsm;
	struct pollfd fds[1];
	int res;
    int load = gsm->load;
	time_t t;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    for(;;)
    {
        fds[0].fd = gsm->fd;
        fds[0].events = POLLIN | POLLPRI;
        fds[0].revents = 0;
        time(&t);
        res = gsm_span_schedule_next(gsm->span_id);
        if ((res < 0) || (res > 1000))
			res = 1000;
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
		res = poll(fds, 1, res);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if(res==0)
        {}
        else if (res > 0)
        {
           if (fds[0].revents & POLLPRI)
              sig_gsm_handle_exception(gsm);
           if (fds[0].revents & POLLIN)
               sig_gsm_handle_data(gsm);//
       }
       else if (errno != EINTR)
           ast_log(LOG_WARNING, "poll returned error %d (%s)\n", errno, strerror(errno));
       gsm_span_run(gsm->span_id, load);
       load = 1;
     }
 }

 int sig_gsm_start_gsm(struct sig_gsm_span *gsm)
 {
	ast_mutex_init(&gsm->lock);
    if (gsm->fd == -1)
			return -1;
	if (ast_pthread_create_background(&gsm->master, NULL, gsm_dchannel, gsm))
    {
        if (gsm->fd > 0)
		   close(gsm->fd);
	    gsm->fd = -1;
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
    }
    if (ast_pthread_create_background(&gsm->mon, NULL, gsm_mon, gsm))
    {
	    if (gsm->fd > 0)
		   close(gsm->fd);
        gsm->fd = -1;
		ast_log(LOG_ERROR, "Unable to spawn m-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
 }
 
 char *parse_lang_charset(char *lang ,unsigned char *charset)
 {
    if(!strcasecmp(lang,"eeur"))
    {
        *charset = GSM_SMS_CONTENT_CHARSET_EEUR;
        return "ISO_8859-2";
    }
    else if(!strcasecmp(lang,"weur"))
    {
        *charset = GSM_SMS_CONTENT_CHARSET_WEUR;
        return "ISO_8859-15";
    }
    else if(!strcasecmp(lang,"cn"))
    {
       *charset =  GSM_SMS_CONTENT_CHARSET_GB18030;
        return "gb18030";
    }
    else
    {
        *charset = GSM_SMS_CONTENT_CHARSET_WEUR;
        return "ISO_8859-15";//en
    }
 }

 void sig_gsm_log_span(unsigned char span_id, unsigned char loglevel, char *fmt, ...)
 {
        char *data;
        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&data, fmt, ap) == -1)
        {
                ast_log(LOG_ERROR, "Failed to get arguments to log error\n");
                return;
        }
        sig_gsm_log(loglevel, "Span %d:%s", span_id, data);
        free(data);
 }


 void sig_gsm_log(unsigned char loglevel, char *fmt, ...)
 {
        char *data;
        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&data, fmt, ap) == -1)
        {
                ast_log(LOG_ERROR, "Failed to get arguments to log error\n");
                return;
        }
        switch(loglevel)
        {
                case GSM_LOG_DEBUG:
                        break;
                case GSM_LOG_NOTICE:
                        ast_verb(3, "%s", data);
                        break;
                case GSM_LOG_WARNING:
                        ast_log(LOG_WARNING, "%s", data);
                        break;
                case GSM_LOG_INFO:
                        ast_verb(1, "%s", data);
                        break;
                case GSM_LOG_CRIT:
                case GSM_LOG_ERROR:
                default:
                        ast_log(LOG_ERROR, "%s", data);
                        break;
        }
        free(data);
}

 void *sig_gsm_malloc(unsigned int size)
 {
      return ast_malloc(size);
 }

 void *sig_gsm_calloc(unsigned int mem_num, unsigned int size)
 {
    return ast_calloc(mem_num, size);
 }

 void sig_gsm_free(void *p)
 {
    return ast_free(p);
 }

 int sig_libgsm_load(void)
 {
       int res = 0;
       gsm_interface_t gcom_gsm_interface;
       gsm_spans = ast_malloc(GSM_MAX_SPANS * sizeof(void*));
       if(gsm_spans)
            memset(gsm_spans, 0, GSM_MAX_SPANS * sizeof(void*));
       else
            return 1;
       gcom_gsm_interface.gsm_span_write = sig_gsm_span_write;
       gcom_gsm_interface.gsm_malloc = sig_gsm_malloc;
       gcom_gsm_interface.gsm_calloc = sig_gsm_calloc;
       gcom_gsm_interface.gsm_free = sig_gsm_free;
       gcom_gsm_interface.gsm_sms_ind = sig_gsm_sms_ind;
       gcom_gsm_interface.gsm_sms_report = sig_gsm_sms_report;
       gcom_gsm_interface.gsm_call_ind = sig_gsm_call_ind;
       gcom_gsm_interface.gsm_call_report = sig_gsm_call_report;
       gcom_gsm_interface.gsm_call_hangup_ind = sig_gsm_call_hangup_ind;
       gcom_gsm_interface.gsm_call_hangup_cfm = sig_gsm_call_hangup_cfm;
       gcom_gsm_interface.gsm_log = sig_gsm_log;
       gcom_gsm_interface.gsm_log_span = sig_gsm_log_span;
       res = init_lib_gsm_stack(&gcom_gsm_interface);
       return res;
}
