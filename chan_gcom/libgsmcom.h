/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Parts taken from libpri
 * Written by mark.liu <mark.liu@openvox.cn>
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * $Id: module.h 60 2010-09-09 07:59:03Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */
 
#ifndef __LIB_ATCOM_LIBGSM_H__
#define __LIB_ATCOM_LIBGSM_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <iconv.h>

#define GSM_MAX_SMS_QUEUE_NUM 50
#define GSM_SPAN_STAUTS_LEN 30
#define GSM_MAX_SPANS   64
#define GSM_CALL_NUM_LEN 30
#define GSM_MAX_ATCMD_LEN  1024
#define GSM_MAX_CONTENT_LEN 1024
#define GSM_MAX_HEX_CONTENT_LEN 2048
#define GSM_PDU_UCS2_MODE_LEN 140 
#define GSM_PDU_7BIT_MODE_LEN 160
#define GSM_NORMAL_SMS_LEN 160
#define GSM_MAX_SMS_LEN 1024
#define GSM_PARSE_ERROR_LEN 30
#define GSM_SMS_TIMESTAMP_CHAR_LEN 30
#define GSM_PINNUM_LEN  16
#define GSM_IMEI_LEN  32
#define GSM_IMSI_LEN  32
#define GSM_MODULE_INFO_LEN 100
#define GSM_COUNTRY_CODE_LEN 5				
#define GSM_CALL_NUM_TYPE_LEN 4				     
#define GSM_SMS_LANGUAGE_LEN 20
#define GSM_CALL_ID_LEN   50//CALLED+SPAN+TIMESTAMP
#define GSM_SMS_ID_LEN     50//CALLED_SPAN_TIMESTAMP
#define GSM_MAX_ATCMD_LINES 10
#define GSM_DIALPLAN_LEN 20
#define GSM_MAX_CALLS_IN_SPAN 8
#define GSM_MAX_NOTIFYS_IN_SPAN 50
#define GSM_MAX_SMSS_IN_SPAN 100
#define GSM_CMD_END "\r\n"
#define GSM_MAX_TIMERS 30

/* Debugging */
#define GSM_DEBUG_UART_RAW                      (1 << 0) /* Show raw uart reads */
#define GSM_DEBUG_UART_DUMP                     (1 << 1) /* Show uart commands */
#define GSM_DEBUG_CALL_STATE            (1 << 2) /* Debug call states */
#define GSM_DEBUG_SPAN_STATE            (1 << 3) /* Debug call states */
#define GSM_DEBUG_AT_PARSE                      (1 << 4) /* Debug how AT commands are parsed */
#define GSM_DEBUG_AT_HANDLE                     (1 << 5) /* Debug how AT commands are scheduled/processed */
#define GSM_DEBUG_SMS_DECODE            (1 << 6) /* Debug how PDU is decoded */
#define GSM_DEBUG_SMS_ENCODE            (1 << 7) /* Debug how PDU is encoded */

 enum gsm_sub
 {
	GSM_SUB_REAL = 0,			/*!< Active call */
	GSM_SUB_CALLWAIT,			/*!< Call-Waiting call on hold */
	GSM_SUB_THREEWAY,			/*!< Three-way call */
 };

 typedef enum
 {
        GSM_NET_NOT_REGISTERED = 0,             /* Initial state */
        GSM_NET_REGISTERED_HOME,                /* Registered to home network */
        GSM_NET_NOT_REGISTERED_SEARCHING,       /* Not Registered, searching for an operator */
        GSM_NET_REGISTRATION_DENIED,            /* Registration denied */
        GSM_NET_UNKNOWN,                        /* Unknown */
        GSM_NET_REGISTERED_ROAMING,             /* Registered, roaming */
        GSM_NET_INVALID,
} gsm_net_stat_t;

 typedef enum
 {
        GSM_SIGSTATUS_DOWN,
        GSM_SIGSTATUS_UP,
 } gsm_sigstatus_t;

 typedef enum
 {
        GSM_LOG_CRIT,
        GSM_LOG_ERROR,
        GSM_LOG_WARNING,
        GSM_LOG_INFO,
        GSM_LOG_NOTICE,
        GSM_LOG_DEBUG,
 } gsm_loglevel_t;

