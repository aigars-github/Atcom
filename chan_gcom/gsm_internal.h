#ifndef _ATCOM_INTERNAL_H
#define _ATCOM_INTERNAL_H

#include "gsm_queue.h"
#include "libgsmcom.h"
#include "gsm_sched.h"

#define GSM_MAX_CMD_RETRIES 3
#define gsm_mutex_lock pthread_mutex_lock
#define gsm_mutex_unlock pthread_mutex_unlock
#define GSM_TIME_ONE_MILLION	1000000
#define gsm_array_len(a) (sizeof(a)/sizeof(a[0]))

 struct gsm_sig
 {
    int rssi;
    int ber;
 };


/*gsm module info*/
 struct gsm_mod
 {
	int fd;				/* File descriptor for D-Channel */
 	int span;			/* Span number */
	int debug;			/* Debug stuff */
	int state;			/* State of D-channel */
 	int network;
    gsm_sim_info_t sim_info;
    int inputvol;
    int outputvol;
    struct gsm_sig sig_info;
 };

 struct gsm_span;

  typedef struct gsm_span_call
{
    gsm_call_state_t newstate;
    struct gsm_span *span;
    struct gsm_call_data data;
    unsigned char id;
    unsigned char direction;
    unsigned char recv_clip;
 }gsm_span_call_t;


//event save one \r\n dequeue must free
//atbuf save one complete
 struct gsm_span
 {
       unsigned int  span_id;
       unsigned int heat;
       unsigned char start_count;
       int  span_state;
       event_fifo_queue event_queue; //do recv cmd
       sched_fifo_queue sched_queue;
       char event_buf[GSM_MAX_ATCMD_LEN]; //save \r\n,
       char sms_buf[100];
       int index;
       char last_cmd[100];
       unsigned char cmd_busy:1;
       unsigned char cmd_err;
       char last_error[GSM_PARSE_ERROR_LEN];
       int  alarm;
       unsigned int setpin:1;
       int call_progress;
       int event_call_state;//1//1:nomal 2:no carrier 3:error
       int rx_sms_state;//1//1:rxsms token
       struct gsm_cfg config;       /* Configuration parameters */
       struct gsm_mod module;            /* Module interface */
       struct gsm_span_call calls;//app to lib jwj change 2012-6-13
       struct gsm_sms_event_data smss;
       unsigned last_call_id;
       unsigned char call_count;
       unsigned char notify_count;
       int csq;
       int load;
       unsigned int timer;
 };

 extern struct gsm_interface g_interface;

 #define gsm_log_span(span, level, a, ...) if (g_interface.gsm_log_span) g_interface.gsm_log_span(span, level,a, ##__VA_ARGS__)
 #define gsm_log(level,a,...) if (g_interface.gsm_log) g_interface.gsm_log(level, a, ##__VA_ARGS__)

 int   GSM_INIT_EVENT_QUEUE(event_fifo_queue *eventq);
 int   GSM_INIT_SCHED_QUEUE(sched_fifo_queue*schedq);

 int  gsm_sig_status_up(gsm_net_stat_t stat);
 void * gsm_malloc(unsigned int size);
 void * gsm_calloc(unsigned int mem_num, unsigned int size);
 void  gsm_free(void *p);
 int gsm_span_write(unsigned int span_id, void *buf, int len);
 int gsm_sms_ind(unsigned int span_id, struct gsm_sms_event_data *event);
 int gsm_sms_report(unsigned int span_id, struct gsm_sms_event_data *sms);
 int gsm_call_ind(unsigned int span_id, struct gsm_call_data *event);
 int gsm_call_report(unsigned int span_id, unsigned int call_id, int result);
 int gsm_call_hangup_ind(unsigned int span_id, int cause, unsigned char callid);
 int gsm_call_hangup_cfm(unsigned int span_id, unsigned int call_id, int result);
 char* get_mod_at(int mod_id, int cmd_id);
 void gsm_span_do_events(struct gsm_span *span);
 void gsm_span_do_sched(struct gsm_span *span);
 struct gsm_span * gsm_get_span(unsigned int span_id);
 int gsm_write_cmd(struct gsm_span *span, void *data, unsigned int len);
 char *gsm_strdup(const char *str); //jwj add 2012-6-14
 struct timeval gsm_tvadd(struct timeval a, struct timeval b);
 struct timeval tvfix(struct timeval a);
 struct timeval gsm_tvsub(struct timeval a, struct timeval b);
 int gsm_tvzero(const struct timeval t);
 int gsm_tvdiff_ms(struct timeval end, struct timeval start);
 struct timeval gsm_tv(unsigned long sec, unsigned long usec);
 struct timeval gsm_samp2tv(unsigned int nsamp, unsigned int rate);
 struct timeval gsm_tvnow(void);
 int gsm_tvcmp(struct timeval a, struct timeval b);
 int gsm_mutex_init(pthread_mutex_t *lock );
 int gsm_mutex_destroy(pthread_mutex_t *lock);
 int gsm_match_prefix(char *string, const char *prefix);
 void gsm_free_tokens(char *tokens[]);
 int gsm_cmd_entry_tokenize(char *entry, char *tokens[], int len);
 int  gsm_call_set_state(struct gsm_span *span, gsm_call_state_t callstate);
 int gsm_set_sms_state(struct gsm_span *span, gsm_sms_state_t  newstate);
 int gsm_span_create_call(struct gsm_span *span, unsigned int call_id,int direct);
 int gsm_find_span_callid(struct gsm_span *span);
 int gsm_find_span_call_by_state(struct gsm_span* span, gsm_call_state_t state);
 int gsm_set_module_at_cmd(struct gsm_span *span, int cmd);
 int gsm_set_span_status(struct gsm_span *span, int status);
 int  gsm_switch_start_state_cmd(struct gsm_span *span, int cmd);

 #endif /* _ATCOM_INTERNAL_H */
