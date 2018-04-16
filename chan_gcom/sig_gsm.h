#ifndef _ATCOM_GCOM_H
#define _ATCOM_GCOM_H
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include <libgsmcom.h>

#define SIG_GSM_MAX_CHANNELS  64
#define SIG_GSM_DEBUG_NORMAL 1
#define SIG_GSM_DEBUG_INTENSE    2
#define GSM_DEBUG_STATE 1
#define GSM_DEBUG_RAW 2
#define GSM_NUMTYPE_NUM 4
#define GSM_PINNUM_NUM  8

/* Polarity states */
#define POLARITY_IDLE   0
#define POLARITY_REV    1

#define READ_SIZE 160

 enum sig_gsm_tone
 {
	SIG_GSM_TONE_RINGTONE = 0,
	SIG_GSM_TONE_STUTTER,
	SIG_GSM_TONE_CONGESTION,
	SIG_GSM_TONE_DIALTONE,
	SIG_GSM_TONE_DIALRECALL,
	SIG_GSM_TONE_INFO,
	SIG_GSM_TONE_BUSY,
 };

 enum sig_gsm_law
 {
	SIG_GSM_DEFLAW = 0,
	SIG_GSM_ULAW,
	SIG_GSM_ALAW
 };

/*! Call establishment life cycle level for simple comparisons. */
 enum sig_gsm_call_level
 {
	/*! Call does not exist. */
	SIG_GSM_CALL_LEVEL_IDLE,
	/*! Call is present but has no response yet. (SETUP) */
	SIG_GSM_CALL_LEVEL_SETUP,
	/*! Call is collecting digits for overlap dialing. (SETUP ACKNOWLEDGE) */
	SIG_GSM_CALL_LEVEL_OVERLAP,
	/*! Call routing is happening. (PROCEEDING) */
	SIG_GSM_CALL_LEVEL_PROCEEDING,
	/*! Called party is being alerted of the call. (ALERTING) */
	SIG_GSM_CALL_LEVEL_ALERTING,
	/*! Call is connected/answered. (CONNECT) */
	SIG_GSM_CALL_LEVEL_CONNECT,
 };

 struct sig_gsm_span;
 struct sig_gsm_callback;

 struct gsm_subchannel
 {
	//int dfd;
	struct ast_channel *owner;
	//int chan;
	//short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct ast_frame f;		/*!< One frame for each channel.  How did this ever work before? */
	unsigned int allocd:1;
    unsigned char call_id;
 };

 struct sig_gsm_chan
 {
	/* Options to be set by user */
	unsigned int hidecallerid:1;
	unsigned int hidecalleridname:1;      /*!< Hide just the name not the number for legacy PBX use */
	unsigned int immediate:1;			/*!< Answer before getting digits? */
	unsigned int priexclusive:1;			/*!< Whether or not to override and use exculsive mode for channel selection */
	unsigned int priindication_oob:1;
	unsigned int use_callerid:1;			/*!< Whether or not to use caller id on this channel */
	unsigned int use_callingpres:1;			/*!< Whether to use the callingpres the calling switch sends */
    unsigned int remotehangup:1;
    char context[AST_MAX_CONTEXT];
	char mohinterpret[MAX_MUSICCLASS];
	int stripmsd;
	int channel;					/*!< Channel Number or CRV */
	/* Options to be checked by user */
	int cid_ani2;						/*!< Automatic Number Identification number (Alternate GSM caller ID number) */
	int callingpres;				/*!< The value of calling presentation that we're going to use when placing a GSM call */
	char cid_num[AST_MAX_EXTENSION];
	char cid_subaddr[AST_MAX_EXTENSION];
	char cid_name[AST_MAX_EXTENSION];
	char cid_ani[AST_MAX_EXTENSION];
	/*! \brief User tag for party id's sent from this device driver. */
	char exten[AST_MAX_EXTENSION];
	/* Probably will need DS0 number, DS1 number, and a few other things */
	char dialdest[256];				/* Queued up digits for overlap dialing.  They will be sent out as information messages when setup ACK is received */
	unsigned int inalarm:1;
	unsigned int alerting:1;		/*!< TRUE if channel is alerting/ringing */
	unsigned int alreadyhungup:1;	/*!< TRUE if the call has already gone/hungup */
	unsigned int isidlecall:1;		/*!< TRUE if this is an idle call */
	unsigned int proceeding:1;		/*!< TRUE if call is in a proceeding state */
	unsigned int progress:1;		/*!< TRUE if the call has seen progress through the network */
	unsigned int resetting:1;		/*!< TRUE if this channel is being reset/restarted */
	unsigned int setup_ack:1;		/*!< TRUE if this channel has received a SETUP_ACKNOWLEDGE */
	unsigned int outgoing:1;
	unsigned int digital:1;
	/*! \brief TRUE if this interface has no B channel.  (call hold and call waiting) */
	unsigned int no_b_channel:1;
	struct ast_channel *owner;
	struct sig_gsm_span *gsm;
    struct gsm_subchannel subs[3];
    char call_forward[AST_MAX_EXTENSION];//
	int gsmoffset;					/*!< channel number in span */
	int logicalspan;				/*!< logical span number within trunk group */
	int mastertrunkgroup;			/*!< what trunk group is our master */
	struct sig_gsm_callback *calls;
	void *chan_pvt;					/*!< Private structure of the user of this module. */
 };

  struct sig_gsm_callback
  {
	void (* const unlock_private)(void *pvt);
    void (* const deadlock_avoidance_private)(void *pvt);
	void (* const lock_private)(void *pvt);
	int (* const play_tone)(void *pvt, enum sig_gsm_tone tone);
	int (* const set_echocanceller)(void *pvt, int enable);
    struct ast_channel * (* const new_ast_channel)(void *pvt, int state, int startpbx, enum gsm_sub sub,const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, struct ast_callid *callid);
	void (* const fixup_chans)(void *old_chan, void *new_chan);
	void (* const handle_dchan_exception)(struct sig_gsm_span *gsm); /* Note: Called with GSM lock held */
	void (* const set_alarm)(void *pvt, int in_alarm);
	void (* const set_dialing)(void *pvt, int is_dialing);
	void (* const set_digital)(void *pvt, int is_digital);
    int (* const allocate_sub)(void *pvt, enum gsm_sub sub);
	int (* const unallocate_sub)(void *pvt, enum gsm_sub sub);
    void (* const set_new_owner)(void *pvt, struct ast_channel *new_owner);
	void (* const queue_control)(void *pvt, int subclass);
	void (* const make_cc_dialstring)(void *pvt, char *buf, size_t buf_size);
	void (* const update_span_devstate)(struct sig_gsm_span *gsm);
	void (* const open_media)(void *pvt);
	void (*module_ref)(void); /*! Reference the parent module. */
	void (*module_unref)(void); /*! Unreference the parent module. */
 };

 struct sig_gsm_span
 {
	int dchan_logical_span;		/*!< Logical offset the DCHAN sits in */
	int fd;					/*!< FD's for d-channels */
    int sig;
	/*! \brief TRUE if held calls are transferred on disconnect. */
	unsigned int hold_disconnect_transfer:1;
	/*!
	 * \brief TRUE if call transfer is enabled for the span.
	 * \note Support switch-side transfer (called 2BCT, RLT or other names)
	 */
	unsigned int transfer:1;
	/*! \brief TRUE if we will allow incoming ISDN call waiting calls. */
	unsigned int allow_call_waiting_calls:1;
    struct gsm_cfg conf;
    unsigned int  span_id;
	long resetinterval;						/*!< Interval (in seconds) for resetting unused channels */
	int trunkgroup;							/*!< What our trunkgroup is */
	int dchanavail;		/*!< Whether each channel is available */
	int debug;								/*!< set to true if to dump GSM event info */
	int resetting;							/*!< true if span is being reset/restarted */
	int resetpos;							/*!< current position during a reset (-1 if not started) */
	int numchans;	/*!< Num of channels we represent */
    unsigned char sms_stats;
   //only one chan in one span
    struct sig_gsm_chan *pvts;/*!< Member channel pvt structs */
	pthread_t master;							/*!< Thread of master */
    pthread_t mon;
	ast_mutex_t lock;							/*!< libpri access Mutex */
	time_t lastreset;							/*!< time when unused channels were last reset */
	struct sig_gsm_callback *calls;  
    char last_cmd[512];
    char load;
 };

  struct gcom_gsm
  {
	int dchannel;
	int mastertrunkgroup;					
	int gsmlogicalspan;						
	struct sig_gsm_span gsm;
 };

 void gsm_event_alarm(struct sig_gsm_span *gsm_span);
 void gsm_event_noalarm(struct sig_gsm_span *gsm_span);
 int sig_gsm_call(struct sig_gsm_chan *p, struct ast_channel *ast, char *rdest);
 int sig_gsm_answer(struct sig_gsm_chan *p, struct ast_channel *ast);
 int sig_gsm_hangup(struct sig_gsm_chan *p, struct ast_channel *ast);
 struct ast_channel * sig_gsm_request(struct sig_gsm_chan *p, int *callwait, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor);
 int sig_gsm_call_hangup_ind( unsigned int span_id, int cause, unsigned char callid);
 int sig_gsm_call_hangup_cfm( unsigned int span_id, unsigned int call_id, int result );
 int sig_gsm_call_ind( unsigned int span_id,  struct gsm_call_data *event);
 int sig_gsm_call_report( unsigned int span_id, unsigned int call_id, int result );
 int sig_gsm_call_hangup_req( unsigned int span_id, unsigned int call_id);
 int sig_gsm_span_sms_fail_log(unsigned int span_id,char *to, int len,char *cause);
 int sig_gsm_sms_fail_log(char* cause);
 void init_gsm_spans(struct sig_gsm_span *span);
 int sig_gsm_start_gsm(struct sig_gsm_span *gsm);
 int gsm_str_charset2int(const char *str_charset);
 int gsm_str_smsmode2int(const char *str_mode);
 int gsm_str_numplan2int(const char *str_to_plan);
 int gsm_str_numtype2int(const char *str_to_type);
 int gsm_str_vpt2int(const char *str_vpt);
 int gsm_str_msgcalss2int(const char *str_class);
 int gsm_str_encrypt2int(const char *str_encrypt);
 char *parse_lang_charset(char *lang, unsigned char *charset);
 int sig_gsm_start_span(struct sig_gsm_span *span);
 void sig_gsm_stop_span(struct sig_gsm_span *span);
 void sig_gsm_init_span(struct sig_gsm_span *span);
 void sig_gsm_load(int maxspans);
 void sig_gsm_unload(void);
 int sig_libgsm_load(void);
 void sig_gsm_init_gsm(struct sig_gsm_span *gsm);
 void gsm_span_process_read(unsigned char span_id, void *data, int  len);
 struct sig_gsm_chan *sig_gsm_chan_new(void *pvt_data, struct sig_gsm_callback *callback, struct sig_gsm_span *gsm, int logicalspan, int channo, int trunkgroup);
 int sig_gsm_send_sms(unsigned int span, struct gsm_sms_event_data *event);
 void sig_gsm_exec_at(struct sig_gsm_span *gsm, const char *at_cmd);
 int sig_gsm_digit_begin(struct sig_gsm_chan *pvt, struct ast_channel *ast, char digit);
 char *sig_gsm_show_span(char *dest, struct sig_gsm_span *gsm);
 char *sig_gsm_show_span_verbose(char *dest, struct sig_gsm_span *gsm);
 char *handle_gsm_send_sms(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
 char *handle_gsm_exec_at(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
 int sig_gsm_get_span_siminfo_imei(int span_id, char *imei, int len);
 int sig_gsm_get_span_siminfo_imsi(int span_id, char *imsi, int len);
 int sig_gsm_get_span_siminfo_version(int span_id, char *version, int len);
 int  sig_gsm_get_span_siminfo_smsc(int span_id, char *smsc, int len);
 int sig_gsm_get_span_stats(int span, char *status, char *active, int len);
 int sig_gsm_get_span_csq(int span, int *csq);
 int sig_gsm_EncodeUCS2( unsigned char* pSrc, unsigned char* pDst, size_t  nSrcLength, int *out_len);
 int  sig_gsm_decode_pdu_content(unsigned char *out_data, int *out_data_len, char *in_content,int charset);

#endif /* _ATCOM_GCOM_H */