/*gsm node type*/
 enum GSM_NODE_TYPE
 {
  GSM_UNKNOWN=0,
  GSM_CPE,
  GSM_NET,
 };

 enum  gsm_call_type
 {
        GSM_CALL_TYPE_VOICE = 0,
        GSM_CALL_TYPE_DATA,
        GSM_CALL_TYPE_FAX,
        GSM_CALL_TYPE_INVALID,
 };

 typedef enum gsm_timer
 {
    GSM_TIMER_CMD_TIMEOUT = 0,
    GSM_TIMER_CMD_INTERVAL,
    GSM_TIMER_NO_ANSWER,
    GSM_TIMER_MOD_BOOT,
    GSM_TIMER_SIGNAL_POLL_INTERVAL,
    GSM_TIMER_WAIT_CID,
    GSM_TIMER_WAIT_SIGNAL,
    GSM_TIMER_PROGRESS_POLL_INTERVAL,
 }gsm_timer_t;

/*span state*/
 enum
 {
        GSM_SPAN_STATE_INIT = 0,                    /* Initial state */
        GSM_SPAN_STATE_START,                   /* Span is starting, waiting for SIM to be ready */
        GSM_SPAN_STATE_REGNET,              /* SIM access is possible, perform SIM or Network dependent chip initialization commands */
        GSM_SPAN_STATE_RUNNING,                 /* Span is running and ready to accept external commands  sync ok now */
        GSM_SPAN_STATE_STOP,                /* Span is stopping */
        GSM_SPAN_STATE_SHUTDOWN,                /* Not used yet, will be used when live SIM swapping is implemented */
        GSM_SPAN_STATE_INVALID,
 };

 /*call direction*/
 enum GSM_CALL_DIRECTION
 {
   GCOM_DIRECTION_OUTGOING = 0,
   GCOM_DIRECTION_INCOMING,
 };

/*call state*/
 typedef enum GSM_CALL_STATE
 {
        GSM_CALL_STATE_INIT = 0,            /* Initial state */
        GSM_CALL_STATE_WAIT,
        GSM_CALL_STATE_DIALING,         /* We just received a CRING/RING */
        GSM_CALL_STATE_DIALED,          /* We notified the user of the incoming call */
        GSM_CALL_STATE_RINGING,         /* On outgoing call, remote side is ringing */
        GSM_CALL_STATE_ANSWERED,        /* Incoming Call has been answered by user */
        GSM_CALL_STATE_UP,                      /* Call is up */
        GSM_CALL_STATE_TERMINATING,             /* Call has been hung-up on the remote side */
        GSM_CALL_STATE_TERMINATING_CMPL, /* User has acknowledged remote hang-up */
        GSM_CALL_STATE_HANGUP,          /* Call has been hung-up on local side */
        GSM_CALL_STATE_HANGUP_CMPL,     /* GSM Chip has acknowledged local hang-up */
        GSM_CALL_STATE_INVALID,
 } gsm_call_state_t;

 typedef enum GSM_SMS_STATE
 {
        GSM_SMS_STATE_INIT = 0,
        GSM_SMS_STATE_READY,
        GSM_SMS_STATE_START,   //change cmgf mode
        GSM_SMS_STATE_SEND_HEADER,//send header
        GSM_SMS_STATE_SEND_BODY,
        GSM_SMS_STATE_SEND_END,//CTROL+Z  send end
        GSM_SMS_STATE_COMPLETE, //send ok
        GSM_SMS_STATE_FAIL, //send ok
        GSM_SMS_STATE_INVALID,    //send fail 
 }gsm_sms_state_t;

/*sms mode */
 typedef enum GSM_SMS_TYPE
 {
       GSM_SMS_PDU =0,
       GSM_SMS_TXT,
	   GSM_SMS_UNKNOWN,
 }gsm_sms_type_t;

/* gsm module state*/
 enum GSM_MODULE_STATE
 {
        GSM_SIM_INIT=0,
        GSM_SIM_STATE_UNKNOWN,
        GSM_SIM_CARD_UNINSERT,
        GSM_SIM_PIN_REQ,
        GSM_SIM_PUK_REQ,
        GSM_SIM_PIN2_REQ,
        GSM_SIM_PUK2_REQ,
        GSM_SIM_SCAN_NET,
        GSM_SIM_NO_NET,
        GSM_SIM_REG_OK,
        GSM_SET_ECHO,
        GSM_SET_CLIP,
        GSM_SET_ATX,
        GSM_SET_QAUDCH,
        GSM_SET_QMOSTAT,
        GSM_SET_QSIMDET,
        GSM_GET_CPIN,
        GSM_SET_CNMI,
        GSM_SET_CMEE,
        GSM_SET_CREG,
        GSM_GET_CGMR,
        GSM_GET_CGSN,
        GSM_GET_CIMI,
        GSM_GET_CNUM,
        GSM_GET_CREG,
        GSM_GET_CSCA,
        GSM_GET_CSQ,
        GSM_SET_ATW,
        GSM_SET_QSIDET,
        GSM_SET_QECHO,
        GSM_SIM_READY,//SYNC AT OK
 };

/*sms pdu dcs type*/
 enum
 {
        GSM_SMS_PDU_DCS_7BIT = 0,               /* ASCII */
        GSM_SMS_PDU_DCS_8BIT,
        GSM_SMS_PDU_DCS_UCS2,                  /* 16 bit */
        GSM_SMS_PDU_DCS_RESERVED,
        GSM_SMS_PDU_DCS_INVALID,
 };

/*content charset*/
 typedef enum  gsm_content_charset
 {
        GSM_SMS_CONTENT_CHARSET_ASCII = 0,
        GSM_SMS_CONTENT_CHARSET_UTF8,
        GSM_SMS_CONTENT_CHARSET_GB18030,
        GSM_SMS_CONTENT_CHARSET_EEUR,
        GSM_SMS_CONTENT_CHARSET_WEUR,
        GSM_SMS_CONTENT_CHARSET_INVALID,
 } gsm_content_charset_t;

/*encryption*/
 typedef enum  gsm_content_encrypt
 {
        GSM_SMS_CONTENT_ENCRYPT_NONE = 0,
        GSM_SMS_CONTENT_ENCRYPT_BASE64,
        GSM_SMS_CONTENT_ENCRYPT_INVALID,
 }gsm_content_encrypt_t ;

 typedef struct _gsm_sim_info
 {
        char cnum[GSM_CALL_NUM_LEN];
        //char subscriber_type[20];
        char smsc[GSM_CALL_NUM_LEN]; /* SMS Service Centre */
        char version[GSM_CALL_NUM_LEN];
        char imsi[GSM_IMEI_LEN];
        char imei[GSM_IMEI_LEN];
        
} gsm_sim_info_t;

 struct gsm_cause_status
 {
    unsigned char cause;
    unsigned char success;
 };

 /*malloc  in application, read for application last inbound ,outbound in common*/
 struct  gsm_call_data
 {
        unsigned char  callid;
        unsigned int span_id;         /* ID used internally by module */
        int  type;
        char calling_num[GSM_CALL_NUM_LEN];
        char called_num[GSM_CALL_NUM_LEN];
        char cmd[GSM_MAX_ATCMD_LEN];
        int  state;
        int switchtype;
        int flags;
        int direct; /* Inbound or outbound */
        unsigned long when;
        int cause;
        char dialplan[GSM_DIALPLAN_LEN];
        int event;
        int sub;
        struct gsm_cause_status status;
 };

/*pdu user data*/
struct gsm_sms_content
{
    int msg_len;
    unsigned char charset;
    unsigned char encrypt;
    char  message[GSM_MAX_HEX_CONTENT_LEN];//not encrypt msg may change
};

/*decode pdu msg timestamp*/
struct gsm_sms_pdu_timestamp
{
        int year;
        int month;
        int day;
        int hour;
        int minute;
        int second;
        int timezone;
};

 struct gsm_sms_event_data
 {
    int gsm_sms_mode;
    char sms_id[GSM_SMS_ID_LEN];
    char body[512];
    unsigned char smsid;//id in span queue
    unsigned char  pid;                 /* Protocol Identifier */
    unsigned char  dcs;                 /* Data coding scheme */
    unsigned char  long_flag;//more than 160
    unsigned char queue_flag;//
    unsigned long when;
    int pdu_len;
    char  to[GSM_CALL_NUM_LEN];
    char from[GSM_CALL_NUM_LEN];
    struct gsm_sms_content content;
    char smsc[GSM_CALL_NUM_LEN];
    struct gsm_sms_pdu_timestamp timestamp;
    struct gsm_cause_status status;
    int stats;//send,del
    int msg_len;
    int send_len;
 };

 /*error code*/
 struct err_code
 {
    unsigned char code;
    char *des;
 };

/*GSM CONFIG */
 struct gsm_cfg
 {
       char countrycode[GSM_COUNTRY_CODE_LEN];//			
       char numtype[GSM_CALL_NUM_TYPE_LEN];				     
       char pinnum[GSM_PINNUM_LEN];
       char sms_lang[GSM_SMS_LANGUAGE_LEN];
       int nodetype;           //GSM_CPE GSM_NET 					
	   int switchtype;
       //TIMER
       unsigned long timer[GSM_MAX_TIMERS];
 };

 typedef void *(*gsm_malloc_cb) (unsigned int size);
 typedef void *(*gsm_calloc_cb) (unsigned int mem_num,unsigned int size);
 typedef void (*gsm_free_cb) (void *p);
 typedef int (*gsm_write_cb) (unsigned int span_id, void *buf, int len);
 typedef int (*gsm_sms_ind_cb) (unsigned int span_id, struct gsm_sms_event_data *event);
 typedef int (*gsm_sms_report_cb) (unsigned int span_id, struct gsm_sms_event_data *sms);
 typedef int (*gsm_call_ind_cb) (unsigned int span_id, struct gsm_call_data *event);
 typedef int (*gsm_call_report_cb) (unsigned int span_id, unsigned int call_id, int result);
 typedef int (*gsm_call_hangup_ind_cb) (unsigned int span_id, int cause, unsigned char callid);
 typedef int (*gsm_call_hangup_cfm_cb) (unsigned int span_id, unsigned int call_id, int result);
 typedef void (*gsm_log_span_cb)(unsigned int span_id, unsigned int level, char *fmt, ...);
 typedef void (*gsm_log_cb)(unsigned int level, char *fmt, ...);

/*for application callback*/
 typedef struct gsm_interface
 {
    gsm_malloc_cb gsm_malloc;
    gsm_calloc_cb gsm_calloc;
    gsm_free_cb gsm_free;
    gsm_write_cb  gsm_span_write;
    gsm_call_hangup_ind_cb gsm_call_hangup_ind;
    gsm_call_hangup_cfm_cb gsm_call_hangup_cfm;
    gsm_sms_ind_cb gsm_sms_ind;
    gsm_sms_report_cb gsm_sms_report;
    gsm_call_ind_cb gsm_call_ind;
    gsm_call_report_cb gsm_call_report;
    gsm_log_cb gsm_log;
    gsm_log_span_cb gsm_log_span;
    }gsm_interface_t;

 extern void gsm_span_run( unsigned int  span_id , int load);
 extern void gsm_read_events(unsigned int span_id, const char *buf, int len );
 extern int init_lib_gsm_stack(gsm_interface_t *gcom_interface);
 extern int init_sig_gsm_span(struct gsm_cfg *config, unsigned int span_id);
 extern const char *gcom_get_libgsm_version(void);
 extern int gsm_call_req(unsigned int span_id,  struct gsm_call_data  *call_data);
 extern int gsm_call_cfm(unsigned int span_id,  unsigned char callid);
 extern int gsm_hangup_req(unsigned int span_id,  unsigned char callid);
 extern int gsm_hangup_cfm(unsigned int span_id,  unsigned char callid);
 extern int gsm_sms_req(unsigned int span_id, struct gsm_sms_event_data  *call_data);
 extern int gsm_cmd_req(unsigned char  span_id, const char *at_cmd);
 extern int gsm_send_dtmf(unsigned char span_id,  char digit);
 int  gsm_span_schedule_next(unsigned int span_id);
 extern int gsm_req_span_stats(int span, char *status, char *active,int len);
 extern int gsm_req_span_csq(int span, int *csq);
 extern int gsm_req_span_siminfo_smsc(int span_id, char *smsc, int len);
 extern int gsm_req_span_siminfo_imei(int span_id, char *imei, int len);
 extern int gsm_req_span_siminfo_imsi(int span_id, char *imsi, int len);
 extern int gsm_req_span_siminfo_version(int span_id, char *version, int len);

#endif

