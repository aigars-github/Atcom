/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief GCOM for Pseudo TDM
 *
 * \author 
 *
 * Connects to the GCOM telephony library as well as
 * libpri. Libpri is optional and needed only if you are
 * going to use ISDN connections.
 *
 * You need to install libgsmcom before you attempt to compile
 * and install the GCOM channel.
 *
 * \par See also
 * \arg \ref Config_gcom
 *
 * \ingroup channel_drivers
 *
 * \todo Deprecate the "musiconhold" configuration option post 1.4
 */

/*** MODULEINFO
	<use>res_smdi</use>
	<depend>dahdi</depend>
	<depend>gsmcom</depend>
	<depend>tonezone</depend>
	<support_level>core</support_level>

 ***/



#include "asterisk.h"


//ASTERISK_FILE_VERSION(__FILE__, "$Revision: 336636 $")

#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <math.h>
#include <ctype.h>

#include <dahdi/user.h>
#include <dahdi/tonezone.h>

typedef int64_t format_t;

//#include "sig_analog.h"


/* Analog signaling is currently still present in chan_dahdi for use with
 * radio. Sig_analog does not currently handle any radio operations. If
 * radio only uses analog signaling, then the radio handling logic could
 * be placed in sig_analog and the duplicated code could be removed.
 */
 

#include <libgsmcom.h>
#include "sig_gsm.h"

#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <dirent.h>
#include <stdlib.h>
#include "asterisk.h"
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"
#include "asterisk/adsi.h"
#include "asterisk/cli.h"
#include "asterisk/cdr.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include "asterisk/dsp.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/test.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/ast_version.h"


#define ASTERISK_VERSION_NUM 150200
//(atoi(ast_get_version_num()))

#ifndef ASTERISK_VERSION_NUM
#error  "---------------------------------------------------------------------------------"
#error "No define ASTERISK_VERSION_NUM"
#error "Get Asterisk version number:"
#error "awk -F. '{printf \"%01d%02d%02d\", $1, $2, $3}' .version"
#error "---------------------------------------------------------------------------------"
#else
#define STRING2(x) #x
#define STRING(x) STRING2(x)
#pragma message "ASTERISK_VERSION_NUM = " STRING(ASTERISK_VERSION_NUM)
#endif //ASTERISK_VERSION_NUM

#ifndef AST_MODULE
#define AST_MODULE "chan_gcom"
#endif


#include "asterisk/cel.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/tdd.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/astdb.h"
#include "asterisk/manager.h"
#include "asterisk/causes.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"
#include "asterisk/transcap.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/smdi.h"
#include "asterisk/astobj.h"
#include "asterisk/event.h"
#include "asterisk/devicestate.h"
#include "asterisk/paths.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xinclude.h>
#include "asterisk/ccss.h"
// #include "asterisk/data.h"
#ifndef AST_FORMAT_ALAW
#include "asterisk/format_compatibility.h"
#include "asterisk/format_cache.h"
#endif


#define SMDI_MD_WAIT_TIMEOUT 1500 /* 1.5 seconds */

static const char * const lbostr[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
};

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in chan_dahdi.conf.sample */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};
static struct ast_jb_conf global_jbconf;

/*!
 * \note Define ZHONE_HACK to cause us to go off hook and then back on hook when
 * the user hangs up to reset the state machine so ring works properly.
 * This is used to be able to support kewlstart by putting the zhone in
 * groundstart mode since their forward disconnect supervision is entirely
 * broken even though their documentation says it isn't and their support
 * is entirely unwilling to provide any assistance with their channel banks
 * even though their web site says they support their products for life.
 */
/* #define ZHONE_HACK */

/*! \brief Typically, how many rings before we should send Caller*ID */
#define DEFAULT_CIDRINGS 1

#define AST_LAW(p) (((p)->law == DAHDI_LAW_ALAW) ? ast_format_alaw : ast_format_ulaw)

static const char tdesc[] = "GSM/GPRS/CDMA Telephony Driver"
#if defined(HAVE_GSMCOM)
	"GSMCOM"				
#endif	/* defined(HAVE_GSMCOM) */
;



static char qdir[255];
static const char config[] = "chan_gcom.conf";
#define HAVE_GSMCOM 1



static char *app_sendsms = "SendSMS";
static char *sendsms_synopsis = "SendSMS(Span,Phone,Content)";
static char *sendsms_desc =
"SendSMS(Span,Phone,Content)\n"
"  Span - Span NO in chan_gcom.conf\n"
"  Phone - Phone NUM\n"
"  Content - content of the SMS\n";

#define SIG_GSM      (0x0800000 | DAHDI_SIG_CLEAR)

#define SIG_GSM_NUM_DCHANS  4

#define SIG_FXSLS	DAHDI_SIG_FXSLS
#define SIG_FXSGS	DAHDI_SIG_FXSGS
#define SIG_FXSKS	DAHDI_SIG_FXSKS

#ifdef LOTS_OF_SPANS
#define NUM_SPANS	DAHDI_MAX_SPANS
#else
#define NUM_SPANS 		32
#endif

#define CHAN_PSEUDO	-2

#define CALLPROGRESS_PROGRESS		1
#define CALLPROGRESS_FAX_OUTGOING	2
#define CALLPROGRESS_FAX_INCOMING	4
#define CALLPROGRESS_FAX		(CALLPROGRESS_FAX_INCOMING | CALLPROGRESS_FAX_OUTGOING)



#define ISTRUNK(p) ( (p->sig == SIG_GSM))

#define CANBUSYDETECT(p) (ISTRUNK(p) )
#define CANPROGRESSDETECT(p) (ISTRUNK(p)  )

#define _SHOWUSAGE_ CLI_SHOWUSAGE
#define _SUCCESS_ CLI_SUCCESS
#define _FAILURE_ CLI_FAILURE

#if !(ASTERISK_VERSION_NUM > 10442) && !defined(ast_verb)
#define VERBOSITY_ATLEAST(level) (option_verbose >= (level))
#define ast_verb(level, ...) do { \
	if (VERBOSITY_ATLEAST((level)) ) { \
		if (level >= 4) \
			ast_verbose(VERBOSE_PREFIX_4 __VA_ARGS__); \
		else if (level == 3) \
			ast_verbose(VERBOSE_PREFIX_3 __VA_ARGS__); \
		else if (level == 2) \
			ast_verbose(VERBOSE_PREFIX_2 __VA_ARGS__); \
		else if (level == 1) \
			ast_verbose(VERBOSE_PREFIX_1 __VA_ARGS__); \
		else \
			ast_verbose(__VA_ARGS__); \
	} \
} while (0)
#endif //!(ASTERISK_VERSION_NUM > 10442) && !defined(ast_verb)

#if !(ASTERISK_VERSION_NUM > 10442) && !defined(ast_debug)
#define ast_debug(level, ...) do {       \
	if (option_debug >= (level) ) \
		ast_log(LOG_DEBUG, __VA_ARGS__); \
} while (0)
#endif



/*! Run this script when the MWI state changes on an FXO line, if mwimonitor is enabled */

static char progzone[10] = "";


static int numbufs = 4;

static int dtmfcid_level = 256;

#define REPORT_CHANNEL_ALARMS 1
#define REPORT_SPAN_ALARMS    2 
static int report_alarms = REPORT_CHANNEL_ALARMS;

static int gsmdebugfd = -1;
static char gsmdebugfilename[1024] = "";
static int trace_debug = 0;

AST_MUTEX_DEFINE_STATIC(iflock);

static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int ifcount = 0;

AST_MUTEX_DEFINE_STATIC(gsmdebugfdlock);
AST_MUTEX_DEFINE_STATIC(monlock);
static ast_cond_t ss_thread_complete;

AST_MUTEX_DEFINE_STATIC(ss_thread_lock);
AST_MUTEX_DEFINE_STATIC(restart_lock);
static int ss_thread_count = 0;
static int num_restart_pending = 0;


static enum ast_bridge_result gcom_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms);

static int gcom_sendtext(struct ast_channel *c, const char *text);

static inline int gcom_get_event(int fd)
{
	int j;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1)
		return -1;
	return j;
}
static inline int gcom_wait_event(int fd)
{
	int i, j = 0;
	i = DAHDI_IOMUX_SIGEVENT;
	if (ioctl(fd, DAHDI_IOMUX, &i) == -1)
		return -1;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1)
		return -1;
	return j;
}


#define MASK_AVAIL		(1 << 0)	/*!< Channel available for GSM use */
#define MASK_INUSE		(1 << 1)	/*!< Channel currently in use */

#define CALLWAITING_SILENT_SAMPLES		((300 * 8) / READ_SIZE) /*!< 300 ms */
#define CALLWAITING_REPEAT_SAMPLES		((10000 * 8) / READ_SIZE) /*!< 10,000 ms */
#define CALLWAITING_SUPPRESS_SAMPLES	((100 * 8) / READ_SIZE) /*!< 100 ms */
#define CIDCW_EXPIRE_SAMPLES			((500 * 8) / READ_SIZE) /*!< 500 ms */
#define MIN_MS_SINCE_FLASH				((2000) )	/*!< 2000 ms */
#define DEFAULT_RINGT 					((8000 * 8) / READ_SIZE) /*!< 8,000 ms */

struct gcom_pvt;

static struct gcom_parms_pseudo
{
	int buf_no;					/*!< Number of buffers */
	int buf_policy;				/*!< Buffer policy */
	int faxbuf_no;              /*!< Number of Fax buffers */
	int faxbuf_policy;          /*!< Fax buffer policy */
} gcom_pseudo_parms;
static struct gcom_gsm gsms[GSM_MAX_SPANS];
static const char * const subnames[] =
{
	"Real",
	"Callwait",
	"Threeway"
};


#define CONF_USER_REAL		(1 << 0)
#define CONF_USER_THIRDCALL	(1 << 1)
#define MAX_SLAVES	4

/*! Specify the lists gcom_pvt can be put in. */
enum GCOM_IFLIST {
	GCOM_IFLIST_NONE,	/*!< The gcom_pvt is not in any list. */
	GCOM_IFLIST_MAIN,	/*!< The gcom_pvt is in the main interface list */
};

#define SUB_REAL	0			/*!< Active call */
#define SUB_CALLWAIT	1			/*!< Call-Waiting call on hold */
#define SUB_THREEWAY	2			/*!< Three-way call */

struct gcom_subchannel
{
	int dfd;
	struct ast_channel *owner;
	int chan;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct ast_frame f;		/*!< One frame for each channel.  How did this ever work before? */
	unsigned int needringing:1;
	unsigned int needbusy:1;
	unsigned int needcongestion:1;
	unsigned int needanswer:1;
	unsigned int needflash:1;
	unsigned int needhold:1;
	unsigned int needunhold:1;
	unsigned int linear:1;
	unsigned int inthreeway:1;
	struct dahdi_confinfo curconf;
};

struct gcom_pvt {
	ast_mutex_t lock;					/*!< Channel private lock. */
	struct callerid_state *cs;
	struct ast_channel *owner;			/*!< Our current active owner (if applicable) */
							/*!< Up to three channels can be associated with this call */

	struct gcom_subchannel sub_unused;		/*!< Just a safety precaution */
	struct gcom_subchannel subs[3];			/*!< Sub-channels */
	struct dahdi_confinfo saveconf;			/*!< Saved conference info */

	struct gcom_pvt *slaves[MAX_SLAVES];		/*!< Slave to us (follows our conferencing) */
	struct gcom_pvt *master;				/*!< Master to us (we follow their conferencing) */
	int inconference;				/*!< If our real should be in the conference */

	int bufsize;                /*!< Size of the buffers */
	int buf_no;					/*!< Number of buffers */
	int buf_policy;				/*!< Buffer policy */
	int faxbuf_no;              /*!< Number of Fax buffers */
	int faxbuf_policy;          /*!< Fax buffer policy */
	int sig;					/*!< Signalling style */
	/*!
	 * \brief Nonzero if the signaling type is sent over a radio.
	 * \note Set to a couple of nonzero values but it is only tested like a boolean.
	 */
	int radio;
	int outsigmod;					/*!< Outbound Signalling style (modifier) */
	int oprmode;					/*!< "Operator Services" mode */
	struct gcom_pvt *oprpeer;				/*!< "Operator Services" peer tech_pvt ptr */
	/*! \brief Amount of gain to increase during caller id */
	float cid_rxgain;
	/*! \brief Rx gain set by chan_dahdi.conf */
	float rxgain;
	/*! \brief Tx gain set by chan_dahdi.conf */
	float txgain;

	float txdrc; /*!< Dynamic Range Compression factor. a number between 1 and 6ish */
	float rxdrc;
	
	int tonezone;					/*!< tone zone for this chan, or -1 for default */
	enum GCOM_IFLIST which_iflist;	/*!< Which interface list is this structure listed? */
	struct gcom_pvt *next;				/*!< Next channel in list */
	struct gcom_pvt *prev;				/*!< Prev channel in list */

	/*! \brief Automatic Number Identification number (Alternate GSM caller ID number) */
	char cid_ani[AST_MAX_EXTENSION];
	/*!
	 * \brief TRUE if ADSI (Analog Display Services Interface) available
	 * \note Set from the "adsi" value read in from chan_dahdi.conf
	 */
	unsigned int adsi:1; //del later
	/*!
	 * \brief TRUE if we can use a polarity reversal to mark when an outgoing
	 * call is answered by the remote party.
	 * \note Set from the "answeronpolarityswitch" value read in from chan_dahdi.conf
	 */
	unsigned int answeronpolarityswitch:1;
	/*!
	 * \brief TRUE if busy detection is enabled.
	 * (Listens for the beep-beep busy pattern.)
	 * \note Set from the "busydetect" value read in from chan_dahdi.conf
	 */
	unsigned int busydetect:1;
	/*!
	 * \brief TRUE if call return is enabled.
	 * (*69, if your dialplan doesn't catch this first)
	 * \note Set from the "callreturn" value read in from chan_dahdi.conf
	 */
	unsigned int callreturn:1;
	/*!
	 * \brief TRUE if busy extensions will hear the call-waiting tone
	 * and can use hook-flash to switch between callers.
	 * \note Can be disabled by dialing *70.
	 * \note Initialized with the "callwaiting" value read in from chan_dahdi.conf
	 */
	unsigned int callwaiting:1;
	/*!
	 * \brief TRUE if send caller ID for Call Waiting
	 * \note Set from the "callwaitingcallerid" value read in from chan_dahdi.conf
	 */
	unsigned int callwaitingcallerid:1;
	/*!
	 * \brief TRUE if support for call forwarding enabled.
	 * Dial *72 to enable call forwarding.
	 * Dial *73 to disable call forwarding.
	 * \note Set from the "cancallforward" value read in from chan_dahdi.conf
	 */
	unsigned int cancallforward:1;
	/*!
	 * \brief TRUE if support for call parking is enabled.
	 * \note Set from the "canpark" value read in from chan_dahdi.conf
	 */
	unsigned int canpark:1;
	/*! \brief TRUE if to wait for a DTMF digit to confirm answer */
	unsigned int confirmanswer:1;
	/*!
	 * \brief TRUE if the channel is to be destroyed on hangup.
	 * (Used by pseudo channels.)
	 */
	unsigned int destroy:1;
	unsigned int didtdd:1;				/*!< flag to say its done it once */
	/*! \brief TRUE if analog type line dialed no digits in Dial() */
	unsigned int dialednone:1;
	/*!
	 * \brief TRUE if in the process of dialing digits or sending something.
	 * \note This is used as a receive squelch for ISDN until connected.
	 */
	unsigned int dialing:1;
	/*! \brief TRUE if the transfer capability of the call is digital. */
	unsigned int digital:1;
	/*! \brief TRUE if Do-Not-Disturb is enabled, present only for non sig_analog */
	unsigned int dnd:1;
	/*! \brief XXX BOOLEAN Purpose??? */
	unsigned int echobreak:1;
	/*!
	 * \brief TRUE if echo cancellation enabled when bridged.
	 * \note Initialized with the "echocancelwhenbridged" value read in from chan_dahdi.conf
	 * \note Disabled if the echo canceller is not setup.
	 */
	unsigned int echocanbridged:1;
	/*! \brief TRUE if echo cancellation is turned on. */
	unsigned int echocanon:1;
	/*! \brief TRUE if a fax tone has already been handled. */
	unsigned int faxhandled:1;
	/*! TRUE if dynamic faxbuffers are configured for use, default is OFF */
	unsigned int usefaxbuffers:1;
	/*! TRUE while buffer configuration override is in use */
	unsigned int bufferoverrideinuse:1;
	/*! \brief TRUE if over a radio and gcom_read() has been called. */
	unsigned int firstradio:1;
	/*!
	 * \brief TRUE if the call will be considered "hung up" on a polarity reversal.
	 * \note Set from the "hanguponpolarityswitch" value read in from chan_dahdi.conf
	 */
	unsigned int hanguponpolarityswitch:1;
	/*! \brief TRUE if DTMF detection needs to be done by hardware. */
	unsigned int hardwaredtmf:1;
	/*!
	 * \brief TRUE if the outgoing caller ID is blocked/hidden.
	 * \note Caller ID can be disabled by dialing *67.
	 * \note Caller ID can be enabled by dialing *82.
	 * \note Initialized with the "hidecallerid" value read in from chan_dahdi.conf
	 */
	unsigned int hidecallerid:1;
	/*!
	 * \brief TRUE if hide just the name not the number for legacy PBX use.
	 * \note Only applies to GSM channels.
	 * \note Set from the "hidecalleridname" value read in from chan_dahdi.conf
	 */
	unsigned int hidecalleridname:1;
	/*! \brief TRUE if DTMF detection is disabled. */
	unsigned int ignoredtmf:1;
	/*!
	 * \brief TRUE if the channel should be answered immediately
	 * without attempting to gather any digits.
	 * \note Set from the "immediate" value read in from chan_dahdi.conf
	 */
	unsigned int immediate:1;
	/*! \brief TRUE if in an alarm condition. */
	unsigned int inalarm:1;
	/*! \brief TRUE if TDD in MATE mode */
	unsigned int mate:1;
	/*! \brief TRUE if we originated the call leg. */
	unsigned int outgoing:1;
	/* unsigned int overlapdial:1; 			unused and potentially confusing */
	/*!
	 * \brief TRUE if busy extensions will hear the call-waiting tone
	 * and can use hook-flash to switch between callers.
	 * \note Set from the "callwaiting" value read in from chan_dahdi.conf
	 */
	unsigned int permcallwaiting:1;
	/*!
	 * \brief TRUE if the outgoing caller ID is blocked/restricted/hidden.
	 * \note Set from the "hidecallerid" value read in from chan_dahdi.conf
	 */
	unsigned int permhidecallerid:1;
	/*!
	 * \brief TRUE if GSM congestion/busy indications are sent out-of-band.
	 * \note Set from the "priindication" value read in from chan_dahdi.conf
	 */
	unsigned int priindication_oob:1;
	/*!
	 * \brief TRUE if GSM B channels are always exclusively selected.
	 * \note Set from the "priexclusive" value read in from chan_dahdi.conf
	 */
	unsigned int priexclusive:1;
	/*!
	 * \brief TRUE if we will pulse dial.
	 * \note Set from the "pulsedial" value read in from chan_dahdi.conf
	 */
	unsigned int pulse:1;
	/*! \brief TRUE if a pulsed digit was detected. (Pulse dial phone detected) */
	unsigned int pulsedial:1;
	unsigned int restartpending:1;		/*!< flag to ensure counted only once for restart */
	/*!
	 * \brief TRUE if caller ID is restricted.
	 * \note Set but not used.  Should be deleted.  Redundant with permhidecallerid.
	 * \note Set from the "restrictcid" value read in from chan_dahdi.conf
	 */
	unsigned int restrictcid:1;
	/*!
	 * \brief TRUE if three way calling is enabled
	 * \note Set from the "threewaycalling" value read in from chan_dahdi.conf
	 */
	unsigned int threewaycalling:1;
	/*!
	 * \brief TRUE if call transfer is enabled
	 * \note For FXS ports (either direct analog or over T1/E1):
	 *   Support flash-hook call transfer
	 * \note For digital ports using ISDN GSM protocols:
	 *   Support switch-side transfer (called 2BCT, RLT or other names)
	 * \note Set from the "transfer" value read in from chan_dahdi.conf
	 */
	unsigned int transfer:1;
	/*!
	 * \brief TRUE if caller ID is used on this channel.
	 * \note GSM and SS7 spans will save caller ID from the networking peer.
	 * \note FXS ports will generate the caller ID spill.
	 * \note FXO ports will listen for the caller ID spill.
	 * \note Set from the "usecallerid" value read in from chan_dahdi.conf
	 */
	unsigned int use_callerid:1;
	/*!
	 * \brief TRUE if we will use the calling presentation setting
	 * from the Asterisk channel for outgoing calls.
	 * \note Only applies to GSM and SS7 channels.
	 * \note Set from the "usecallingpres" value read in from chan_dahdi.conf
	 */
	unsigned int use_callingpres:1;

	/*!
	 * \brief TRUE if allowed to flash-transfer to busy channels.
	 * \note Set from the "transfertobusy" value read in from chan_dahdi.conf
	 */
	unsigned int transfertobusy:1;

	/*!
	 * \brief TRUE if channel is out of reset and ready
	 * \note Set but not used.
	 */
	unsigned int inservice:1;


	/*!
	 * \brief TRUE if the channel alarms will be managed also as Span ones
	 * \note Applies to all channels
	 */
	unsigned int manages_span_alarms:1;

	struct sig_gsm_span *gsm;
	int logicalspan;

	/*!
	 * \brief The configured context for incoming calls.
	 * \note The "context" string read in from chan_dahdi.conf
	 */
	char context[AST_MAX_CONTEXT];
	/*!
	 * \brief Saved context string.
	 */
	char defcontext[AST_MAX_CONTEXT];
	/*! \brief Extension to use in the dialplan. */
	char exten[AST_MAX_EXTENSION];
	/*!
	 * \brief Language configured for calls.
	 * \note The "language" string read in from chan_dahdi.conf
	 */
	char language[MAX_LANGUAGE];
	/*!
	 * \brief The configured music-on-hold class to use for calls.
	 * \note The "musicclass" or "mohinterpret" or "musiconhold" string read in from chan_dahdi.conf
	 */
	char mohinterpret[MAX_MUSICCLASS];
	/*!
	 * \brief Suggested music-on-hold class for peer channel to use for calls.
	 * \note The "mohsuggest" string read in from chan_dahdi.conf
	 */
	char mohsuggest[MAX_MUSICCLASS];
	char parkinglot[AST_MAX_EXTENSION]; /*!< Parking lot for this channel */

	/*! \brief Automatic Number Identification code from GSM */
	int cid_ani2;
	/*! \brief Caller ID number from an incoming call. */
	char cid_num[AST_MAX_EXTENSION];
	/*!
	 * \brief Caller ID tag from incoming call
	 * \note the "cid_tag" string read in from chan_dahdi.conf
	 */
	char cid_tag[AST_MAX_EXTENSION];

	/*! \brief Caller ID name from an incoming call. */
	char cid_name[AST_MAX_EXTENSION];
	/*! \brief Caller ID subaddress from an incoming call. */
	char cid_subaddr[AST_MAX_EXTENSION];
	char *origcid_num;				/*!< malloced original callerid */
	char *origcid_name;				/*!< malloced original callerid */
	/*! \brief Call waiting number. */
	char callwait_num[AST_MAX_EXTENSION];
	/*! \brief Call waiting name. */
	char callwait_name[AST_MAX_EXTENSION];
	/*! \brief Redirecting Directory Number Information Service (RDNIS) number */
	char rdnis[AST_MAX_EXTENSION];
	/*! \brief Dialed Number Identifier */
	char dnid[AST_MAX_EXTENSION];
	/*!
	 * \brief Bitmapped groups this belongs to.
	 * \note The "group" bitmapped group string read in from chan_dahdi.conf
	 */
	ast_group_t group;
	/*! \brief Default call PCM encoding format: DAHDI_LAW_ALAW or DAHDI_LAW_MULAW. */
	int law_default;
	/*! \brief Active PCM encoding format: DAHDI_LAW_ALAW or DAHDI_LAW_MULAW */
	int law;
	int confno;					/*!< Our conference */
	int confusers;					/*!< Who is using our conference */
	int propconfno;					/*!< Propagated conference number */

	/*!
	 * \brief Channel variable list with associated values to set when a channel is created.
	 * \note The "setvar" strings read in from chan_dahdi.conf
	 */
	struct ast_variable *vars;
	int channel;					/*!< Channel Number */
	int span;					/*!< Span number */
	time_t guardtime;				/*!< Must wait this much time before using for new call */
	int dtmfcid_holdoff_state;		/*!< State indicator that allows for line to settle before checking for dtmf energy */
	struct timeval	dtmfcid_delay;  /*!< Time value used for allow line to settle */
	int callingpres;				/*!< The value of calling presentation that we're going to use when placing a GSM call */
	int callwaitingrepeat;				/*!< How many samples to wait before repeating call waiting */

       /*jwj cid start will del */
       int cidcwexpire;				/*!< When to stop waiting for CID/CW CAS response (In samples) */
	int cid_suppress_expire;		/*!< How many samples to suppress after a CID spill. */
	/*! \brief Analog caller ID waveform sample buffer */
	unsigned char *cidspill;//jwj must del gsm not have this item
	/*! \brief Position in the cidspill buffer to send out next. */
	int cidpos;
	/*! \brief Length of the cidspill buffer containing samples. */
	int cidlen;
       /*jwj  cid end*/
       
	/*! \brief Ring timeout timer?? */
	int ringt;

	/*!
	 * \brief Number of most significant digits/characters to strip from the dialed number.
	 * \note Feature is deprecated.  Use dialplan logic.
	 * \note The characters are stripped before the GSM TON/NPI prefix
	 * characters are processed.
	 */
	int stripmsd;
	/*!
	 * \brief TRUE if Call Waiting (CW) CPE Alert Signal (CAS) is being sent.
	 * \note
	 * After CAS is sent, the call waiting caller id will be sent if the phone
	 * gives a positive reply.
	 */
	int callwaitcas;
	/*! \brief Number of call waiting rings. */
	int callwaitrings;
	/*! \brief Echo cancel parameters. */
	struct {
		struct dahdi_echocanparams head;
		struct dahdi_echocanparam params[DAHDI_MAX_ECHOCANPARAMS];
	} echocancel;
	/*!
	 * \brief Echo training time. 0 = disabled
	 * \note Set from the "echotraining" value read in from chan_dahdi.conf
	 */
	int echotraining;
	/*! \brief Filled with 'w'.  XXX Purpose?? */
	char echorest[20];
	/*!
	 * \brief Number of times to see "busy" tone before hanging up.
	 * \note Set from the "busycount" value read in from chan_dahdi.conf
	 */
	int busycount;

	struct ast_dsp_busy_pattern busy_cadence;

	/*!
	 * \brief Bitmapped call progress detection flags. CALLPROGRESS_xxx values.
	 * \note Bits set from the "callprogress" and "faxdetect" values read in from chan_dahdi.conf
	 */
	int callprogress;
	/*!
	 * \brief Number of milliseconds to wait for dialtone.
	 */
	struct timeval waitingfordt;			/*!< Time we started waiting for dialtone */
	struct timeval flashtime;			/*!< Last flash-hook time */
	/*! \brief Opaque DSP configuration structure. */
	struct ast_dsp *dsp;
	/*! \brief DAHDI dial operation command struct for ioctl() call. */
	struct dahdi_dialoperation dop;

	char finaldial[64];
	char accountcode[AST_MAX_ACCOUNT_CODE];		/*!< Account code */
	int amaflags;					/*!< AMA Flags */
	struct tdd_state *tdd;				/*!< TDD flag */
	/*! \brief Accumulated call forwarding number. */
	char call_forward[AST_MAX_EXTENSION];

	/*! \brief Opaque event subscription parameters for message waiting indication support. */
    
	/*! \brief Delayed dialing for E911.  Overlap digits for ISDN. */
	char dialdest[256];
	int distinctivering;				/*!< Which distinctivering to use */
	int dtmfrelax;					/*!< whether to run in relaxed DTMF mode */
	/*! \brief Holding place for event injected from outside normal operation. */
	int fake_event;
	/*!
	 * \brief Minimal time period (ms) between the answer polarity
	 * switch and hangup polarity switch.
	 */
	int polarityonanswerdelay;
	/*! \brief Start delay time if polarityonanswerdelay is nonzero. */
	struct timeval polaritydelaytv;

	/*! \brief Current line interface polarity. POLARITY_IDLE, POLARITY_REV */
	int polarity;
	/*! \brief DSP feature flags: DSP_FEATURE_xxx */
	int dsp_features;

	/*! \brief DTMF digit in progress.  0 when no digit in progress. */
	char begindigit;
	/*! \brief TRUE if confrence is muted. */
	int muting;
	void *sig_pvt;
    
       #if ( ASTERISK_VERSION_NUM >= 10800 )
	struct ast_cc_config_params *cc_params;
       #endif
	/* gcom channel names may differ greatly from the
	 * string that was provided to an app such as Dial. We
	 * need to save the original string passed to dahdi_request
	 * for call completion purposes. This way, we can replicate
	 * the original dialed string later.
	 */
	char dialstring[AST_CHANNEL_NAME];
};


 struct queue_sms
 {
   char called[GSM_MAX_SMS_QUEUE_NUM][GSM_CALL_NUM_LEN];
   unsigned char context[ GSM_MAX_CONTENT_LEN ];
   int  idx;
   int spanid;
 };

#define DATA_EXPORT_DAHDI_PVT(MEMBER)					\
	MEMBER(gcom_pvt, cid_rxgain, AST_DATA_DOUBLE)			\
	MEMBER(gcom_pvt, rxgain, AST_DATA_DOUBLE)			\
	MEMBER(gcom_pvt, txgain, AST_DATA_DOUBLE)			\
	MEMBER(gcom_pvt, txdrc, AST_DATA_DOUBLE)			\
	MEMBER(gcom_pvt, rxdrc, AST_DATA_DOUBLE)			\
	MEMBER(gcom_pvt, adsi, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, answeronpolarityswitch, AST_DATA_BOOLEAN)	\
	MEMBER(gcom_pvt, busydetect, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, callreturn, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, callwaiting, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, callwaitingcallerid, AST_DATA_BOOLEAN)	\
	MEMBER(gcom_pvt, cancallforward, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, canpark, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, confirmanswer, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, destroy, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, didtdd, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, dialednone, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, dialing, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, digital, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, dnd, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, echobreak, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, echocanbridged, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, echocanon, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, faxhandled, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, usefaxbuffers, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, bufferoverrideinuse, AST_DATA_BOOLEAN)	\
	MEMBER(gcom_pvt, firstradio, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, hanguponpolarityswitch, AST_DATA_BOOLEAN)	\
	MEMBER(gcom_pvt, hardwaredtmf, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, hidecallerid, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, hidecalleridname, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, ignoredtmf, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, immediate, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, inalarm, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, mate, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, outgoing, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, permcallwaiting, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, priindication_oob, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, priexclusive, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, pulse, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, pulsedial, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, restartpending, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, restrictcid, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, threewaycalling, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, transfer, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, use_callerid, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, use_callingpres, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, transfertobusy, AST_DATA_BOOLEAN)			\
	MEMBER(gcom_pvt, inservice, AST_DATA_BOOLEAN)				\
	MEMBER(gcom_pvt, manages_span_alarms, AST_DATA_BOOLEAN)		\
	MEMBER(gcom_pvt, context, AST_DATA_STRING)				\
	MEMBER(gcom_pvt, defcontext, AST_DATA_STRING)				\
	MEMBER(gcom_pvt, exten, AST_DATA_STRING)				\
	MEMBER(gcom_pvt, language, AST_DATA_STRING)				\
	MEMBER(gcom_pvt, mohinterpret, AST_DATA_STRING)			\
	MEMBER(gcom_pvt, mohsuggest, AST_DATA_STRING)				\
	MEMBER(gcom_pvt, parkinglot, AST_DATA_STRING)

AST_DATA_STRUCTURE(gcom_pvt, DATA_EXPORT_DAHDI_PVT);

static struct gcom_pvt *iflist = NULL;	/*!< Main interface list start */
static struct gcom_pvt *ifend = NULL;	/*!< Main interface list end */


/*! \brief Channel configuration from chan_dahdi.conf .
 * This struct is used for parsing the [channels] section of chan_dahdi.conf.
 * Generally there is a field here for every possible configuration item.
 *
 * The state of fields is saved along the parsing and whenever a 'channel'
 * statement is reached, the current gcom_chan_conf is used to configure the
 * channel (struct gcom_pvt)
 *
 * \see dahdi_chan_init for the default values.
 */
struct gcom_chan_conf
{
	struct gcom_pvt chan;
	struct gcom_gsm gsm;
	int is_sig_auto; /*!< Use channel signalling from DAHDI? */
	/*! Continue configuration even if a channel is not there. */
	int ignore_failed_channels;
	/*!
	 * \brief The serial port to listen for SMDI data on
	 * \note Set from the "smdiport" string read in from chan_dahdi.conf
	 */
};

/*! returns a new gcom_chan_conf with default values (by-value) */
static struct gcom_chan_conf gcom_chan_conf_default(void)
{
	/* recall that if a field is not included here it is initialized
	 * to 0 or equivalent
	 */
	struct gcom_chan_conf conf = {
		.gsm.gsm.conf = {//gsm_span parmeter
			.switchtype =1, //for m50
			.nodetype = GSM_CPE,
                     .countrycode = "",
                     .numtype = "",
		},
		.chan = {
			.context = "default",
			.cid_num = "",
			.cid_name = "",
			.cid_tag = "",
			.mohinterpret = "default",
			.mohsuggest = "",
			.parkinglot = "",
			.transfertobusy = 1,
			.use_callerid = 1,
			.sig = -1,
			.outsigmod = -1,
			.cid_rxgain = +5.0,
			.tonezone = -1,
			.echocancel.head.tap_length = 1024,
			.busycount = 3,
			.accountcode = "",
			.polarityonanswerdelay = 600,
			.buf_policy = DAHDI_POLICY_IMMEDIATE,
			.buf_no = numbufs,
			.usefaxbuffers = 0,
			.cc_params = ast_cc_config_params_init(),
		},
		.is_sig_auto = 1,
	};

	return conf;
}

static struct ast_channel *gcom_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int gcom_digit_begin(struct ast_channel *ast, char digit);
static int gcom_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int gcom_sendtext(struct ast_channel *c, const char *text);
static int gcom_call(struct ast_channel *ast, char *rdest, int timeout);
static int gcom_hangup(struct ast_channel *ast);
static int gcom_answer(struct ast_channel *ast);
static struct ast_frame *gcom_read(struct ast_channel *ast);
static int gcom_write(struct ast_channel *ast, struct ast_frame *frame);
static struct ast_frame *gcom_exception(struct ast_channel *ast);
static int gcom_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen);
static int gcom_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int gcom_setoption(struct ast_channel *chan, int option, void *data, int datalen);
static int gcom_queryoption(struct ast_channel *chan, int option, void *data, int *datalen);
static int gcom_func_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len);
static int gcom_func_write(struct ast_channel *chan, const char *function, char *data, const char *value);
static int gcom_devicestate(void *data);
static int gcom_cc_callback(struct ast_channel *inbound, const char *dest, ast_cc_callback_fn callback);
static struct ast_xml_doc *ast_xml_open_utf8(char *filename);

static struct ast_channel_tech gcom_tech = {
	.type = "GCOM",
	.capabilities =  AST_FORMAT_ULAW | AST_FORMAT_ALAW,
	.description = tdesc,
	.requester = gcom_request,
	.send_digit_begin = gcom_digit_begin,
	.send_digit_end = gcom_digit_end,
	.call = gcom_call,
	.hangup = gcom_hangup,
	.answer = gcom_answer,
	.read = gcom_read,
	.write = gcom_write,
	.send_text = gcom_sendtext,
	.exception = gcom_exception,
	.indicate = gcom_indicate,
	.fixup = gcom_fixup,//NO
	.setoption = gcom_setoption,
	.queryoption = gcom_queryoption,
	.func_channel_read = gcom_func_read,
	.func_channel_write = gcom_func_write,
	.devicestate = gcom_devicestate,
	.cc_callback = gcom_cc_callback,
};

#define GET_CHANNEL(p) ((p)->channel)

static int restart_monitor(void);
static void *sms_scan_thread(void *data);

static int send_callerid(struct gcom_pvt *p);
static int save_conference(struct gcom_pvt *p);
static int restore_conference(struct gcom_pvt *p);
static void wakeup_sub(struct gcom_pvt *p, int a);
static int reset_conf(struct gcom_pvt *p);
static inline int gcom_confmute(struct gcom_pvt *p, int muted);
static int get_alarms(struct gcom_pvt *p);
static void handle_alarms(struct gcom_pvt *p, int alms);
static int conf_del(struct gcom_pvt *p, struct gcom_subchannel *c, int index);
static int conf_add(struct gcom_pvt *p, struct gcom_subchannel *c, int index, int slavechannel);
static int isslavenative(struct gcom_pvt *p, struct gcom_pvt **out);
static struct ast_channel *gcom_new(struct gcom_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor,struct ast_callid *callid);
static void gcom_enable_ec(struct gcom_pvt *p);
static void gcom_disable_ec(struct gcom_pvt *p);
static char *alarm2str(int alm);
static const char *event2str(int event);
static int unalloc_sub(struct gcom_pvt *p, int x);
static int alloc_sub(struct gcom_pvt *p, int x);
static int gcom_longsms_send(struct gsm_sms_event_data *data, int span_id);
static int gcom_setlaw(int dfd, int law)
{
	int res;
	res = ioctl(dfd, DAHDI_SETLAW, &law);
	if (res)
		return res;
	return 0;
}


static void   my_unlock_private (void *pvt)
{
    struct gcom_pvt *p = pvt;
    ast_mutex_unlock(&p->lock);
}   

static void  my_lock_private (void *pvt)
{
    struct gcom_pvt *p = pvt;
    ast_mutex_lock(&p->lock);
}   

static void my_deadlock_avoidance_private(void *pvt)
{
        struct gcom_pvt *p = pvt;
        DEADLOCK_AVOIDANCE(&p->lock);
}

static int sig_gsm_tone_to_dahditone(enum sig_gsm_tone tone)
{
	switch (tone) {
	case SIG_GSM_TONE_RINGTONE:
		return DAHDI_TONE_RINGTONE;
	case SIG_GSM_TONE_STUTTER:
		return DAHDI_TONE_STUTTER;
	case SIG_GSM_TONE_CONGESTION:
		return DAHDI_TONE_CONGESTION;
	case SIG_GSM_TONE_DIALTONE:
		return DAHDI_TONE_DIALTONE;
	case SIG_GSM_TONE_DIALRECALL:
		return DAHDI_TONE_DIALRECALL;
	case SIG_GSM_TONE_INFO:
		return DAHDI_TONE_INFO;
	case SIG_GSM_TONE_BUSY:
		return DAHDI_TONE_BUSY;
	default:
		return -1;
	}
}

static int my_play_tone (void *pvt, enum sig_gsm_tone tone)
{
    struct gcom_pvt *p = pvt;
    return tone_zone_play_tone(p->subs[SUB_REAL].dfd, sig_gsm_tone_to_dahditone(tone));
}

static int  my_set_echocanceller (void *pvt, int enable)
{
    struct gcom_pvt *p = pvt;
	if (enable)
		gcom_enable_ec(p);
	else
		gcom_disable_ec(p);
	return 0;
}

static void  my_fixup_chans (void *old_chan, void *new_chan)
{

}

static void my_set_alarm (void *pvt, int in_alarm)
{
    const char *alarm_str = alarm2str(in_alarm);
    struct gcom_pvt *p = pvt;
    if(!p)
        return ;
    if (report_alarms & REPORT_CHANNEL_ALARMS)
    {
		ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm_str);
		manager_event(EVENT_FLAG_SYSTEM, "Alarm",
					  "Alarm: %s\r\n"
					  "Channel: %d\r\n",
					  alarm_str, p->channel);
	}
	if (report_alarms & REPORT_SPAN_ALARMS && p->manages_span_alarms) {
		ast_log(LOG_WARNING, "Detected alarm on span %d: %s\n", p->span, alarm_str);
		manager_event(EVENT_FLAG_SYSTEM, "SpanAlarm",
					  "Alarm: %s\r\n"
					  "Span: %d\r\n",
					  alarm_str, p->span);
	}
}

static  void my_set_dialing  (void *pvt, int is_dialing)
{
   struct gcom_pvt *p = pvt;
	p->dialing = is_dialing;
}

static  void my_set_digital  (void *pvt, int is_digital)
{
}


static  void  my_make_cc_dialstring  (void *pvt, char *buf, size_t buf_size)
{
}

static  void gcom_gsm_update_span_devstate (struct sig_gsm_span *gsm)
{
    
}

static  void  my_module_ref (void)
{
     ast_module_ref(ast_module_info->self);
}   

static  void my_module_unref (void)
{
    ast_module_unref(ast_module_info->self);
}

static int gsmsub_to_dahdisub(enum gsm_sub gsmsub)
{
	int index;
	switch (gsmsub) {
	case GSM_SUB_REAL:
		index = SUB_REAL;
		break;
	case GSM_SUB_CALLWAIT:
		index = SUB_CALLWAIT;
		break;
	case GSM_SUB_THREEWAY:
		index = SUB_THREEWAY;
		break;
	default:
		ast_log(LOG_ERROR, "Unidentified sub!\n");
		index = SUB_REAL;
	}
	return index;
}


 static  struct ast_channel * my_new_gsm_ast_channel (void *pvt, int state, int startpbx, enum gsm_sub sub, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor,struct ast_callid *callid)
 {
  ast_log(LOG_DEBUG,"my_new_gsm_ast_channel name start startpbx=%d\n",startpbx);
  struct gcom_pvt *p = pvt;
  struct sig_gsm_chan *c = NULL;
  int dsub = gsmsub_to_dahdisub(sub);
  if(!p)
    return NULL;
  c = p->sig_pvt;
  if (!ast_strlen_zero(c->cid_num))
  ast_copy_string(p->cid_num, c->cid_num, sizeof(p->cid_num));
  if (!ast_strlen_zero(c->cid_name))
   ast_copy_string(p->cid_name, c->cid_name, sizeof(p->cid_name));
   struct ast_channel * retChannel=gcom_new(p, state, startpbx, dsub, 0, requestor ? ast_channel_linkedid(requestor) : "",assignedids,requestor,callid);
   return retChannel;
 }


/* Note: Called with GSM lock held can alarm event for gsm batery or other */
 static  void my_handle_dchan_exception (struct sig_gsm_span *gsm)
 {
  int x = 0;
  ioctl(gsm->fd, DAHDI_GETEVENT, &x);
  if (x)
      ast_log(LOG_NOTICE, "gcom got event: %s (%d) on signalling channel of span %d fd=%d\n", event2str(x), x, gsm->span_id,gsm->fd);
  switch (x)
  {
    case DAHDI_EVENT_ALARM:
       gsm_event_alarm(gsm);
       break;
    case DAHDI_EVENT_NOALARM:
       gsm_event_noalarm(gsm);
       break;
    default:
       break;
   }
 }
    
 static  void my_open_media (void *p)
 {
   struct gcom_pvt *pvt = p;
   int res;
   int dfd;
   int set_val;
   dfd = pvt->subs[SUB_REAL].dfd;
   set_val = 1;
   res = ioctl(dfd, DAHDI_AUDIOMODE, &set_val);
   if(res < 0)
		ast_log(LOG_WARNING, "Unable to enable audio mode on channel %d (%s)\n",pvt->channel, strerror(errno));
/* Set correct companding law for this call. */
   res = gcom_setlaw(dfd, pvt->law);
   if(res < 0)
		ast_log(LOG_WARNING, "Unable to set law on channel %d\n", pvt->channel);
	if (pvt->dsp_features && pvt->dsp)
	{
		ast_dsp_set_features(pvt->dsp, pvt->dsp_features);
		pvt->dsp_features = 0;
	}
 }

 static int my_unallocate_sub(void *pvt, enum gsm_sub gsmsub)
 {
	struct gcom_pvt *p = pvt;
	return unalloc_sub(p, gsmsub_to_dahdisub(gsmsub));
 }

 static int my_allocate_sub(void *pvt, enum gsm_sub gsmsub)
 {
	struct gcom_pvt *p = pvt;
	return alloc_sub(p, gsmsub_to_dahdisub(gsmsub));
 }

 static void my_set_new_owner(void *pvt, struct ast_channel *new_owner)
 {
	struct gcom_pvt *p = pvt;
	p->owner = new_owner;
 }


 static struct sig_gsm_callback gcom_gsm_callbacks =
 {
    .lock_private = my_lock_private,
	.unlock_private = my_unlock_private,
	.play_tone = my_play_tone,
	.set_echocanceller = my_set_echocanceller,
	.deadlock_avoidance_private = my_deadlock_avoidance_private,
	.new_ast_channel = my_new_gsm_ast_channel,
	.fixup_chans = my_fixup_chans,
	.handle_dchan_exception = my_handle_dchan_exception,	
	.set_alarm = my_set_alarm,
	.set_dialing = my_set_dialing,
	.set_digital = my_set_digital,
    .allocate_sub = my_allocate_sub,
	.unallocate_sub = my_unallocate_sub,
    .set_new_owner = my_set_new_owner,
	.make_cc_dialstring = my_make_cc_dialstring,
	.update_span_devstate = gcom_gsm_update_span_devstate,
    .open_media = my_open_media,
	.module_ref = my_module_ref,
	.module_unref = my_module_unref,
 };

 static int set_actual_gain(int fd, float rxgain, float txgain, float rxdrc, float txdrc, int law);
 static int unalloc_sub(struct gcom_pvt *p, int x);
 static int alloc_sub(struct gcom_pvt *p, int x);
 static inline int gcom_wait_event(int fd);
 static int gcom_ring_phone(struct gcom_pvt *p);
 static inline int gcom_set_hook(int fd, int hs);
 static void gcom_train_ec(struct gcom_pvt *p);
/*! Round robin search locations. */
 static struct gcom_pvt *round_robin[32];

 #define gcom_get_index(ast, p, nullok)	_gcom_get_index(ast, p, nullok, __PRETTY_FUNCTION__, __LINE__)

 static char *gcom_sig2str(int sig)
 {
	static char buf[256];
	switch (sig)
	{
	 case SIG_GSM:
		return "GSM";
	 case 0:
		return "Pseudo";
	 default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d", sig);
		return buf;
	}
 }


 static char *gsm_node2str(int node)
 {
	switch(node)
	{
	 case GSM_NET:
		return "NET";
 	 case GSM_CPE:
		return "CPE";
	 default:
		return "Invalid value";
	}
 }

 static char *gsm_switch2str(int sw)
 {
  switch(sw)
  {
	case 1:
		return "QUECTEL M50";
	default:
		return "Unknown switchtype";
	}
 }

#define sig2str gcom_sig2str

static void gcom_close(int fd)
{
	if (fd > 0)
		close(fd);
}

static void gcom_close_sub(struct gcom_pvt *chan_pvt, int sub_num)
{
	gcom_close(chan_pvt->subs[sub_num].dfd);
	chan_pvt->subs[sub_num].dfd = -1;
}


static void gcom_close_gsm_fd(struct gcom_gsm *gsm, int fd_num)
{
	gcom_close(gsm->gsm.fd);
	gsm->gsm.fd = -1;
}


static int gsm_create_spanmap(int span, int trunkgroup, int logicalspan)
{
	if (gsms[span].mastertrunkgroup) {
		ast_log(LOG_WARNING, "Span %d is already part of trunk group %d, cannot add to trunk group %d\n", span + 1, gsms[span].mastertrunkgroup, trunkgroup);
		return -1;
	}
	gsms[span].mastertrunkgroup = trunkgroup;
	gsms[span].gsmlogicalspan = logicalspan;
	return 0;
}

static int gsm_create_trunkgroup(int trunkgroup, int *channels)
{
	struct dahdi_spaninfo si;
	struct dahdi_params p;
	int fd;
	int span;
	int ospan=0;
	int x = 0;
    int y = 0;
	for (x = 0; x < NUM_SPANS; x++)
	{
	 if (gsms[x].gsm.trunkgroup == trunkgroup)
	 {
	 	ast_log(LOG_WARNING, "Trunk group %d already exists on span %d, Primary d-channel %d\n", trunkgroup, x + 1, gsms[x].dchannel);
		return -1;
	 }
	}
	if (!channels)
	 return -1;
 	memset(&si, 0, sizeof(si));
	memset(&p, 0, sizeof(p));
	fd = open("/dev/dahdi/channel", O_RDWR);
	if (fd < 0)
	{
		ast_log(LOG_WARNING, "Failed to open channel: %s\n", strerror(errno));
		return -1;
	}
	x = channels[y];
	if (ioctl(fd, DAHDI_SPECIFY, &x))
	{
		ast_log(LOG_WARNING, "Failed to specify channel %d: %s\n", channels[y], strerror(errno));
		close(fd);
		return -1;
	}
	if (ioctl(fd, DAHDI_GET_PARAMS, &p))
	{
		ast_log(LOG_WARNING, "Failed to get channel parameters for channel %d: %s\n", channels[y], strerror(errno));
		return -1;
	}
	if (ioctl(fd, DAHDI_SPANSTAT, &si))
	{
		ast_log(LOG_WARNING, "Failed go get span information on channel %d (span %d): %s\n", channels[y], p.spanno, strerror(errno));
		close(fd);
		return -1;
	}
	span = p.spanno - 1;
	if (gsms[span].gsm.trunkgroup)
	{
		ast_log(LOG_WARNING, "Span %d is already provisioned for trunk group %d\n", span + 1, gsms[span].gsm.trunkgroup);
		close(fd);
		return -1;
	}
	if (gsms[span].gsm.pvts)
	{
		ast_log(LOG_WARNING, "Span %d is already provisioned with channels (implicit GSM maybe?)\n", span + 1);
		close(fd);
		return -1;
	}
	if (!y)
	{
		gsms[span].gsm.trunkgroup = trunkgroup;
		ospan = span;
	}
	gsms[ospan].dchannel = *channels;
	close(fd);
	return 0;
 }


 static int prepare_gsm(struct gcom_gsm *gsm)
 {
	ast_log(LOG_DEBUG,"prepare_gsm\n");
	int i=0;
    int res, x;
	struct dahdi_params p;
	struct dahdi_bufferinfo bi;
	struct dahdi_spaninfo si;
	gsm->gsm.calls = &gcom_gsm_callbacks;
	if (!gsm->dchannel)
	{
	  ast_log(LOG_ERROR, "prepare_gsm have no dchannel\n");
	  return -1;
    }
	gsm->gsm.fd = open("/dev/dahdi/channel", O_RDWR);
	x = gsm->dchannel;
	if ((gsm->gsm.fd < 0) || (ioctl(gsm->gsm.fd,DAHDI_SPECIFY,&x) == -1))
    {
 	   ast_log(LOG_ERROR, "Unable to open D-channel %d (%s) fd=%d \n", x, strerror(errno),gsm->gsm.fd);
	   return -1;
	}
	memset(&p, 0, sizeof(p));
	res = ioctl(gsm->gsm.fd, DAHDI_GET_PARAMS, &p);
	if (res)
	{
		gcom_close_gsm_fd(gsm, i);
		ast_log(LOG_ERROR, "Unable to get parameters for D-channel %d (%s)\n", x, strerror(errno));
		return -1;
	}
    memset(&si, 0, sizeof(si));
	res = ioctl(gsm->gsm.fd, DAHDI_SPANSTAT, &si);
	if (res)
	{
		gcom_close_gsm_fd(gsm, i);
		ast_log(LOG_ERROR, "Unable to get span state for D-channel %d (%s)\n", x, strerror(errno));
	}
	if (!si.alarms)
		gsm_event_noalarm(&gsm->gsm);
    else
		gsm_event_alarm(&gsm->gsm);
	memset(&bi, 0, sizeof(bi));
	bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
	bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
	bi.numbufs = 32;
	bi.bufsize = 1024;
	if (ioctl(gsm->gsm.fd, DAHDI_SET_BUFINFO, &bi))
	{
		ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d: %s\n", x, strerror(errno));
		gcom_close_gsm_fd(gsm, i);
		return -1;
	}
	gsm->gsm.dchan_logical_span = gsms[p.spanno - 1].gsmlogicalspan;
    ast_log(LOG_DEBUG, "prepare_gsmg sm.fd%d \n", gsm->gsm.fd);
	return 0;
}

 static int _gcom_get_index(const struct ast_channel *ast, struct gcom_pvt *p, int nullok, const char *fname, unsigned long line)
 {
	int res;
	if (p->subs[SUB_REAL].owner == ast)
		res = 0;
	else if (p->subs[SUB_CALLWAIT].owner == ast)
		res = 1;
	else if (p->subs[SUB_THREEWAY].owner == ast)
		res = 2;
	else
	{
  	 res = -1;
	 if (!nullok)
		ast_log(LOG_WARNING,"Unable to get index for '%s' on channel %d (%s(), line %lu)\n",ast ? ast_channel_name(ast) : "", p->channel, fname, line);
	}
	return res;
 }

/*!
 * \internal
 * \brief Obtain the specified subchannel owner lock if the owner exists.
 *
 * \param pvt Channel private struct.
 * \param sub_idx Subchannel owner to lock.
 *
 * \note Assumes the pvt->lock is already obtained.
 *
 * \note
 * Because deadlock avoidance may have been necessary, you need to confirm
 * the state of things before continuing.
 *
 * \return Nothing
 */
 static void gcom_lock_sub_owner(struct gcom_pvt *pvt, int sub_idx)
 {
	for (;;)
	{
	 if (!pvt->subs[sub_idx].owner)
 		break; /* No subchannel owner pointer */
 	 if (!ast_channel_trylock(pvt->subs[sub_idx].owner))
		break; /* Got subchannel owner lock */
	 DEADLOCK_AVOIDANCE(&pvt->lock); /* We must unlock the private to avoid the possibility of a deadlock */
	}
 }

 static void wakeup_sub(struct gcom_pvt *p, int a)
 {
	gcom_lock_sub_owner(p, a);
	if (p->subs[a].owner) {
		ast_queue_frame(p->subs[a].owner, &ast_null_frame);
		ast_channel_unlock(p->subs[a].owner);
	}
 }

 static void handle_clear_alarms(struct gcom_pvt *p)
 {
	if (report_alarms & REPORT_CHANNEL_ALARMS)
	{
		ast_log(LOG_NOTICE, "Alarm cleared on channel %d\n", p->channel);
		manager_event(EVENT_FLAG_SYSTEM, "AlarmClear", "Channel: %d\r\n", p->channel);
	}
	if (report_alarms & REPORT_SPAN_ALARMS && p->manages_span_alarms)
	{
		ast_log(LOG_NOTICE, "Alarm cleared on span %d\n", p->span);
		manager_event(EVENT_FLAG_SYSTEM, "SpanAlarmClear", "Span: %d\r\n", p->span);
	}
 }

 static void swap_subs(struct gcom_pvt *p, int a, int b)
 {
	int tchan;
	int tinthreeway;
	struct ast_channel *towner;
	ast_debug(1, "Swapping %d and %d\n", a, b);
	tchan = p->subs[a].chan;
	towner = p->subs[a].owner;
	tinthreeway = p->subs[a].inthreeway;
	p->subs[a].chan = p->subs[b].chan;
	p->subs[a].owner = p->subs[b].owner;
	p->subs[a].inthreeway = p->subs[b].inthreeway;
	p->subs[b].chan = tchan;
	p->subs[b].owner = towner;
	p->subs[b].inthreeway = tinthreeway;
	if (p->subs[a].owner)
		ast_channel_set_fd(p->subs[a].owner, 0, p->subs[a].dfd);
	if (p->subs[b].owner)
		ast_channel_set_fd(p->subs[b].owner, 0, p->subs[b].dfd);
	wakeup_sub(p, a);
	wakeup_sub(p, b);
}

static int gcom_open(char *fn)
{
    ast_log(LOG_DEBUG,"gcom_open\n");
	int fd;
	int isnum;
	int chan = 0;
	int bs;
	int x;
	isnum = 1;
	for (x = 0; x < strlen(fn); x++)
	{
		if (!isdigit(fn[x]))
		{
			isnum = 0;
			break;
		}
	}
	if (isnum)
	{
		chan = atoi(fn);
		if (chan < 1)
		{
			ast_log(LOG_WARNING, "Invalid channel number '%s'\n", fn);
			return -1;
		}
		fn = "/dev/dahdi/channel";
	}
	fd = open(fn, O_RDWR | O_NONBLOCK);
	if (fd < 0)
	{
		ast_log(LOG_WARNING, "Unable to open '%s': %s\n", fn, strerror(errno));
		return -1;
	}
	if (chan)
	{
		if (ioctl(fd, DAHDI_SPECIFY, &chan))
		{
			x = errno;
			close(fd);
			errno = x;
			ast_log(LOG_WARNING, "gcom module Unable to specify channel %d: %s\n", chan, strerror(errno));
			return -1;
		}
	}
	bs = READ_SIZE;
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set blocksize '%d': %s\n", bs,  strerror(errno));
		x = errno;
		close(fd);
		errno = x;
		return -1;
	}
	return fd;
 }

 static int gcom_setlinear(int dfd, int linear)
 {
	return ioctl(dfd, DAHDI_SETLINEAR, &linear);
 }


 static int alloc_sub(struct gcom_pvt *p, int x)
 {
	struct dahdi_bufferinfo bi;
	int res;
	if (p->subs[x].dfd >= 0)
	{
		ast_log(LOG_WARNING, "%s subchannel of %d already in use\n", subnames[x], p->channel);
		return -1;
	}
	p->subs[x].dfd = gcom_open("/dev/dahdi/pseudo");
	if (p->subs[x].dfd <= -1)
	{
		ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
		return -1;
	}
	res = ioctl(p->subs[x].dfd, DAHDI_GET_BUFINFO, &bi);
	if (!res)
	{
		bi.txbufpolicy = p->buf_policy;
		bi.rxbufpolicy = p->buf_policy;
		bi.numbufs = p->buf_no;
		res = ioctl(p->subs[x].dfd, DAHDI_SET_BUFINFO, &bi);
		if (res < 0)
			ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d: %s\n", x, strerror(errno));
	}
	else
		ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d: %s\n", x, strerror(errno));
	if (ioctl(p->subs[x].dfd, DAHDI_CHANNO, &p->subs[x].chan) == 1)
	{
		ast_log(LOG_WARNING, "Unable to get channel number for pseudo channel on FD %d: %s\n", p->subs[x].dfd, strerror(errno));
		gcom_close_sub(p, x);
		p->subs[x].dfd = -1;
		return -1;
	}
	ast_debug(1, "Allocated %s subchannel on FD %d channel %d\n", subnames[x], p->subs[x].dfd, p->subs[x].chan);
	return 0;
 }

/*check ok*/
 static int unalloc_sub(struct gcom_pvt *p, int x)
 {
	if (!x) {
		ast_log(LOG_WARNING, "Trying to unalloc the real channel %d?!?\n", p->channel);
		return -1;
	}
	ast_debug(1, "Released sub %d of channel %d\n", x, p->channel);
	gcom_close_sub(p, x);
	p->subs[x].linear = 0;
	p->subs[x].chan = 0;
	p->subs[x].owner = NULL;
	p->subs[x].inthreeway = 0;
	p->polarity = POLARITY_IDLE;
	memset(&p->subs[x].curconf, 0, sizeof(p->subs[x].curconf));
	return 0;
 }

 static int digit_to_dtmfindex(char digit)
 {
	if (isdigit(digit))
		return DAHDI_TONE_DTMF_BASE + (digit - '0');
	else if (digit >= 'A' && digit <= 'D')
		return DAHDI_TONE_DTMF_A + (digit - 'A');
	else if (digit >= 'a' && digit <= 'd')
		return DAHDI_TONE_DTMF_A + (digit - 'a');
	else if (digit == '*')
		return DAHDI_TONE_DTMF_s;
	else if (digit == '#')
		return DAHDI_TONE_DTMF_p;
	else
		return -1;
 }

 static int gcom_digit_begin(struct ast_channel *chan, char digit)
 {
	struct gcom_pvt *pvt;
	int idx;
	int dtmf = -1;
	int res;
	pvt = ast_channel_tech_pvt(chan);
	ast_mutex_lock(&pvt->lock);
	idx = gcom_get_index(chan, pvt, 0);
	if ((idx != SUB_REAL) || !pvt->owner)
	   goto out;
    switch (pvt->sig)
    {
	  case SIG_GSM:
	    res = sig_gsm_digit_begin(pvt->sig_pvt, chan, digit);
	    if (!res)
		  goto out;
	     break;
       default:
          break;
    }
	if ((dtmf = digit_to_dtmfindex(digit)) == -1)
		goto out;
	if (pvt->pulse || ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SENDTONE, &dtmf))
	{
	 struct dahdi_dialoperation zo =
	 {
	 	.op = DAHDI_DIAL_OP_APPEND,
	 };
 	 zo.dialstr[0] = 'T';
	 zo.dialstr[1] = digit;
	 zo.dialstr[2] = '\0';
	 if ((res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_DIAL, &zo)))
		ast_log(LOG_WARNING, "Couldn't dial digit %c: %s\n", digit, strerror(errno));
	 else
		pvt->dialing = 1;
	}
	else
	{
		ast_debug(1, "Started VLDTMF digit '%c'\n", digit);
		pvt->dialing = 1;
		pvt->begindigit = digit;
	}
out:
	ast_mutex_unlock(&pvt->lock);
	return 0;
 }

 static int gcom_digit_end(struct ast_channel *chan, char digit, unsigned int duration)
 {
	struct gcom_pvt *pvt;
	int res = 0;
	int idx;
	int x;
	pvt = ast_channel_tech_pvt(chan);
	ast_mutex_lock(&pvt->lock);
	idx = gcom_get_index(chan, pvt, 0);
	if ((idx != SUB_REAL) || !pvt->owner || pvt->pulse)
		goto out;
	if (pvt->begindigit)
	{
		x = -1;
		ast_debug(1, "Ending VLDTMF digit '%c'\n", digit);
		res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SENDTONE, &x);
		pvt->dialing = 0;
		pvt->begindigit = 0;
	}
out:
	ast_mutex_unlock(&pvt->lock);
	return res;
 }

static const char * const events[] =
{
	"No event",
	"On hook",
	"Ring/Answered",
	"Wink/Flash",
	"Alarm",
	"No more alarm",
	"HDLC Abort",
	"HDLC Overrun",
	"HDLC Bad FCS",
	"Dial Complete",
	"Ringer On",
	"Ringer Off",
	"Hook Transition Complete",
	"Bits Changed",
	"Pulse Start",
	"Timer Expired",
	"Timer Ping",
	"Polarity Reversal",
	"Ring Begin",
};

  static struct
  {
	int alarm;
	char *name;
  } alarms[] =
  {
	{ DAHDI_ALARM_RED, "Red Alarm" },
	{ DAHDI_ALARM_YELLOW, "Yellow Alarm" },
	{ DAHDI_ALARM_BLUE, "Blue Alarm" },
	{ DAHDI_ALARM_RECOVER, "Recovering" },
	{ DAHDI_ALARM_LOOPBACK, "Loopback" },
	{ DAHDI_ALARM_NOTOPEN, "Not Open" },
	{ DAHDI_ALARM_NONE, "None" },
 };

 static char *alarm2str(int alm)
 {
	int x;
	for (x = 0; x < ARRAY_LEN(alarms); x++)
	{
		if (alarms[x].alarm & alm)
			return alarms[x].name;
	}
	return alm ? "Unknown Alarm" : "No Alarm";
 }

 static const char *event2str(int event)
 {
	static char buf[256];
	if ((event < (ARRAY_LEN(events))) && (event > -1))
		return events[event];
	sprintf(buf, "Event %d", event); /* safe */
	return buf;
 }

 static int conf_add(struct gcom_pvt *p, struct gcom_subchannel *c, int idx, int slavechannel)
 {
	/* If the conference already exists, and we're already in it  don't bother doing anything */
	struct dahdi_confinfo zi;
	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;
	if (slavechannel > 0)
	{
		/* If we have only one slave, do a digital mon */
		zi.confmode = DAHDI_CONF_DIGITALMON;
		zi.confno = slavechannel;
	}
	else
	{
	 if (!idx) /* Real-side and pseudo-side both participate in conference */
	   zi.confmode = DAHDI_CONF_REALANDPSEUDO | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER |	DAHDI_CONF_PSEUDO_TALKER | DAHDI_CONF_PSEUDO_LISTENER;
	 else
		zi.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
	 zi.confno = p->confno;
	}
	if ((zi.confno == c->curconf.confno) && (zi.confmode == c->curconf.confmode))
		return 0;
	if (c->dfd < 0)
		return 0;
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi))
	{
		ast_log(LOG_WARNING, "Failed to add %d to conference %d/%d: %s\n", c->dfd, zi.confmode, zi.confno, strerror(errno));
		return -1;
	}
	if (slavechannel < 1)
		p->confno = zi.confno;
	c->curconf = zi;
	ast_debug(1, "Added %d to conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	return 0;
 }

 static int isourconf(struct gcom_pvt *p, struct gcom_subchannel *c)
 {
	/* If they're listening to our channel, they're ours */
	if ((p->channel == c->curconf.confno) && (c->curconf.confmode == DAHDI_CONF_DIGITALMON))
		return 1;
	/* If they're a talker on our (allocated) conference, they're ours */
	if ((p->confno > 0) && (p->confno == c->curconf.confno) && (c->curconf.confmode & DAHDI_CONF_TALKER))
		return 1;
	return 0;
 }

 static int conf_del(struct gcom_pvt *p, struct gcom_subchannel *c, int idx)
 {
	struct dahdi_confinfo zi;
	if (/* Can't delete if there's no dfd */
		(c->dfd < 0) ||
		/* Don't delete from the conference if it's not our conference */
		!isourconf(p, c)
		/* Don't delete if we don't think it's conferenced at all (implied) */
		) return 0;
	memset(&zi, 0, sizeof(zi));
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi))
	{
		ast_log(LOG_WARNING, "Failed to drop %d from conference %d/%d: %s\n", c->dfd, c->curconf.confmode, c->curconf.confno, strerror(errno));
		return -1;
	}
	ast_debug(1, "Removed %d from conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	return 0;
 }

 static int isslavenative(struct gcom_pvt *p, struct gcom_pvt **out)
 {
	int x;
	int useslavenative;
	struct gcom_pvt *slave = NULL;
	/* Start out optimistic */
	useslavenative = 1;
	/* Update conference state in a stateless fashion */
	for (x = 0; x < 3; x++)
	{
		/* Any three-way calling makes slave native mode *definitely* out  of the question */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway)
			useslavenative = 0;
	}
	/* If we don't have any 3-way calls, check to see if we have   precisely one slave */
	if (useslavenative)
	{
		for (x = 0; x < MAX_SLAVES; x++)
		{
			if (p->slaves[x])
			{
				if (slave)
				{
					/* Whoops already have a slave!  No  slave native and stop right away */
					slave = NULL;
					useslavenative = 0;
					break;
				}
				else
				{
					/* We have one slave so far */
					slave = p->slaves[x];
				}
			}
		}
	}
	/* If no slave, slave native definitely out */
	if (!slave)
		useslavenative = 0;
	else if (slave->law != p->law)
	{
		useslavenative = 0;
		slave = NULL;
	}
	if (out)
		*out = slave;
	return useslavenative;
}

 static int reset_conf(struct gcom_pvt *p)
 {
	p->confno = -1;
	memset(&p->subs[SUB_REAL].curconf, 0, sizeof(p->subs[SUB_REAL].curconf));
	if (p->subs[SUB_REAL].dfd > -1)
	{
		struct dahdi_confinfo zi;
		memset(&zi, 0, sizeof(zi));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &zi))
			ast_log(LOG_WARNING, "Failed to reset conferencing on channel %d: %s\n", p->channel, strerror(errno));
	}
	return 0;
 }

 static int update_conf(struct gcom_pvt *p)
 {
	int needconf = 0;
	int x;
	int useslavenative;
	struct gcom_pvt *slave = NULL;
	useslavenative = isslavenative(p, &slave);
	/* Start with the obvious, general stuff */
	for (x = 0; x < 3; x++)
	{
		/* Look for three way calls */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway)
		{
			conf_add(p, &p->subs[x], x, 0);
			needconf++;
		}
		else
			conf_del(p, &p->subs[x], x);
	}
	/* If we have a slave, add him to our conference now. or DAX   if this is slave native */
	for (x = 0; x < MAX_SLAVES; x++)
	{
		if (p->slaves[x])
		{
			if (useslavenative)
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p));
			else
			{
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, 0);
				needconf++;
			}
		}
	}
	/* If we're supposed to be in there, do so now */
	if (p->inconference && !p->subs[SUB_REAL].inthreeway)
	{
		if (useslavenative)
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(slave));
		else
		{
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, 0);
			needconf++;
		}
	}
	/* If we have a master, add ourselves to his conference */
	if (p->master)
	{
		if (isslavenative(p->master, NULL))
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p->master));
		else
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, 0);
	}
	if (!needconf)
	{
		/* Nobody is left (or should be left) in our conference.   Kill it. */
		p->confno = -1;
	}
	ast_debug(1, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);
	return 0;
 }

 static void gcom_enable_ec(struct gcom_pvt *p)
 {
 ast_log(LOG_DEBUG,"gcom_enable_ec");
	int res;
	if (!p)
		return;
	if (p->echocanon)
	{
		ast_debug(1, "Echo cancellation already on\n");
		return;
	}
	if (p->digital)
	{
		ast_debug(1, "Echo cancellation isn't required on digital connection\n");
		return;
	}
	if (p->echocancel.head.tap_length)
	{
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_PARAMS, &p->echocancel);
		if (res)
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d (%s)\n", p->channel, strerror(errno));
		else
		{
			p->echocanon = 1;
			ast_log(LOG_NOTICE, "Enabled echo cancellation on channel %d\n", p->channel);
		}
	}
	else
	ast_log(LOG_NOTICE, "No echo cancellation requested\n");
 }


 static void gcom_train_ec(struct gcom_pvt *p)
 {
	int x;
	int res;
	if (p && p->echocanon && p->echotraining)
	{
		x = p->echotraining;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOTRAIN, &x);
		if (res)
			ast_log(LOG_WARNING, "Unable to request echo training on channel %d: %s\n", p->channel, strerror(errno));
		else
			ast_debug(1, "Engaged echo training on channel %d\n", p->channel);
	} else
		ast_debug(1, "No echo training requested\n");
 }

 static void gcom_disable_ec(struct gcom_pvt *p)
 {
	int res;
	if (p->echocanon)
	{
		struct dahdi_echocanparams ecp = { .tap_length = 0 };
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_PARAMS, &ecp);
		if (res)
			ast_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d: %s\n", p->channel, strerror(errno));
		else
			ast_log(LOG_NOTICE, "Disabled echo cancellation on channel %d\n", p->channel);
	}
	p->echocanon = 0;
 }

/* perform a dynamic range compression transform on the given sample */
 static int drc_sample(int sample, float drc)
 {
	float neg;
	float shallow, steep;
	float max = SHRT_MAX;
	neg = (sample < 0 ? -1 : 1);
	steep = drc*sample;
	shallow = neg*(max-max/drc)+(float)sample/drc;
	if (abs(steep) < abs(shallow))
		sample = steep;
	else
		sample = shallow;
	return sample;
 }


 static void fill_txgain(struct dahdi_gains *g, float gain, float drc, int law)
 {
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);
	switch (law)
	{
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++)
		{
			if (gain || drc)
			{
				k = AST_ALAW(j);
				if (drc)
					k = drc_sample(k, drc);
				k = (float)k*linear_gain;
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = AST_LIN2A(k);
			}
			else
				g->txgain[j] = j;
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++)
		{
		   if (gain || drc)
		   {
				k = AST_MULAW(j);
				if (drc)
					k = drc_sample(k, drc);
				k = (float)k*linear_gain;
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = AST_LIN2MU(k);
		   }
		   else
			g->txgain[j] = j;
		}
		break;
	}
 }

 static void fill_rxgain(struct dahdi_gains *g, float gain, float drc, int law)
 {
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);
	switch (law)
	{
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++)
		{
		  if (gain || drc)
		  {
			k = AST_ALAW(j);
			if (drc)
				k = drc_sample(k, drc);
			k = (float)k*linear_gain;
			if (k > 32767) k = 32767;
			if (k < -32767) k = -32767;
			g->rxgain[j] = AST_LIN2A(k);
		  }
		  else
			g->rxgain[j] = j;
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++)
		{
		  if (gain || drc)
		  {
			k = AST_MULAW(j);
			if (drc)
			  k = drc_sample(k, drc);
			k = (float)k*linear_gain;
			if (k > 32767) k = 32767;
			if (k < -32767) k = -32767;
			g->rxgain[j] = AST_LIN2MU(k);
		  }
		  else
			g->rxgain[j] = j;
		}
		break;
	}
 }

 static int set_actual_txgain(int fd, float gain, float drc, int law)
 {
	struct dahdi_gains g;
	int res;
	memset(&g, 0, sizeof(g));
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res)
	{
		ast_log(LOG_ERROR, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}
	fill_txgain(&g, gain, drc, law);
	return ioctl(fd, DAHDI_SETGAINS, &g);
  }

 static int set_actual_rxgain(int fd, float gain, float drc, int law)
 {
	struct dahdi_gains g;
	int res;
	memset(&g, 0, sizeof(g));
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res)
	{
		ast_log(LOG_ERROR, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}
	fill_rxgain(&g, gain, drc, law);
	return ioctl(fd, DAHDI_SETGAINS, &g);
 }

 static int set_actual_gain(int fd, float rxgain, float txgain, float rxdrc, float txdrc, int law)
 {
	return set_actual_txgain(fd, txgain, txdrc, law) | set_actual_rxgain(fd, rxgain, rxdrc, law);
 }



 static int restore_gains(struct gcom_pvt *p)
 {
	int res;
	res = set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
	if (res)
	{
		ast_log(LOG_ERROR, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}
	return 0;
 }

 static inline int gcom_set_hook(int fd, int hs)
 {
	int x, res;
	x = hs;
	res = ioctl(fd, DAHDI_HOOK, &x);
	if (res < 0)
	{
	  if (errno == EINPROGRESS)
		return 0;
	  ast_log(LOG_ERROR, "DAHDI hook failed returned %d (trying %d): %s\n", res, hs, strerror(errno));
		/* will expectedly fail if phone is off hook during operation, such as during a restart */
	}
	return res;
 }

 static inline int gcom_confmute(struct gcom_pvt *p, int muted)
 {
	ast_log(LOG_DEBUG,"gcom_confmute\n");
	int x, res;
	x = muted;
	int y = 1;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &y);
	if (res)
		ast_log(LOG_WARNING, "Unable to set audio mode on %d: %s\n",p->channel, strerror(errno));
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_CONFMUTE, &x);
	if (res < 0)
		ast_log(LOG_WARNING, "DAHDI confmute(%d) failed on channel %d: %s\n", muted, p->channel, strerror(errno));
	return res;
  }

  static int save_conference(struct gcom_pvt *p)
  {
	struct dahdi_confinfo c;
	int res;
	if (p->saveconf.confmode)
	{
		ast_log(LOG_WARNING, "Can't save conference -- already in use\n");
		return -1;
	}
	p->saveconf.chan = 0;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GETCONF, &p->saveconf);
	if (res)
	{
		ast_log(LOG_WARNING, "Unable to get conference info: %s\n", strerror(errno));
		p->saveconf.confmode = 0;
		return -1;
	}
	memset(&c, 0, sizeof(c));
	c.confmode = DAHDI_CONF_NORMAL;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &c);
	if (res)
	{
		ast_log(LOG_WARNING, "Unable to set conference info: %s\n", strerror(errno));
		return -1;
	}
	ast_log(LOG_DEBUG, "Disabled conferencing\n");
	return 0;
 }

 static int restore_conference(struct gcom_pvt *p)
 {
	int res;
	if (p->saveconf.confmode)
	{
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &p->saveconf);
		p->saveconf.confmode = 0;
		if (res)
		{
			ast_log(LOG_WARNING, "Unable to restore conference info: %s\n", strerror(errno));
			return -1;
		}
		ast_log(LOG_DEBUG,"Restored conferencing\n");
	}
	return 0;
 }

 static int send_cwcidspill(struct gcom_pvt *p)
 {
	p->callwaitcas = 0;
	p->cidcwexpire = 0;
	p->cid_suppress_expire = 0;
	if (!(p->cidspill = ast_malloc(MAX_CALLERID_SIZE)))
		return -1;
	p->cidlen = ast_callerid_callwaiting_generate(p->cidspill, p->callwait_name, p->callwait_num, AST_LAW(p));
	/* Make sure we account for the end */
	p->cidlen += READ_SIZE * 4;
	p->cidpos = 0;
	send_callerid(p);
	ast_log(LOG_DEBUG, "CPE supports Call Waiting Caller*ID.  Sending '%s/%s'\n", p->callwait_name, p->callwait_num);
	return 0;
 }


 static int send_callerid(struct gcom_pvt *p)
  {
	/* Assumes spill in p->cidspill, p->cidlen in length and we're p->cidpos into it */
	int res;
	/* Take out of linear mode if necessary */
	if (p->subs[SUB_REAL].linear)
	{
		p->subs[SUB_REAL].linear = 0;
		gcom_setlinear(p->subs[SUB_REAL].dfd, 0);
	}
	while (p->cidpos < p->cidlen)
	{
		res = write(p->subs[SUB_REAL].dfd, p->cidspill + p->cidpos, p->cidlen - p->cidpos);
		ast_log(LOG_DEBUG, "writing callerid at pos %d of %d, res = %d\n", p->cidpos, p->cidlen, res);
		if (res < 0)
		{
			if (errno == EAGAIN)
				return 0;
			else
			{
				ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
				return -1;
			}
		}
		if (!res)
			return 0;
		p->cidpos += res;
	}
	p->cid_suppress_expire = CALLWAITING_SUPPRESS_SAMPLES;
	ast_free(p->cidspill);
	p->cidspill = NULL;
	if (p->callwaitcas)
	{
		/* Wait for CID/CW to expire */
		p->cidcwexpire = CIDCW_EXPIRE_SAMPLES;
		p->cid_suppress_expire = p->cidcwexpire;
	} else
		restore_conference(p);
	return 0;
 }

static int gcom_callwait(struct ast_channel *ast)
{
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	p->callwaitingrepeat = CALLWAITING_REPEAT_SAMPLES;
	if (p->cidspill)
	{
		ast_log(LOG_DEBUG, "Spill already exists?!?\n");
		ast_free(p->cidspill);
	}
	/*
	 * SAS: Subscriber Alert Signal, 440Hz for 300ms
	 * CAS: CPE Alert Signal, 2130Hz * 2750Hz sine waves
	 */
	if (!(p->cidspill = ast_malloc(2400 /* SAS */ + 680 /* CAS */ + READ_SIZE * 4)))
		return -1;
	save_conference(p);
	/* Silence */
	memset(p->cidspill, 0x7f, 2400 + 600 + READ_SIZE * 4);
	if (!p->callwaitrings && p->callwaitingcallerid)
	{
		ast_gen_cas(p->cidspill, 1, 2400 + 680, AST_LAW(p));
		p->callwaitcas = 1;
		p->cidlen = 2400 + 680 + READ_SIZE * 4;
	}
	else
	{
		ast_gen_cas(p->cidspill, 1, 2400, AST_LAW(p));
		p->callwaitcas = 0;
		p->cidlen = 2400 + READ_SIZE * 4;
	}
	p->cidpos = 0;
	send_callerid(p);
	return 0;
}


static int gcom_gsm_lib_handles(int signalling)
{
     int res=0;
     switch (signalling)
     {
	    case SIG_GSM:
                res =1;
	        break;
           default:
                res = 0;
               break;
     }
     return res;
}


//gcom_call--sig_gsm_call--libgsm--insert queue--write
static int gcom_call(struct ast_channel *ast, char *rdest, int timeout)
{
	ast_log(LOG_DEBUG,"gcom_call\n");
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	int x, res, mysig;
	char dest[256] = {0}; /* must be same length as p->dialdest */
	ast_mutex_lock(&p->lock);
	ast_copy_string(dest, rdest, sizeof(dest));
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));
    ast_log(LOG_DEBUG,"gcom module take gcom_call,rdest=%s timeout=%d\n",p->dialdest,timeout);
	if ((ast_channel_state(ast) == AST_STATE_BUSY))
	{
		p->subs[SUB_REAL].needbusy = 1;
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED))
	{
		ast_log(LOG_WARNING, "gcom_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	p->waitingfordt.tv_sec = 0;
	p->dialednone = 0;
	if ((p->radio || (p->oprmode < 0)))  /* if a radio channel, up immediately */
	{
		/* Special pseudo -- automatically up */
		ast_setstate(ast, AST_STATE_UP);
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	x = DAHDI_FLUSH_READ | DAHDI_FLUSH_WRITE;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
	if (res)
		ast_log(LOG_WARNING, "Unable to flush input on channel %d: %s\n", p->channel, strerror(errno));
	p->outgoing = 1;
	if (IS_DIGITAL(ast_channel_transfercapability(ast)))
		set_actual_gain(p->subs[SUB_REAL].dfd, 0, 0, p->rxdrc, p->txdrc, p->law);
	else
		set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
    if(gcom_gsm_lib_handles(p->sig))
    {
     ast_log(LOG_DEBUG,"gcom module take sig_gsm_call,rdest=%s\n",rdest);
     res = sig_gsm_call(p->sig_pvt, ast, rdest);
     ast_mutex_unlock(&p->lock);
     return res;
    }
    ast_log(LOG_DEBUG,"gcom module take gcom_call and ast_setstate,rdest=%s\n",rdest);
	mysig = p->outsigmod > -1 ? p->outsigmod : p->sig;
	switch (mysig)
	{
	 case 0:
		/* Special pseudo -- automatically up*/
		ast_setstate(ast, AST_STATE_UP);
		break;
	default:
		ast_log(LOG_DEBUG,"not yet implemented\n");
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	ast_mutex_unlock(&p->lock);
	return 0;
 }

/*!
 * \internal
 * \brief Insert the given chan_gcom interface structure into the interface list.
 * \since 1.8
 *
 * \param pvt chan_dahdi private interface structure to insert.
 *
 * \details
 * The interface list is a doubly linked list sorted by the chan_gcom channel number.
 * Any duplicates are inserted after the existing entries.
 *
 * \note The new interface must not already be in the list.
 *
 * \return Nothing
 */
 static void gcom_iflist_insert(struct gcom_pvt *pvt)
 {
	struct gcom_pvt *cur = NULL;
	pvt->which_iflist = GCOM_IFLIST_MAIN;
	/* Find place in middle of list for the new interface. */
	for (cur = iflist; cur; cur = cur->next)
	{
		if (pvt->channel < cur->channel)
		{
			/* New interface goes before the current interface. */
			pvt->prev = cur->prev;
			pvt->next = cur;
			if (cur->prev)
				cur->prev->next = pvt; /* Insert into the middle of the list. */
			else
				iflist = pvt; /* Insert at head of list. */
			cur->prev = pvt;
			return;
		}
	}
	/* New interface goes onto the end of the list */
	pvt->prev = ifend;
	pvt->next = NULL;
	if (ifend)
		ifend->next = pvt;
	ifend = pvt;
	if (!iflist)
		iflist = pvt; /* List was empty */
 }

/*!
 * \internal
 * \brief Extract the given chan_dahdi interface structure from the interface list.
 * \since 1.8
 *
 * \param pvt chan_dahdi private interface structure to extract.
 *
 * \note
 * The given interface structure can be either in the interface list or a stand alone
 * structure that has not been put in the list if the next and prev pointers are NULL.
 *
 * \return Nothing
 */
 static void gcom_iflist_extract(struct gcom_pvt *pvt)
 {
	/* Extract from the forward chain. */
	if (pvt->prev)
		pvt->prev->next = pvt->next;
	else if (iflist == pvt)
		iflist = pvt->next; /* Node is at the head of the list. */
	/* Extract from the reverse chain. */
	if (pvt->next)
		pvt->next->prev = pvt->prev;
	else if (ifend == pvt)
		ifend = pvt->prev; /* Node is at the end of the list. */
	/* Node is no longer in the list. */
	pvt->which_iflist = GCOM_IFLIST_NONE;
	pvt->prev = NULL;
	pvt->next = NULL;
 }

 static struct gcom_pvt *find_next_iface_in_span(struct gcom_pvt *cur)
 {
	if (cur->next && cur->next->span == cur->span)
		return cur->next;
	else if (cur->prev && cur->prev->span == cur->span)
		return cur->prev;
	return NULL;
 }

 static void destroy_gcom_pvt(struct gcom_pvt *pvt)
 {
	struct gcom_pvt *p = pvt;
	if (p->manages_span_alarms)
	{
		struct gcom_pvt *next = find_next_iface_in_span(p);
		if (next)
			next->manages_span_alarms = 1;
	}
	/* Remove channel from the list */
	switch (pvt->which_iflist)
	{
	 case GCOM_IFLIST_NONE:
		break;
	 case GCOM_IFLIST_MAIN:
		gcom_iflist_extract(p);
		break;
	}
	ast_free(p->cidspill);
	if (p->vars)
		ast_variables_destroy(p->vars);
	if (p->cc_params)
		ast_cc_config_params_destroy(p->cc_params);
	ast_mutex_destroy(&p->lock);
	gcom_close_sub(p, SUB_REAL);
	if (p->owner)
		ast_channel_tech_pvt_set(p->owner, NULL);
	ast_free(p);
 }

 static void destroy_channel(struct gcom_pvt *cur, int now)
 {
	int i;
	if (!now)
	{
		/* Do not destroy the channel now if it is owned by someone. */
		if (cur->owner)
			return;
		for (i = 0; i < 3; i++)
		{
			if (cur->subs[i].owner)
				return;
		}
	}
	destroy_gcom_pvt(cur);
 }

 static void destroy_all_channels(void)
 {
	int chan = 0;
	struct gcom_pvt *p = NULL;
	while (num_restart_pending)
		usleep(1);
	ast_mutex_lock(&iflock);
	/* Destroy all the interfaces and free their memory */
	while (iflist)
	{
		p = iflist;
		chan = p->channel;
		/* Free associated memory */
		destroy_gcom_pvt(p);
		ast_log(LOG_DEBUG, "Unregistered channel %d\n", chan);
	}
	ifcount = 0;
	ast_mutex_unlock(&iflock);

 }

 static int revert_fax_buffers(struct gcom_pvt *p, struct ast_channel *ast)
 {
	if (p->bufferoverrideinuse)
	{
		/* faxbuffers are in use, revert them */
		struct dahdi_bufferinfo bi =
		{
			.txbufpolicy = p->buf_policy,
			.rxbufpolicy = p->buf_policy,
			.bufsize = p->bufsize,
			.numbufs = p->buf_no
		};
		int bpres;
		if ((bpres = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi)) < 0)
			ast_log(LOG_WARNING, "Channel '%s' unable to revert buffer policy: %s\n", ast_channel_name(ast), strerror(errno));
		p->bufferoverrideinuse = 0;
		return bpres;
	}
	return -1;
 }

//check may be next check
 static int gcom_hangup(struct ast_channel *ast)
 {
	int res = 0;
	int idx,x;
	int law = 0;
	/*static int restore_gains(struct gcom_pvt *p);*/
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	ast_log(LOG_DEBUG, "gcom_hangup(%s)\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast))
	{
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_mutex_lock(&p->lock);
	p->exten[0] = '\0';
	p->cid_num[0] = '\0';
	p->cid_name[0] = '\0';
	p->cid_subaddr[0] = '\0';
	idx = gcom_get_index(ast, p, 1);
	gcom_confmute(p, 0);
	p->muting = 0;
	restore_gains(p);
	if (p->origcid_num)
	{
		ast_copy_string(p->cid_num, p->origcid_num, sizeof(p->cid_num));
		ast_free(p->origcid_num);
		p->origcid_num = NULL;
	}
	if (p->origcid_name)
	{
		ast_copy_string(p->cid_name, p->origcid_name, sizeof(p->cid_name));
		ast_free(p->origcid_name);
		p->origcid_name = NULL;
	}
	ast_log(LOG_DEBUG, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",p->channel, idx, p->subs[SUB_REAL].dfd, p->subs[SUB_CALLWAIT].dfd, p->subs[SUB_THREEWAY].dfd);
	p->ignoredtmf = 0;
	if (idx > -1)
	{
		/* Real channel, do some fixup */
		p->subs[idx].owner = NULL;
		p->subs[idx].needanswer = 0;
		p->subs[idx].needflash = 0;
		p->subs[idx].needringing = 0;
		p->subs[idx].needbusy = 0;
		p->subs[idx].needcongestion = 0;
		p->subs[idx].linear = 0;
		p->polarity = POLARITY_IDLE;
		gcom_setlinear(p->subs[idx].dfd, 0);
		if (idx == SUB_REAL)
		{
			if ((p->subs[SUB_CALLWAIT].dfd > -1) && (p->subs[SUB_THREEWAY].dfd > -1))
			{
				ast_log(LOG_DEBUG, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[SUB_CALLWAIT].inthreeway)
				{
					/* We had flipped over to answer a callwait and now it's gone */
					ast_log(LOG_DEBUG, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					unalloc_sub(p, SUB_CALLWAIT);
					p->owner = NULL;
				}
				else
				{
					/* The three way hung up, but we still have a call wait */
					ast_log(LOG_DEBUG, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					swap_subs(p, SUB_THREEWAY, SUB_REAL);
					unalloc_sub(p, SUB_THREEWAY);
					if (p->subs[SUB_REAL].inthreeway)
					{
						/* This was part of a three way call.  Immediately make way for another call */
						ast_log(LOG_DEBUG, "Call was complete, setting owner to former third call\n");
						p->owner = p->subs[SUB_REAL].owner;
					}
					else
					{
						/* This call hasn't been completed yet...  Set owner to NULL */
						ast_log(LOG_DEBUG, "Call was incomplete, setting owner to NULL\n");
						p->owner = NULL;
					}
					p->subs[SUB_REAL].inthreeway = 0;
				}
			}
			else if (p->subs[SUB_CALLWAIT].dfd > -1)
			{
				/* Move to the call-wait and switch back to them. */
				swap_subs(p, SUB_CALLWAIT, SUB_REAL);
				unalloc_sub(p, SUB_CALLWAIT);
				p->owner = p->subs[SUB_REAL].owner;
				if (ast_channel_state(p->owner) != AST_STATE_UP)
					p->subs[SUB_REAL].needanswer = 1;
				if (ast_channel_is_bridged(p->subs[SUB_REAL].owner))
					ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
			}
			else if (p->subs[SUB_THREEWAY].dfd > -1)
			{
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				unalloc_sub(p, SUB_THREEWAY);
				if (p->subs[SUB_REAL].inthreeway)
				{
					/* This was part of a three way call.  Immediately make way for	   another call */
					ast_log(LOG_DEBUG, "Call was complete, setting owner to former third call\n");
					p->owner = p->subs[SUB_REAL].owner;
				}
				else
				{
					/* This call hasn't been completed yet...  Set owner to NULL */
					ast_debug(1, "Call was incomplete, setting owner to NULL\n");
					p->owner = NULL;
				}
				p->subs[SUB_REAL].inthreeway = 0;
			}
		}
		else if (idx == SUB_CALLWAIT)
		{
			/* Ditch the holding callwait call, and immediately make it availabe */
			if (p->subs[SUB_CALLWAIT].inthreeway)
			{
				/* This is actually part of a three way, placed on hold.  Place the third part   on music on hold now */
				if (p->subs[SUB_THREEWAY].owner && ast_channel_is_bridged(p->subs[SUB_THREEWAY].owner))
					ast_queue_control_data(p->subs[SUB_THREEWAY].owner, AST_CONTROL_HOLD,S_OR(p->mohsuggest, NULL),!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				p->subs[SUB_THREEWAY].inthreeway = 0;
				/* Make it the call wait now */
				swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
				unalloc_sub(p, SUB_THREEWAY);
			}
			else
				unalloc_sub(p, SUB_CALLWAIT);
		}
		else if (idx == SUB_THREEWAY)
		{
			if (p->subs[SUB_CALLWAIT].inthreeway)
			{
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				if (p->subs[SUB_CALLWAIT].owner && ast_channel_is_bridged(p->subs[SUB_CALLWAIT].owner))
					ast_queue_control_data(p->subs[SUB_CALLWAIT].owner, AST_CONTROL_HOLD,S_OR(p->mohsuggest, NULL),!ast_strlen_zero(p->mohsuggest) ? strlen(p->mohsuggest) + 1 : 0);
				p->subs[SUB_CALLWAIT].inthreeway = 0;
			}
			p->subs[SUB_REAL].inthreeway = 0;
			/* If this was part of a three way call index, let us make   another three way call */
			unalloc_sub(p, SUB_THREEWAY);
		}
		else
		{
			/* This wasn't any sort of call, but how are we an index? */
			ast_log(LOG_DEBUG, "Index found but not any type of call?\n");
		}
	}
	if (!p->subs[SUB_REAL].owner && !p->subs[SUB_CALLWAIT].owner && !p->subs[SUB_THREEWAY].owner)
	{
		p->owner = NULL;
		p->ringt = 0;
		p->distinctivering = 0;
		p->confirmanswer = 0;
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;
		if (p->dsp)
		{
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}
		revert_fax_buffers(p, ast);
		p->law = p->law_default;
		law = p->law_default;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (res < 0)
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n", p->channel, strerror(errno));
		/* Perform low level hangup if no owner left */
        //may be add gsm_grab
        if (gcom_gsm_lib_handles(p->sig))
        {
              x = 1;
              ast_channel_setoption(ast, AST_OPTION_AUDIO_MODE, &x, sizeof(char), 0);
              gcom_confmute(p, 0);
              p->muting = 0;
              restore_gains(p);
              if (p->dsp)
              {
                 ast_dsp_free(p->dsp);
                 p->dsp = NULL;
              }
              p->ignoredtmf = 0;
               /* Real channel, do some fixup */
              p->subs[SUB_REAL].owner = NULL;
              p->subs[SUB_REAL].needbusy = 0;
              gcom_setlinear(p->subs[SUB_REAL].dfd, 0);
              p->owner = NULL;
              p->cid_tag[0] = '\0';
              p->outgoing = 0;
              revert_fax_buffers(p, ast);
              sig_gsm_hangup(p->sig_pvt, ast);
              gcom_disable_ec(p);
              update_conf(p);
              reset_conf(p);
              goto hangup_out;
        }
		tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
		if (p->sig)
			gcom_disable_ec(p);
		x = 0;
		ast_channel_setoption(ast,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		ast_channel_setoption(ast,AST_OPTION_TDD,&x,sizeof(char),0);
		p->didtdd = 0;
		p->callwaitcas = 0;
		p->callwaiting = p->permcallwaiting;
		p->hidecallerid = p->permhidecallerid;
		p->waitingfordt.tv_sec = 0;
		p->dialing = 0;
		p->rdnis[0] = '\0';
		update_conf(p);
		reset_conf(p);
		/* Restore data mode */
		switch (p->sig)
		{
		 case SIG_GSM:
			x = 0;
			ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
			break;
		 default:
			break;
		 }
	}
	p->callwaitingrepeat = 0;
	p->cidcwexpire = 0;
	p->cid_suppress_expire = 0;
	p->oprmode = 0;
hangup_out:
	ast_channel_tech_pvt_set(ast,NULL);
	ast_free(p->cidspill);
	p->cidspill = NULL;
	ast_mutex_unlock(&p->lock);
	ast_log(LOG_DEBUG, "Hungup '%s'\n", ast_channel_name(ast));
    ast_module_unref(ast_module_info->self);
	ast_mutex_lock(&iflock);
	if (p->restartpending)
		num_restart_pending--;
	if (p->destroy)
		destroy_channel(p, 0);
	ast_mutex_unlock(&iflock);
	return 0;
}

 static int gcom_answer(struct ast_channel *ast)
 {
	ast_log(LOG_DEBUG,"gcom_answer\n");
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	int res = 0;
	int idx;
	ast_setstate(ast, AST_STATE_UP);/*! \todo XXX this is redundantly set by the analog and GSM submodules! */
	ast_mutex_lock(&p->lock);
	idx = gcom_get_index(ast, p, 0);
	if (idx < 0)
		idx = SUB_REAL;
	/* nothing to do if a radio channel */
	if ((p->radio || (p->oprmode < 0)))
	{
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if (gcom_gsm_lib_handles(p->sig))
	{
		res = sig_gsm_answer(p->sig_pvt, ast);
		ast_mutex_unlock(&p->lock);
		return res;
	}
	switch (p->sig)
	{
	 case 0:
		ast_mutex_unlock(&p->lock);
		return 0;
	 default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		res = -1;
		break;
	}
	ast_mutex_unlock(&p->lock);
	return res;
 }

 static void disable_dtmf_detect(struct gcom_pvt *p)
 {
	int val = 0;
	p->ignoredtmf = 1;
	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);
	if (!p->hardwaredtmf && p->dsp)
	{
		p->dsp_features &= ~DSP_FEATURE_DIGIT_DETECT;
		ast_dsp_set_features(p->dsp, p->dsp_features);
	}
 }

 static void enable_dtmf_detect(struct gcom_pvt *p)
 {
	int val = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;
	if (p->channel == CHAN_PSEUDO)
		return;
	p->ignoredtmf = 0;
	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);
	if (!p->hardwaredtmf && p->dsp)
	{
		p->dsp_features |= DSP_FEATURE_DIGIT_DETECT;
		ast_dsp_set_features(p->dsp, p->dsp_features);
	}
 }

 static int gcom_queryoption(struct ast_channel *chan, int option, void *data, int *datalen)
 {
	char *cp;
	struct gcom_pvt *p = ast_channel_tech_pvt(chan);
	/* all supported options require data */
	if (!data || (*datalen < 1))
	{
		errno = EINVAL;
		return -1;
	}
	switch (option)
	{
	 case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		*cp = p->ignoredtmf ? 0 : 1;
		ast_log(LOG_DEBUG, "Reporting digit detection %sabled on %s\n", *cp ? "en" : "dis", ast_channel_name(chan));
		break;
	 case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		*cp = (p->dsp_features & DSP_FEATURE_FAX_DETECT) ? 0 : 1;
		ast_log(LOG_DEBUG, "Reporting fax tone detection %sabled on %s\n", *cp ? "en" : "dis", ast_channel_name(chan));
		break;
	 case AST_OPTION_CC_AGENT_TYPE:
		return -1;
	 default:
		return -1;
	}
	errno = 0;
	return 0;
 }

 static int gcom_setoption(struct ast_channel *chan, int option, void *data, int datalen)
 {
	char *cp;
	signed char *scp;
	int x;
	int idx;
	struct gcom_pvt *p = ast_channel_tech_pvt(chan), *pp;
	struct oprmode *oprmode;
	/* all supported options require data */
	if (!data || (datalen < 1))
	{
		errno = EINVAL;
		return -1;
	}
	switch (option)
	{
	 case AST_OPTION_TXGAIN:
		scp = (signed char *) data;
		idx = gcom_get_index(chan, p, 0);
		if (idx < 0)
		{
			ast_log(LOG_WARNING, "No index in TXGAIN?\n");
			return -1;
		}
		ast_log(LOG_DEBUG, "Setting actual tx gain on %s to %f\n", ast_channel_name(chan), p->txgain + (float) *scp);
		return set_actual_txgain(p->subs[idx].dfd, p->txgain + (float) *scp, p->txdrc, p->law);
	 case AST_OPTION_RXGAIN:
		scp = (signed char *) data;
		idx = gcom_get_index(chan, p, 0);
		if (idx < 0)
		{
			ast_log(LOG_WARNING, "No index in RXGAIN?\n");
			return -1;
		}
		ast_log(LOG_DEBUG, "Setting actual rx gain on %s to %f\n", ast_channel_name(chan), p->rxgain + (float) *scp);
		return set_actual_rxgain(p->subs[idx].dfd, p->rxgain + (float) *scp, p->rxdrc, p->law);
	case AST_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		cp = (char *) data;
		switch (*cp)
		{
		 case 1:
		    ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",ast_channel_name(chan));
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		 case 2:
			 ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",ast_channel_name(chan));
			 ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
			 break;
 		 default:
 			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",ast_channel_name(chan));
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
			break;
		}
		break;
	case AST_OPTION_TDD:
		/* turn on or off TDD */
		cp = (char *) data;
		p->mate = 0;
		if (!*cp)  /* turn it off */
		{
			ast_log(LOG_DEBUG, "Set option TDD MODE, value: OFF(0) on %s\n",ast_channel_name(chan));
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			break;
		}
		ast_log(LOG_DEBUG, "Set option TDD MODE, value: %s(%d) on %s\n",(*cp == 2) ? "MATE" : "ON", (int) *cp, ast_channel_name(chan));
		gcom_disable_ec(p);
		/* otherwise, turn it on */
		if (!p->didtdd) /* if havent done it yet */
		{
			unsigned char mybuf[41000];/*! \todo XXX This is an abuse of the stack!! */
			unsigned char *buf;
			int size, res, fd, len;
			struct pollfd fds[1];
			buf = mybuf;
			memset(buf, 0x7f, sizeof(mybuf)); /* set to silence */
			ast_tdd_gen_ecdisa(buf + 16000, 16000);  /* put in tone */
			len = 40000;
			idx = gcom_get_index(chan, p, 0);
			if (idx < 0)
			{
				ast_log(LOG_WARNING, "No index in TDD?\n");
				return -1;
			}
			fd = p->subs[idx].dfd;
			while (len)
			{
				if (ast_check_hangup(chan))
					return -1;
				size = len;
				if (size > READ_SIZE)
					size = READ_SIZE;
				fds[0].fd = fd;
				fds[0].events = POLLPRI | POLLOUT;
				fds[0].revents = 0;
				res = poll(fds, 1, -1);
				if (!res)
				{
					ast_log(LOG_DEBUG,  "poll (for write) ret. 0 on channel %d\n", p->channel);
					continue;
				}
				/* if got exception */
				if (fds[0].revents & POLLPRI)
					return -1;
				if (!(fds[0].revents & POLLOUT))
				{
					ast_log(LOG_DEBUG,  "write fd not ready on channel %d\n", p->channel);
					continue;
				}
				res = write(fd, buf, size);
				if (res != size)
				{
					if (res == -1) return -1;
					ast_log(LOG_DEBUG,  "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
					break;
				}
				len -= size;
				buf += size;
			}
			p->didtdd = 1; /* set to have done it now */
		}
		if (*cp == 2)  /* Mate mode */
		{
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			p->mate = 1;
			break;
		}
		if (!p->tdd)/* if we don't have one yet */
			p->tdd = tdd_new(); /* allocate one */
		break;
	case AST_OPTION_RELAXDTMF:  /* Relax DTMF decoding (or not) */
		if (!p->dsp)
			break;
		cp = (char *) data;
		ast_log(LOG_DEBUG, "Set option RELAX DTMF, value: %s(%d) on %s\n",*cp ? "ON" : "OFF", (int) *cp, ast_channel_name(chan));
		ast_dsp_set_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
		break;
	case AST_OPTION_AUDIO_MODE:  /* Set AUDIO mode (or not) */
		cp = (char *) data;
		if (!*cp)
		{
			ast_log(LOG_DEBUG, "Set option AUDIO MODE, value: OFF(0) on %s\n", ast_channel_name(chan));
			x = 0;
			gcom_disable_ec(p);
		}
		else
		{
			ast_log(LOG_DEBUG, "Set option AUDIO MODE, value: ON(1) on %s\n", ast_channel_name(chan));
			x = 1;
		}
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x) == -1)
			ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", p->channel, x, strerror(errno));
		break;
	case AST_OPTION_OPRMODE:  /* Operator services mode */
		oprmode = (struct oprmode *) data;
		/* We don't support operator mode across technologies */
		if (strcasecmp(ast_channel_tech(chan)->type, ast_channel_tech(oprmode->peer)->type))
		{
			ast_log(LOG_NOTICE, "Operator mode not supported on %s to %s calls.\n",
			ast_channel_tech(chan)->type, ast_channel_tech(oprmode->peer)->type);
			errno = EINVAL;
			return -1;
		}
		pp = ast_channel_tech_pvt(oprmode->peer);
		p->oprmode = pp->oprmode = 0;
		/* setup peers */
		p->oprpeer = pp;
		pp->oprpeer = p;
		/* setup modes, if any */
		if (oprmode->mode)
		{
			pp->oprmode = oprmode->mode;
			p->oprmode = -oprmode->mode;
		}
		ast_log(LOG_DEBUG, "Set Operator Services mode, value: %d on %s/%s\n",oprmode->mode, ast_channel_name(chan),ast_channel_name(oprmode->peer));
		break;
	case AST_OPTION_ECHOCAN:
		cp = (char *) data;
		if (*cp)
			gcom_enable_ec(p);
		else
			gcom_disable_ec(p);
		break;
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		ast_log(LOG_DEBUG, "%sabling digit detection on %s\n", *cp ? "En" : "Dis",  ast_channel_name(chan));
		if (*cp)
			enable_dtmf_detect(p);
		else
			disable_dtmf_detect(p);
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		if (p->dsp)
		{
		 ast_log(LOG_DEBUG, "%sabling fax tone detection on %s\n", *cp ? "En" : "Dis",  ast_channel_name(chan));
		  if (*cp)
			p->dsp_features |= DSP_FEATURE_FAX_DETECT;
		  else
			p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
		 ast_dsp_set_features(p->dsp, p->dsp_features);
		}
		break;
	 default:
		return -1;
	}
	errno = 0;
	return 0;
 }

 static int gcom_func_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
 {

	 struct gcom_pvt *p =  ast_channel_tech_pvt(chan);
	int res = 0;
	if (!strcasecmp(data, "rxgain"))
	{
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%f", p->rxgain);
		ast_mutex_unlock(&p->lock);
	}
	else if (!strcasecmp(data, "txgain"))
	{
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%f", p->txgain);
		ast_mutex_unlock(&p->lock);
	}
	else if (!strcasecmp(data, "dahdi_channel"))
	{
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%d", p->channel);
		ast_mutex_unlock(&p->lock);
	}
	else if (!strcasecmp(data, "dahdi_span"))
	{
		ast_mutex_lock(&p->lock);
		snprintf(buf, len, "%d", p->span);
		ast_mutex_unlock(&p->lock);
	}
	else if (!strcasecmp(data, "dahdi_type"))
	{
		ast_mutex_lock(&p->lock);
		switch (p->sig)
		{
		 case 0:
			ast_copy_string(buf, "pseudo", len);
			break;
		 default:
			/* The only thing left is analog ports. */
			ast_copy_string(buf, "analog", len);
			break;
		}
		ast_mutex_unlock(&p->lock);
	}
	else
	{
		*buf = '\0';
		res = -1;
	}
	return res;
 }


 static int parse_buffers_policy(const char *parse, int *num_buffers, int *policy)
 {
	int res;
	char policy_str[21] = "";
	if ((res = sscanf(parse, "%30d,%20s", num_buffers, policy_str)) != 2)
	{
		ast_log(LOG_WARNING, "Parsing buffer string '%s' failed.\n", parse);
		return 1;
	}
	if (*num_buffers < 0)
	{
		ast_log(LOG_WARNING, "Invalid buffer count given '%d'.\n", *num_buffers);
		return -1;
	}
	if (!strcasecmp(policy_str, "full"))
		*policy = DAHDI_POLICY_WHEN_FULL;
	else if (!strcasecmp(policy_str, "immediate"))
		*policy = DAHDI_POLICY_IMMEDIATE;
	else
	{
		ast_log(LOG_WARNING, "Invalid policy name given '%s'.\n", policy_str);
		return -1;
	}
	return 0;
 }

 static int gcom_func_write(struct ast_channel *chan, const char *function, char *data, const char *value)
 {
  ast_log(LOG_DEBUG,"gcom_func_write\n");
  struct gcom_pvt *p =  ast_channel_tech_pvt(chan);
  int res = 0;
  if (!strcasecmp(data, "buffers"))
  {
	int num_bufs, policy;
	if (!(parse_buffers_policy(value, &num_bufs, &policy)))
	{
			struct dahdi_bufferinfo bi =
			{
				.txbufpolicy = policy,
				.rxbufpolicy = policy,
				.bufsize = p->bufsize,
				.numbufs = num_bufs,
			};
			int bpres;
			if ((bpres = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi)) < 0)
				ast_log(LOG_WARNING, "Channel '%d' unable to override buffer policy: %s\n", p->channel, strerror(errno));
			else
				p->bufferoverrideinuse = 1;
	}
	else
 	 res = -1;
  }
  else if (!strcasecmp(data, "echocan_mode"))
  {
		if (!strcasecmp(value, "on"))
		{
			ast_mutex_lock(&p->lock);
			gcom_enable_ec(p);
			ast_mutex_unlock(&p->lock);
		}
		else if (!strcasecmp(value, "off"))
		{
			ast_mutex_lock(&p->lock);
			gcom_disable_ec(p);
			ast_mutex_unlock(&p->lock);
		}
		else if (!strcasecmp(value, "fax"))
		{
			int blah = 1;
			ast_mutex_lock(&p->lock);
			if (!p->echocanon)
				gcom_enable_ec(p);
			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_FAX_MODE, &blah))
				ast_log(LOG_WARNING, "Unable to place echocan into fax mode on channel %d: %s\n", p->channel, strerror(errno));
			ast_mutex_unlock(&p->lock);
		}
		else if (!strcasecmp(value, "voice"))
		{
			int blah = 0;
			ast_mutex_lock(&p->lock);
			if (!p->echocanon)
				gcom_enable_ec(p);
			if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_FAX_MODE, &blah))
				ast_log(LOG_WARNING, "Unable to place echocan into voice mode on channel %d: %s\n", p->channel, strerror(errno));
			ast_mutex_unlock(&p->lock);
		}
		else
		{
			ast_log(LOG_WARNING, "Unsupported value '%s' provided for '%s' item.\n", value, data);
			res = -1;
		}
	}
    else
		res = -1;
	return res;
 }

static void gcom_unlink(struct gcom_pvt *slave, struct gcom_pvt *master, int needlock)
{
	/* Unlink a specific slave or all slaves/masters from a given master */
	int x;
	int hasslaves;
	if (!master)
		return;
	if (needlock)
	{
		ast_mutex_lock(&master->lock);
		if (slave)
		{
		  while (ast_mutex_trylock(&slave->lock))
				DEADLOCK_AVOIDANCE(&master->lock);
		}
	}
	hasslaves = 0;
	for (x = 0; x < MAX_SLAVES; x++)
	{
		if (master->slaves[x])
		{
			if (!slave || (master->slaves[x] == slave))
			{
				/* Take slave out of the conference */
				ast_log(LOG_DEBUG, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
				conf_del(master, &master->slaves[x]->subs[SUB_REAL], SUB_REAL);
				conf_del(master->slaves[x], &master->subs[SUB_REAL], SUB_REAL);
				master->slaves[x]->master = NULL;
				master->slaves[x] = NULL;
			}
			else
				hasslaves = 1;
		}
		if (!hasslaves)
			master->inconference = 0;
	}
	if (!slave)
	{
		if (master->master)
		{
			/* Take master out of the conference */
			conf_del(master->master, &master->subs[SUB_REAL], SUB_REAL);
			conf_del(master, &master->master->subs[SUB_REAL], SUB_REAL);
			hasslaves = 0;
			for (x = 0; x < MAX_SLAVES; x++)
			{
				if (master->master->slaves[x] == master)
					master->master->slaves[x] = NULL;
				else if (master->master->slaves[x])
					hasslaves = 1;
			}
			if (!hasslaves)
				master->master->inconference = 0;
		}
		master->master = NULL;
	}
	update_conf(master);
	if (needlock)
	{
		if (slave)
			ast_mutex_unlock(&slave->lock);
		ast_mutex_unlock(&master->lock);
	}
 }

 static void gcom_link(struct gcom_pvt *slave, struct gcom_pvt *master)
 {
	int x;
	if (!slave || !master)
	{
		ast_log(LOG_WARNING, "Tried to link to/from NULL??\n");
		return;
	}
	for (x = 0; x < MAX_SLAVES; x++)
	{
		if (!master->slaves[x])
		{
			master->slaves[x] = slave;
			break;
		}
	}
	if (x >= MAX_SLAVES)
	{
		ast_log(LOG_WARNING, "Replacing slave %d with new slave, %d\n", master->slaves[MAX_SLAVES - 1]->channel, slave->channel);
		master->slaves[MAX_SLAVES - 1] = slave;
	}
	if (slave->master)
		ast_log(LOG_WARNING, "Replacing master %d with new master, %d\n", slave->master->channel, master->channel);
	slave->master = master;
	ast_log(LOG_DEBUG, "Making %d slave to master %d at %d\n", slave->channel, master->channel, x);
 }

 static enum ast_bridge_result gcom_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms)
 {
    ast_log(LOG_DEBUG,"ast_bridge_result\n");
	struct ast_channel *who;
	struct gcom_pvt *p0, *p1, *op0, *op1;
	struct gcom_pvt *master = NULL, *slave = NULL;
	struct ast_frame *f;
	int inconf = 0;
	int nothingok = 1;
	int ofd0, ofd1;
	int oi0, oi1, i0 = -1, i1 = -1, t0, t1;
	int os0 = -1, os1 = -1;
	int priority = 0;
	struct ast_channel *oc0, *oc1;
	enum ast_bridge_result res;
	/* For now, don't attempt to native bridge if either channel needs DTMF detection.
	   There is code below to handle it properly until DTMF is actually seen,
	   but due to currently unresolved issues it's ignored...
	*/
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return AST_BRIDGE_FAILED_NOWARN;
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1))
		CHANNEL_DEADLOCK_AVOIDANCE(c0);
	p0 = ast_channel_tech_pvt(c0);
	p1 = ast_channel_tech_pvt(c1);
	/* cant do pseudo-channels here */
	if (!p0 || (!p0->sig) || !p1 || (!p1->sig))
	{
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}
	oi0 = gcom_get_index(c0, p0, 0);
	oi1 = gcom_get_index(c1, p1, 0);
	if ((oi0 < 0) || (oi1 < 0))
	{
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED;
	}
	op0 = p0 = ast_channel_tech_pvt(c0);
	op1 = p1 = ast_channel_tech_pvt(c1);
	ofd0 = ast_channel_fd(c0, 0);
	ofd1 = ast_channel_fd(c1, 0);
	oc0 = p0->owner;
	oc1 = p1->owner;
	if (ast_mutex_trylock(&p0->lock))
	{
		/* Don't block, due to potential for deadlock */
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_RETRY;
	}
	if (ast_mutex_trylock(&p1->lock))
	{
		/* Don't block, due to potential for deadlock */
		ast_mutex_unlock(&p0->lock);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_RETRY;
	}
	if ((p0->callwaiting && p0->callwaitingcallerid) || (p1->callwaiting && p1->callwaitingcallerid))
	{
		/*
		 * Call Waiting Caller ID requires DTMF detection to know if it
		 * can send the CID spill.
		 *
		 * For now, don't attempt to native bridge if either channel
		 * needs DTMF detection.  There is code below to handle it
		 * properly until DTMF is actually seen, but due to currently
		 * unresolved issues it's ignored...
		 */
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&p1->lock);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}
	if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL))
	{
		if (p0->owner && p1->owner)
		{
			/* If we don't have a call-wait in a 3-way, and we aren't in a 3-way, we can be master */
			if (!p0->subs[SUB_CALLWAIT].inthreeway && !p1->subs[SUB_REAL].inthreeway)
			{
				master = p0;
				slave = p1;
				inconf = 1;
			}
			else if (!p1->subs[SUB_CALLWAIT].inthreeway && !p0->subs[SUB_REAL].inthreeway)
			{
				master = p1;
				slave = p0;
				inconf = 1;
			}
			else
				ast_log(LOG_WARNING, "DEADLOCK p0: chan %d/%d/CW%d/3W%d, p1: chan %d/%d/CW%d/3W%d\n",p0->channel,oi0, (p0->subs[SUB_CALLWAIT].dfd > -1) ? 1 : 0,p0->subs[SUB_REAL].inthreeway, p0->channel,oi0, (p1->subs[SUB_CALLWAIT].dfd > -1) ? 1 : 0,p1->subs[SUB_REAL].inthreeway);
			nothingok = 0;
		}
	}
	else if ((oi0 == SUB_REAL) && (oi1 == SUB_THREEWAY))
	{
		if (p1->subs[SUB_THREEWAY].inthreeway)
		{
			master = p1;
			slave = p0;
			nothingok = 0;
		}
	}
	else if ((oi0 == SUB_THREEWAY) && (oi1 == SUB_REAL))
	{
		if (p0->subs[SUB_THREEWAY].inthreeway)
		{
			master = p0;
			slave = p1;
			nothingok = 0;
		}
	}
	else if ((oi0 == SUB_REAL) && (oi1 == SUB_CALLWAIT))
	{
		/* We have a real and a call wait.  If we're in a three way call, put us in it, otherwise,  don't put us in anything */
		if (p1->subs[SUB_CALLWAIT].inthreeway)
		{
			master = p1;
			slave = p0;
			nothingok = 0;
		}
	}
	else if ((oi0 == SUB_CALLWAIT) && (oi1 == SUB_REAL))
	{
		/* Same as previous */
		if (p0->subs[SUB_CALLWAIT].inthreeway)
		{
			master = p0;
			slave = p1;
			nothingok = 0;
		}
	}
	ast_log(LOG_DEBUG, "master: %d, slave: %d, nothingok: %d\n",master ? master->channel : 0, slave ? slave->channel : 0, nothingok);
	if (master && slave)
	{
		/* Stop any tones, or play ringtone as appropriate.  If they're bridged	   in an active threeway call with a channel that is ringing, we should	   indicate ringing. */
		if ((oi1==SUB_THREEWAY) && p1->subs[SUB_THREEWAY].inthreeway && p1->subs[SUB_REAL].owner && p1->subs[SUB_REAL].inthreeway && (ast_channel_state(p1->subs[SUB_REAL].owner) == AST_STATE_RINGING))
		{
			ast_log(LOG_DEBUG,"Playing ringback on %d/%d(%s) since %d/%d(%s) is in a ringing three-way\n",p0->channel, oi0, ast_channel_name(c0), p1->channel, oi1,ast_channel_name(c1));
			tone_zone_play_tone(p0->subs[oi0].dfd, DAHDI_TONE_RINGTONE);
			os1 = ast_channel_state(p1->subs[SUB_REAL].owner);
		}
		else
		{
			ast_log(LOG_DEBUG,"Stopping tones on %d/%d(%s) talking to %d/%d(%s)\n",p0->channel, oi0, ast_channel_name(c0), p1->channel, oi1,ast_channel_name(c1));
			tone_zone_play_tone(p0->subs[oi0].dfd, -1);
		}
		if ((oi0 == SUB_THREEWAY) && p0->subs[SUB_THREEWAY].inthreeway && p0->subs[SUB_REAL].owner && p0->subs[SUB_REAL].inthreeway &&	(ast_channel_state(p1->subs[SUB_REAL].owner) == AST_STATE_RINGING))
		{
			ast_log(LOG_DEBUG,"Playing ringback on %d/%d(%s) since %d/%d(%s) is in a ringing three-way\n",p1->channel, oi1,ast_channel_name(c1), p0->channel, oi0,ast_channel_name(c0));
			tone_zone_play_tone(p1->subs[oi1].dfd, DAHDI_TONE_RINGTONE);
			os0 = ast_channel_state(p0->subs[SUB_REAL].owner);
		} else
		{
			ast_log(LOG_DEBUG, "Stopping tones on %d/%d(%s) talking to %d/%d(%s)\n",p1->channel, oi1, ast_channel_name(c1), p0->channel, oi0, ast_channel_name(c0));
			tone_zone_play_tone(p1->subs[oi1].dfd, -1);
		}
		if ((oi0 == SUB_REAL) && (oi1 == SUB_REAL))
		{
			if (!p0->echocanbridged || !p1->echocanbridged)
			{
				/* Disable echo cancellation if appropriate */
				gcom_disable_ec(p0);
				gcom_disable_ec(p1);
			}
		}
		gcom_link(slave, master);
		master->inconference = inconf;
	} else if (!nothingok)
		ast_log(LOG_WARNING, "Can't link %d/%s with %d/%s\n", p0->channel, subnames[oi0], p1->channel, subnames[oi1]);
	update_conf(p0);
	update_conf(p1);
	t0 = p0->subs[SUB_REAL].inthreeway;
	t1 = p1->subs[SUB_REAL].inthreeway;
	ast_mutex_unlock(&p0->lock);
	ast_mutex_unlock(&p1->lock);
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);
	/* Native bridge failed */
	if ((!master || !slave) && !nothingok)
	{
		gcom_enable_ec(p0);
		gcom_enable_ec(p1);
		return AST_BRIDGE_FAILED;
	}
	ast_log(LOG_DEBUG, "Native bridging %s and %s\n", ast_channel_name(c0),ast_channel_name(c1));
	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0) && (oi0 == SUB_REAL))
		disable_dtmf_detect(op0);
	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1) && (oi1 == SUB_REAL))
		disable_dtmf_detect(op1);
	for (;;)
	{
		struct ast_channel *c0_priority[2] = {c0, c1};
		struct ast_channel *c1_priority[2] = {c1, c0};
		/* Here's our main loop...  Start by locking things, looking for private parts,  and then balking if anything is wrong */
		ast_channel_lock(c0);
		while (ast_channel_trylock(c1))
			CHANNEL_DEADLOCK_AVOIDANCE(c0);
		p0 = ast_channel_tech_pvt(c0);
		p1 = ast_channel_tech_pvt(c1);
		if (op0 == p0)
			i0 = gcom_get_index(c0, p0, 1);
		if (op1 == p1)
			i1 = gcom_get_index(c1, p1, 1);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		if (!timeoutms || (op0 != p0) || (op1 != p1) ||	(ofd0 !=  ast_channel_fd(c0,0)) ||	(ofd1 != ast_channel_fd(c1,0)) ||
			(p0->subs[SUB_REAL].owner && (os0 > -1) && (os0 != ast_channel_state(p0->subs[SUB_REAL].owner))) ||
			(p1->subs[SUB_REAL].owner && (os1 > -1) && (os1 != ast_channel_state(p1->subs[SUB_REAL].owner))) ||
			(oc0 != p0->owner) ||
			(oc1 != p1->owner) ||
			(t0 != p0->subs[SUB_REAL].inthreeway) ||
			(t1 != p1->subs[SUB_REAL].inthreeway) ||
			(oi0 != i0) ||
			(oi1 != i1))
		{
			ast_log(LOG_DEBUG, "Something changed out on %d/%d to %d/%d, returning -3 to restart\n",op0->channel, oi0, op1->channel, oi1);
			res = AST_BRIDGE_RETRY;
			goto return_from_bridge;
		}
		who = ast_waitfor_n(priority ? c0_priority : c1_priority, 2, &timeoutms);
		if (!who)
			continue;
		f = ast_read(who);
		if (!f || (f->frametype == AST_FRAME_CONTROL))
		{
			*fo = f;
			*rc = who;
			res = AST_BRIDGE_COMPLETE;
			goto return_from_bridge;
		}
		if (f->frametype == AST_FRAME_DTMF)
		{
			if ((who == c0) && p0->pulsedial)
				ast_write(c1, f);
			else if ((who == c1) && p1->pulsedial)
				ast_write(c0, f);
			else
			{
				*fo = f;
				*rc = who;
				res = AST_BRIDGE_COMPLETE;
				goto return_from_bridge;
			}
		}
		ast_frfree(f);
		/* Swap who gets priority */
		priority = !priority;
	}
return_from_bridge:
	if (op0 == p0)
		gcom_enable_ec(p0);
	if (op1 == p1)
		gcom_enable_ec(p1);
	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_0) && (oi0 == SUB_REAL))
		enable_dtmf_detect(op0);
	if (!(flags & AST_BRIDGE_DTMF_CHANNEL_1) && (oi1 == SUB_REAL))
		enable_dtmf_detect(op1);
	gcom_unlink(slave, master, 1);
	return res;
 }

 static int gcom_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
 {
	struct gcom_pvt *p =  ast_channel_tech_pvt(newchan);
	int x;
	ast_mutex_lock(&p->lock);
	ast_log(LOG_DEBUG,"New owner for channel %d is %s\n", p->channel,  ast_channel_name(newchan));
	if (p->owner == oldchan)
		p->owner = newchan;
	for (x = 0; x < 3; x++)
	{
		if (p->subs[x].owner == oldchan)
		{
			if (!x)
				gcom_unlink(NULL, p, 0);
			p->subs[x].owner = newchan;
		}
	}
	update_conf(p);
	ast_mutex_unlock(&p->lock);
	if (ast_channel_state(newchan) == AST_STATE_RINGING)
		gcom_indicate(newchan, AST_CONTROL_RINGING, NULL, 0);
	return 0;
}

 static int gcom_ring_phone(struct gcom_pvt *p)
 {
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = DAHDI_ONHOOK;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
	do
	{
		x = DAHDI_RING;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
		if (res)
		{
			switch (errno)
			{
			case EBUSY:
			case EINTR:
				/* Wait just in case */
				usleep(10000);
				continue;
			case EINPROGRESS:
				res = 0;
				break;
			default:
				ast_log(LOG_WARNING, "Couldn't ring the phone: %s\n", strerror(errno));
				res = 0;
			}
		}
	} while (res);
	return res;
 }

 static int attempt_transfer(struct gcom_pvt *p)
 {
	/* In order to transfer, we need at least one of the channels to
	   actually be in a call bridge.  We can't conference two applications
	   together (but then, why would we want to?) */
	if (ast_channel_is_bridged(p->subs[SUB_REAL].owner))
	{
		/* The three-way person we're about to transfer to could still be in MOH, so
		   stop if now if appropriate */
		if (ast_channel_is_bridged(p->subs[SUB_THREEWAY].owner))
			ast_queue_control(p->subs[SUB_THREEWAY].owner, AST_CONTROL_UNHOLD);
		if (ast_channel_state(p->subs[SUB_REAL].owner) == AST_STATE_RINGING)
			ast_indicate(ast_channel_is_bridged(p->subs[SUB_REAL].owner), AST_CONTROL_RINGING);
		if (ast_channel_state(p->subs[SUB_THREEWAY].owner) == AST_STATE_RING)
			tone_zone_play_tone(p->subs[SUB_THREEWAY].dfd, DAHDI_TONE_RINGTONE);
		/* Orphan the channel after releasing the lock */
		ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
		unalloc_sub(p, SUB_THREEWAY);
	}
	else if (ast_channel_is_bridged(p->subs[SUB_THREEWAY].owner))
	{
		ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
		if (ast_channel_state(p->subs[SUB_THREEWAY].owner) == AST_STATE_RINGING)
			ast_indicate(ast_channel_is_bridged(p->subs[SUB_THREEWAY].owner), AST_CONTROL_RINGING);
		if (ast_channel_state(p->subs[SUB_REAL].owner) == AST_STATE_RING)
			tone_zone_play_tone(p->subs[SUB_REAL].dfd, DAHDI_TONE_RINGTONE);
		/* Three-way is now the REAL */
		swap_subs(p, SUB_THREEWAY, SUB_REAL);
		ast_channel_unlock(p->subs[SUB_REAL].owner);
		unalloc_sub(p, SUB_THREEWAY);
		/* Tell the caller not to hangup */
		return 1;
	}
	else
	{
		ast_log(LOG_DEBUG, "Neither %s nor %s are in a bridge, nothing to transfer\n",ast_channel_name(p->subs[SUB_REAL].owner), ast_channel_name(p->subs[SUB_THREEWAY].owner));
        ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
		return -1;
	}
	return 0;
 }


/*! Checks channel for alarms
 * \param p a channel to check for alarms.
 * \returns the alarms on the span to which the channel belongs, or alarms on
 *          the channel if no span alarms.
 */
 static int get_alarms(struct gcom_pvt *p)
 {
	int res;
	struct dahdi_spaninfo zi;
	struct dahdi_params params;
	memset(&zi, 0, sizeof(zi));
	zi.spanno = p->span;
	if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SPANSTAT, &zi)) >= 0)
	{
		if (zi.alarms != DAHDI_ALARM_NONE)
			return zi.alarms;
	}
	else
	{
		ast_log(LOG_WARNING, "Unable to determine alarm on channel %d: %s\n", p->channel, strerror(errno));
		return 0;
	}
	/* No alarms on the span. Check for channel alarms. */
	memset(&params, 0, sizeof(params));
	if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &params)) >= 0)
		return params.chan_alarms;
	ast_log(LOG_WARNING, "Unable to determine alarm on channel %d\n", p->channel);
	return DAHDI_ALARM_NONE;
 }

 static void gcom_handle_dtmf(struct ast_channel *ast, int idx, struct ast_frame **dest)
 {
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_frame *f = *dest;
	ast_log(LOG_DEBUG, "%s DTMF digit: 0x%02X '%c' on %s\n",f->frametype == AST_FRAME_DTMF_BEGIN ? "Begin" : "End",f->subclass.integer, f->subclass.integer, ast_channel_name(ast));
	if (p->confirmanswer)
	{
		if (f->frametype == AST_FRAME_DTMF_END)
		{
			ast_log(LOG_DEBUG, "Confirm answer on %s!\n", ast_channel_name(ast));
			/* Upon receiving a DTMF digit, consider this an answer confirmation instead  of a DTMF digit */
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
			/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
			p->confirmanswer = 0;
		}
		else
		{
			p->subs[idx].f.frametype = AST_FRAME_NULL;
			p->subs[idx].f.subclass.integer = 0;
		}
		*dest = &p->subs[idx].f;
	}
	else if (p->callwaitcas)
	{
		if (f->frametype == AST_FRAME_DTMF_END)
		{
			if ((f->subclass.integer == 'A') || (f->subclass.integer == 'D'))
			{
				ast_log(LOG_DEBUG, "Got some DTMF, but it's for the CAS\n");
				ast_free(p->cidspill);
				p->cidspill = NULL;
				send_cwcidspill(p);
			}
			p->callwaitcas = 0;
		}
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		*dest = &p->subs[idx].f;
	}
	else if (f->subclass.integer == 'f')
	{
		if (f->frametype == AST_FRAME_DTMF_END)
		{
			/* Fax tone -- Handle and return NULL */
			if ((p->callprogress & CALLPROGRESS_FAX) && !p->faxhandled)
			{
				/* If faxbuffers are configured, use them for the fax transmission */
				if (p->usefaxbuffers && !p->bufferoverrideinuse)
				{
					struct dahdi_bufferinfo bi =
					{
						.txbufpolicy = p->faxbuf_policy,
						.bufsize = p->bufsize,
						.numbufs = p->faxbuf_no
					};
					int res;
					if ((res = ioctl(p->subs[idx].dfd, DAHDI_SET_BUFINFO, &bi)) < 0)
						ast_log(LOG_WARNING, "Channel '%s' unable to set buffer policy, reason: %s\n", ast_channel_name(ast), strerror(errno));
					else
						p->bufferoverrideinuse = 1;
				}
				p->faxhandled = 1;
				if (p->dsp)
				{
					p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
					ast_dsp_set_features(p->dsp, p->dsp_features);
					ast_log(LOG_DEBUG, "Disabling FAX tone detection on %s after tone received\n", ast_channel_name(ast));
				}
				if (strcmp(ast_channel_exten(ast), "fax"))
				{
					const char *target_context = S_OR(ast_channel_macrocontext(ast), ast_channel_context(ast));
					/* We need to unlock 'ast' here because ast_exists_extension has the
					 * potential to start autoservice on the channel. Such action is prone
					 * to deadlock.
					 */
					ast_mutex_unlock(&p->lock);
					ast_channel_unlock(ast);
					if (ast_exists_extension(ast, target_context, "fax", 1,
						S_COR(ast_channel_caller(ast)->id.number.valid,ast_channel_caller(ast)->id.number.str, NULL)))
					{
						ast_channel_lock(ast);
						ast_mutex_lock(&p->lock);
						ast_log(LOG_DEBUG, "Redirecting %s to fax extension\n", ast_channel_name(ast));
						/* Save the DID/DNIS when we transfer the fax call to a "fax" extension */
						pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast_channel_exten(ast));
						if (ast_async_goto(ast, target_context, "fax", 1))
							ast_log(LOG_WARNING, "Failed to async goto '%s' into fax of '%s'\n", ast_channel_name(ast), target_context);
					}
					else
					{
						ast_channel_lock(ast);
						ast_mutex_lock(&p->lock);
						ast_log(LOG_NOTICE, "Fax detected, but no fax extension\n");
					}
				}
				else
					ast_log(LOG_DEBUG, "Already in a fax extension, not redirecting\n");
			}
			else
				ast_log(LOG_DEBUG,"Fax already handled\n");
			gcom_confmute(p, 0);
		}
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		*dest = &p->subs[idx].f;
	}
 }

 static void handle_alarms(struct gcom_pvt *p, int alms)
 {
	const char *alarm_str = alarm2str(alms);
	if (report_alarms & REPORT_CHANNEL_ALARMS)
	{
		ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm_str);
		manager_event(EVENT_FLAG_SYSTEM, "Alarm",
					  "Alarm: %s\r\n"
					  "Channel: %d\r\n",
					  alarm_str, p->channel);
	}
	if (report_alarms & REPORT_SPAN_ALARMS && p->manages_span_alarms)
	{
		ast_log(LOG_WARNING, "Detected alarm on span %d: %s\n", p->span, alarm_str);
		manager_event(EVENT_FLAG_SYSTEM, "SpanAlarm",
					  "Alarm: %s\r\n"
					  "Span: %d\r\n",
					  alarm_str, p->span);
	}
 }

 static struct ast_frame *gcom_handle_event(struct ast_channel *ast)
 {
    ast_log(LOG_DEBUG,"gcom_handle_event\n");
	int res, x;
	int idx, mysig;
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_frame *f;
	idx = gcom_get_index(ast, p, 0);
	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;
	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.src = "gcom_handle_event";
	p->subs[idx].f.data.ptr = NULL;
	f = &p->subs[idx].f;
	if (idx < 0)
		return &p->subs[idx].f;
	if (p->fake_event)
	{
		res = p->fake_event;
		p->fake_event = 0;
	}
	else
		res = gcom_get_event(p->subs[idx].dfd);
	ast_log(LOG_DEBUG, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, idx);
	if (res & (DAHDI_EVENT_PULSEDIGIT | DAHDI_EVENT_DTMFUP))
	{
		p->pulsedial = (res & DAHDI_EVENT_PULSEDIGIT) ? 1 : 0;
		ast_log(LOG_DEBUG, "Detected %sdigit '%c'\n", p->pulsedial ? "pulse ": "", res & 0xff);
		{
			/* Unmute conference */
			gcom_confmute(p, 0);
			p->subs[idx].f.frametype = AST_FRAME_DTMF_END;
			p->subs[idx].f.subclass.integer = res & 0xff;
			gcom_handle_dtmf(ast, idx, &f);
		}
		return f;
	}
	if (res & DAHDI_EVENT_DTMFDOWN)
	{
		ast_log(LOG_DEBUG, "DTMF Down '%c'\n", res & 0xff);
		{
			/* Mute conference */
			gcom_confmute(p, 1);
			p->subs[idx].f.frametype = AST_FRAME_DTMF_BEGIN;
			p->subs[idx].f.subclass.integer = res & 0xff;
			gcom_handle_dtmf(ast, idx, &f);
		}
		return &p->subs[idx].f;
	}

	switch (res)
	{
	 case DAHDI_EVENT_EC_DISABLED:
		ast_log(LOG_DEBUG, "Channel %d echo canceler disabled.\n", p->channel);
		p->echocanon = 0;
		break;
 	 case DAHDI_EVENT_TX_CED_DETECTED:
 		ast_log(LOG_DEBUG, "Channel %d detected a CED tone towards the network.\n", p->channel);
		break;
	 case DAHDI_EVENT_RX_CED_DETECTED:
	    ast_log(LOG_DEBUG, "Channel %d detected a CED tone from the network.\n", p->channel);
		break;
	 case DAHDI_EVENT_EC_NLP_DISABLED:
		ast_log(LOG_DEBUG, "Channel %d echo canceler disabled its NLP.\n", p->channel);
		break;
	 case DAHDI_EVENT_EC_NLP_ENABLED:
		ast_log(LOG_DEBUG, "Channel %d echo canceler enabled its NLP.\n", p->channel);
		break;
	 case DAHDI_EVENT_BITSCHANGED:
	 case DAHDI_EVENT_PULSE_START:
		/* Stop tone if there's a pulse start and the PBX isn't started */
		if (!ast_channel_pbx(ast))
			tone_zone_play_tone(p->subs[idx].dfd, -1);
		break;
	 case DAHDI_EVENT_DIALCOMPLETE:
		if (p->inalarm)
			break;
		if ((p->radio || (p->oprmode < 0)))
			break;
		if (ioctl(p->subs[idx].dfd,DAHDI_DIALING,&x) == -1)
		{
			ast_log(LOG_DEBUG, "DAHDI_DIALING ioctl failed on %s: %s\n",ast_channel_name(ast), strerror(errno));
			return NULL;
		}
		if (!x)  /* if not still dialing in driver */
		{
			gcom_enable_ec(p);
			if (p->echobreak)
			{
				gcom_train_ec(p);
				ast_copy_string(p->dop.dialstr, p->echorest, sizeof(p->dop.dialstr));
				p->dop.op = DAHDI_DIAL_OP_REPLACE;
				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				p->echobreak = 0;
			}
			else
			{
				p->dialing = 0;
				if (ast_channel_state(ast) == AST_STATE_DIALING)
				{
					if ((p->callprogress & CALLPROGRESS_PROGRESS) && CANPROGRESSDETECT(p) && p->dsp && p->outgoing)
						ast_log(LOG_DEBUG,"Done dialing, but waiting for progress detection before doing more...\n");
					else if (p->confirmanswer || (!p->dialednone && ((mysig == SIG_GSM))))
						ast_setstate(ast, AST_STATE_RINGING);
					else if (!p->answeronpolarityswitch)
					{
						ast_setstate(ast, AST_STATE_UP);
						p->subs[idx].f.frametype = AST_FRAME_CONTROL;
						p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
						/* If aops=0 and hops=1, this is necessary */
						p->polarity = POLARITY_REV;
					}
					else /* Start clean, so we can catch the change to REV polarity when party answers */
						p->polarity = POLARITY_IDLE;
				}
			}
		}
		break;
	 case DAHDI_EVENT_ALARM:
		switch (p->sig)
		{
		 default:
			p->inalarm = 1;
			break;
		}
		res = get_alarms(p);
		handle_alarms(p, res);
	 case DAHDI_EVENT_ONHOOK:
		if (p->radio)
		{
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_UNKEY;
			break;
		}
		if (p->oprmode < 0)
		{
			if (p->oprmode != -1) break;
			break;
		}
		switch (p->sig)
		{
		 case SIG_GSM:
			/* Check for some special conditions regarding call waiting */
			if (idx == SUB_REAL)
			{
				/* The normal line was hung up */
				if (p->subs[SUB_CALLWAIT].owner)
				{
					/* There's a call waiting call, so ring the phone, but make it unowned in the mean time */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					ast_log(LOG_DEBUG,"Channel %d still has (callwait) call, ringing phone\n", p->channel);
					unalloc_sub(p, SUB_CALLWAIT);
					p->callwaitingrepeat = 0;
					p->cidcwexpire = 0;
					p->cid_suppress_expire = 0;
					p->owner = NULL;
					/* Don't start streaming audio yet if the incoming call isn't up yet */
					if (ast_channel_state(p->subs[SUB_REAL].owner) != AST_STATE_UP)
						p->dialing = 1;
					gcom_ring_phone(p);
				}
				else if (p->subs[SUB_THREEWAY].owner)
				{
					unsigned int mssinceflash;
					/* Here we have to retain the lock on both the main channel, the 3-way channel, and
					   the private structure -- not especially easy or clean */
					while (p->subs[SUB_THREEWAY].owner && ast_channel_trylock(p->subs[SUB_THREEWAY].owner))
					{
						/* Yuck, didn't get the lock on the 3-way, gotta release everything and re-grab! */
						DLA_UNLOCK(&p->lock);
						CHANNEL_DEADLOCK_AVOIDANCE(ast);
						/* We can grab ast and p in that order, without worry.  We should make sure
						   nothing seriously bad has happened though like some sort of bizarre double
						   masquerade! */
						DLA_LOCK(&p->lock);
						if (p->owner != ast)
							return NULL;
					}
					if (!p->subs[SUB_THREEWAY].owner)
						return NULL;
					mssinceflash = ast_tvdiff_ms(ast_tvnow(), p->flashtime);
					ast_log(LOG_DEBUG, "Last flash was %d ms ago\n", mssinceflash);
					if (mssinceflash < MIN_MS_SINCE_FLASH)
					{
						/* It hasn't been long enough since the last flashook.  This is probably a bounce on
						   hanging up.  Hangup both channels now */
						if (p->subs[SUB_THREEWAY].owner)
							ast_queue_hangup_with_cause(p->subs[SUB_THREEWAY].owner, AST_CAUSE_NO_ANSWER);
                        ast_channel_softhangup_internal_flag_add(p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
                        ast_log(LOG_DEBUG, "Looks like a bounced flash, hanging up both calls on %d\n", p->channel);
						ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
					}
					else if ((ast_channel_pbx(ast)) || (ast_channel_state(ast) == AST_STATE_UP))
					{
						if (p->transfer)
						{
							/* In any case this isn't a threeway call anymore */
							p->subs[SUB_REAL].inthreeway = 0;
							p->subs[SUB_THREEWAY].inthreeway = 0;
							/* Only attempt transfer if the phone is ringing; why transfer to busy tone eh? */
							if (!p->transfertobusy && ast_channel_state(ast) == AST_STATE_BUSY)
							{
								ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
								/* Swap subs and dis-own channel */
								swap_subs(p, SUB_THREEWAY, SUB_REAL);
								p->owner = NULL;
								/* Ring the phone */
								gcom_ring_phone(p);
							}
							else
							{
								if ((res = attempt_transfer(p)) < 0)
								{
									ast_channel_softhangup_internal_flag_add(	p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
									if (p->subs[SUB_THREEWAY].owner)
										ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
								}
								else if (res)
								{
									/* Don't actually hang up at this point */
									if (p->subs[SUB_THREEWAY].owner)
										ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
									break;
								}
							}
						}
						else
						{
							ast_channel_softhangup_internal_flag_add(	p->subs[SUB_THREEWAY].owner, AST_SOFTHANGUP_DEV);
							if (p->subs[SUB_THREEWAY].owner)
								ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
						}
					}
					else
					{
						ast_channel_unlock(p->subs[SUB_THREEWAY].owner);
						/* Swap subs and dis-own channel */
						swap_subs(p, SUB_THREEWAY, SUB_REAL);
						p->owner = NULL;
						/* Ring the phone */
						gcom_ring_phone(p);
					}
				}
			}
			else
				ast_log(LOG_WARNING, "Got a hangup and my index is %d?\n", idx);
			/* Fall through */
		default:
			gcom_disable_ec(p);
			return NULL;
		}
		break;
	case DAHDI_EVENT_RINGOFFHOOK:
		if (p->inalarm)
			break;
		if (p->oprmode < 0)
			break;
		if (p->radio)
		{
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_KEY;
			break;
 		}
		/* for E911, its supposed to wait for offhook then dial the second half of the dial string */
		break;
	case DAHDI_EVENT_RINGBEGIN:
		switch (p->sig)
		{
		 case SIG_GSM:
			break;
		}
		break;
 	 case DAHDI_EVENT_RINGERON:
	 	 break;
	 case DAHDI_EVENT_NOALARM:
		 switch (p->sig)
		 {
		  default:
			p->inalarm = 0;
			break;
		}
		handle_clear_alarms(p);
		break;
	 case DAHDI_EVENT_WINKFLASH:
		break;
	 case DAHDI_EVENT_HOOKCOMPLETE:
		if (p->inalarm)
			break;
		if ((p->radio || (p->oprmode < 0)))
			break;
		if (p->waitingfordt.tv_sec)
			break;
		switch (mysig)
		{
		 case SIG_GSM:
			if (!ast_strlen_zero(p->dop.dialstr))
			{
				res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_DIAL, &p->dop);
				if (res < 0)
				{
					ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", p->channel, strerror(errno));
					p->dop.dialstr[0] = '\0';
					return NULL;
				}
				else
				ast_log(LOG_DEBUG,"Sent deferred digit string: %s\n", p->dop.dialstr);
			}
			p->dop.dialstr[0] = '\0';
			p->dop.op = DAHDI_DIAL_OP_REPLACE;
			break;
		 default:
			break;
		}
		break;
	case DAHDI_EVENT_POLARITY:
		/*
		 * If we get a Polarity Switch event, check to see
		 * if we should change the polarity state and
		 * mark the channel as UP or if this is an indication
		 * of remote end disconnect.
		 */
		if (p->polarity == POLARITY_IDLE)
		{
			p->polarity = POLARITY_REV;
			if (p->answeronpolarityswitch && ((ast_channel_state(ast) == AST_STATE_DIALING) ||	(ast_channel_state(ast) == AST_STATE_RINGING)))
			{
				ast_log(LOG_DEBUG, "Answering on polarity switch!\n");
				ast_setstate(p->owner, AST_STATE_UP);
				if (p->hanguponpolarityswitch)
					p->polaritydelaytv = ast_tvnow();
			}
			else
				ast_log(LOG_DEBUG, "Ignore switch to REVERSED Polarity on channel %d, state %d\n", p->channel, ast_channel_state(ast));
		 }
		 /* Removed else statement from here as it was preventing hangups from ever happening*/
		 /* Added AST_STATE_RING in if statement below to deal with calling party hangups that take place when ringing */
		 if (p->hanguponpolarityswitch &&
			(p->polarityonanswerdelay > 0) &&
			(p->polarity == POLARITY_REV) &&
			((ast_channel_state(ast) == AST_STATE_UP) || (ast_channel_state(ast) == AST_STATE_RING)) )
		 {
			/* Added log_debug information below to provide a better indication of what is going on */
			 ast_log(LOG_DEBUG, "Polarity Reversal event occured - DEBUG 1: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %" PRIi64 "\n", p->channel, ast_channel_state(ast), p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
			if (ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) > p->polarityonanswerdelay)
			{
				ast_log(LOG_DEBUG, "Polarity Reversal detected and now Hanging up on channel %d\n", p->channel);
				ast_softhangup(p->owner, AST_SOFTHANGUP_EXPLICIT);
				p->polarity = POLARITY_IDLE;
			}
			else
				ast_log(LOG_DEBUG, "Polarity Reversal detected but NOT hanging up (too close to answer event) on channel %d, state %d\n", p->channel,ast_channel_state(ast));
		}
		else
		{
			p->polarity = POLARITY_IDLE;
			ast_log(LOG_DEBUG, "Ignoring Polarity switch to IDLE on channel %d, state %d\n", p->channel, ast_channel_state(ast));
		}
		/* Added more log_debug information below to provide a better indication of what is going on */
		ast_log(LOG_DEBUG, "Polarity Reversal event occured - DEBUG 2: channel %d, state %d, pol= %d, aonp= %d, honp= %d, pdelay= %d, tv= %" PRIi64 "\n", p->channel,ast_channel_state(ast), p->polarity, p->answeronpolarityswitch, p->hanguponpolarityswitch, p->polarityonanswerdelay, ast_tvdiff_ms(ast_tvnow(), p->polaritydelaytv) );
		break;
	default:
		ast_log(LOG_DEBUG, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[idx].f;
 }

 static struct ast_frame *__gcom_exception(struct ast_channel *ast)
 {
	int res;
	int idx;
	struct ast_frame *f;
	int usedindex = -1;
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	idx = gcom_get_index(ast, p, 1);
	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "gcom_exception";
	p->subs[idx].f.data.ptr = NULL;
	if ((!p->owner) && (!(p->radio || (p->oprmode < 0))))
	{
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		if (p->fake_event)
		{
			res = p->fake_event;
			p->fake_event = 0;
		}
		else
			res = gcom_get_event(p->subs[SUB_REAL].dfd);
		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != DAHDI_EVENT_RINGEROFF) && (res != DAHDI_EVENT_RINGERON) &&	(res != DAHDI_EVENT_HOOKCOMPLETE))
		{
			ast_log(LOG_DEBUG,"Restoring owner of channel %d on event %d\n", p->channel, res);
			p->owner = p->subs[SUB_REAL].owner;
			if (p->owner && ast_channel_is_bridged(p->owner))
				ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
			p->subs[SUB_REAL].needunhold = 1;
		}
		switch (res)
		{
		 case DAHDI_EVENT_ONHOOK:
			gcom_disable_ec(p);
			if (p->owner)
			{
				ast_log(LOG_DEBUG, "Channel %s still has call, ringing phone\n", ast_channel_name(p->owner));
				gcom_ring_phone(p);
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				p->cid_suppress_expire = 0;
			}
			else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			update_conf(p);
			break;
		 case DAHDI_EVENT_RINGOFFHOOK:
			gcom_enable_ec(p);
			gcom_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
			if (p->owner && ( ast_channel_state(p->owner) == AST_STATE_RINGING))
			{
				p->subs[SUB_REAL].needanswer = 1;
				p->dialing = 0;
			}
			break;
		 case DAHDI_EVENT_HOOKCOMPLETE:
		 case DAHDI_EVENT_RINGERON:
		 case DAHDI_EVENT_RINGEROFF:
  			 break;
		 case DAHDI_EVENT_WINKFLASH:
			 p->flashtime = ast_tvnow();
			 if (p->owner)
			 {
				ast_log(LOG_DEBUG, "Channel %d flashed to other channel %s\n", p->channel, ast_channel_name(p->owner));
				if (ast_channel_state(p->owner)!= AST_STATE_UP)
				{
					usedindex = gcom_get_index(p->owner, p, 0);
					if (usedindex > -1)
						p->subs[usedindex].needanswer = 1;
					ast_setstate(p->owner, AST_STATE_UP);
				}
				p->callwaitingrepeat = 0;
				p->cidcwexpire = 0;
				p->cid_suppress_expire = 0;
				if (ast_channel_is_bridged(p->owner))
					ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
				p->subs[SUB_REAL].needunhold = 1;
			 }
			 else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
			 update_conf(p);
			 break;
	 	 default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		f = &p->subs[idx].f;
		return f;
	}
	if (!(p->radio || (p->oprmode < 0)))
		ast_log(LOG_DEBUG,"Exception on %d, channel %d\n", ast_channel_fd(ast, 0),p->channel);
	/* If it's not us, return NULL immediately */
	if (ast != p->owner)
	{
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast_channel_name(ast),ast_channel_name(p->owner));
		f = &p->subs[idx].f;
		return f;
	}
	f = gcom_handle_event(ast);
	/* tell the cdr this zap device hung up */
	if (f == NULL)
		ast_set_hangupsource(ast, ast_channel_name(ast), 0);
	return f;
 }

 static struct ast_frame *gcom_exception(struct ast_channel *ast)
 {
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_frame *f;
	ast_mutex_lock(&p->lock);
	f = __gcom_exception(ast);
	ast_mutex_unlock(&p->lock);
	return f;
 }

 static struct ast_frame *gcom_read(struct ast_channel *ast)
 {
	struct gcom_pvt *p;
	int res;
	int idx;
	void *readbuf;
	struct ast_frame *f;
	/*
	 * For analog channels, we must do deadlock avoidance because
	 * analog ports can have more than one Asterisk channel using
	 * the same private structure.
	 */
	p = ast_channel_tech_pvt(ast);
	while (ast_mutex_trylock(&p->lock))
	{
		CHANNEL_DEADLOCK_AVOIDANCE(ast);
		/*
		 * For GSM channels, we must refresh the private pointer because
		 * the call could move to another B channel while the Asterisk
		 * channel is unlocked.
		 */
		p = ast_channel_tech_pvt(ast);
	}
	idx = gcom_get_index(ast, p, 0);
	/* Hang up if we don't really exist */
	if (idx < 0)
	{
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	if ((p->radio || (p->oprmode < 0)) && p->inalarm)
	{
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.subclass.integer = 0;
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "gcom_read";
	p->subs[idx].f.data.ptr = NULL;
	/* make sure it sends initial key state as first frame */
	if ((p->radio || (p->oprmode < 0)) && (!p->firstradio))
	{
		struct dahdi_params ps;
		memset(&ps, 0, sizeof(ps));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0)
		{
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		p->firstradio = 1;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		if (ps.rxisoffhook)
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_KEY;
		else
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_UNKEY;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->ringt > 0)
	{
		if (!(--p->ringt))
		{
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
	}
	if (p->subs[idx].needringing)
	{
		/* Send ringing frame if requested */
		p->subs[idx].needringing = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_RINGING;
		ast_setstate(ast, AST_STATE_RINGING);
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->subs[idx].needbusy)
	{
		/* Send busy frame if requested */
		p->subs[idx].needbusy = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_BUSY;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->subs[idx].needcongestion)
	{
		/* Send congestion frame if requested */
		p->subs[idx].needcongestion = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_CONGESTION;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->subs[idx].needanswer)
	{
		/* Send answer frame if requested */
		p->subs[idx].needanswer = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->subs[idx].needflash)
	{
		/* Send answer frame if requested */
		p->subs[idx].needflash = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_FLASH;
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	if (p->subs[idx].needhold)
	{
		/* Send answer frame if requested */
		p->subs[idx].needhold = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_HOLD;
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_DEBUG, "Sending hold on '%s'\n", ast_channel_name(ast));
		return &p->subs[idx].f;
	}
	if (p->subs[idx].needunhold)
	{
		/* Send answer frame if requested */
		p->subs[idx].needunhold = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		p->subs[idx].f.subclass.integer = AST_CONTROL_UNHOLD;
		ast_mutex_unlock(&p->lock);
		ast_log(LOG_DEBUG, "Sending unhold on '%s'\n", ast_channel_name(ast));
		return &p->subs[idx].f;
	}
	/*
	 * If we have a fake_event, fake an exception to handle it only
	 * if this channel owns the private.
	 */
	if (p->fake_event && p->owner == ast)
	{
		f = __gcom_exception(ast);
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if(ast_format_cmp(ast_format_slin, ast_channel_rawreadformat(ast)) == AST_FORMAT_CMP_EQUAL)
	{
		if (!p->subs[idx].linear)
		{
			p->subs[idx].linear = 1;
			res = gcom_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, idx);
		}
	}
    else if (ast_format_cmp(ast_format_ulaw, ast_channel_rawreadformat(ast)) == AST_FORMAT_CMP_EQUAL ||
	 		 ast_format_cmp(ast_format_alaw, ast_channel_rawreadformat(ast)) == AST_FORMAT_CMP_EQUAL)
    {
		if (p->subs[idx].linear)
		{
			p->subs[idx].linear = 0;
			res = gcom_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to companded mode.\n", p->channel, idx);
		}
    }
    else
    {
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_format_get_name(ast_channel_rawreadformat(ast)));
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->subs[idx].buffer) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	res = read(p->subs[idx].dfd, readbuf, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
	ast_clear_flag(ast_channel_flags(ast), AST_FLAG_BLOCKING);
	/* Check for hangup */
	if (res < 0)
	{
		f = NULL;
		if (res == -1)
		{
			if (errno == EAGAIN)
			{
				/* Return "NULL" frame if there is nobody there */
				ast_mutex_unlock(&p->lock);
				return &p->subs[idx].f;
			}
			else if (errno == ELAST)
			   f = __gcom_exception(ast);
			else
			   ast_log(LOG_WARNING, "dahdi_rec: %s\n", strerror(errno));
		}
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (res != (p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE))
	{
		ast_log(LOG_DEBUG, "Short read (%d/%d), must be an event...\n", res, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
		f = __gcom_exception(ast);
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (p->tdd)  /* if in TDD mode, see if we receive that */
	{
		int c;
		c = tdd_feed(p->tdd,readbuf,READ_SIZE);
		if (c < 0)
		{
			ast_log(LOG_DEBUG,"tdd_feed failed\n");
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		if (c)  /* if a char to return */
		{
			p->subs[idx].f.subclass.integer = 0;
			p->subs[idx].f.frametype = AST_FRAME_TEXT;
			p->subs[idx].f.mallocd = 0;
			p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
			p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET;
			p->subs[idx].f.datalen = 1;
			*((char *) p->subs[idx].f.data.ptr) = c;
			ast_mutex_unlock(&p->lock);
			return &p->subs[idx].f;
		}
	}
	if (idx == SUB_REAL)
	{
		/* Ensure the CW timers decrement only on a single subchannel */
		if (p->cidcwexpire)
		{
			if (!--p->cidcwexpire)
			{
				/* Expired CID/CW */
				ast_log(LOG_DEBUG,"CPE does not support Call Waiting Caller*ID.\n");
				restore_conference(p);
			}
		}
		if (p->cid_suppress_expire)
			--p->cid_suppress_expire;
		if (p->callwaitingrepeat)
		{
			if (!--p->callwaitingrepeat)
			{
				/* Expired, Repeat callwaiting tone */
				++p->callwaitrings;
				gcom_callwait(ast);
			}
		}
	}
	if (p->subs[idx].linear)
		p->subs[idx].f.datalen = READ_SIZE * 2;
	else
		p->subs[idx].f.datalen = READ_SIZE;
	/* Handle CallerID Transmission */
	if ((p->owner == ast) && p->cidspill)
		send_callerid(p);
	p->subs[idx].f.frametype = AST_FRAME_VOICE;
    p->subs[idx].f.subclass.format = ast_channel_rawreadformat(ast);
	p->subs[idx].f.samples = READ_SIZE;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
	p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET / sizeof(p->subs[idx].buffer[0]);
	if (p->dialing ||  p->radio || /* Transmitting something */
		(idx && (ast_channel_state(ast) != AST_STATE_UP)) || /* Three-way or callwait that isn't up */
		((idx == SUB_CALLWAIT) && !p->subs[SUB_CALLWAIT].inthreeway) /* Inactive and non-confed call-wait */
		)
	{
		p->subs[idx].f.frametype = AST_FRAME_NULL;
		p->subs[idx].f.subclass.integer = 0;
		p->subs[idx].f.samples = 0;
		p->subs[idx].f.mallocd = 0;
		p->subs[idx].f.offset = 0;
		p->subs[idx].f.data.ptr = NULL;
		p->subs[idx].f.datalen= 0;
	}
	if (p->dsp && (!p->ignoredtmf || p->callwaitcas || p->busydetect || p->callprogress || p->waitingfordt.tv_sec) && !idx)
	{
		/* Perform busy detection etc on the dahdi line */
		int mute;
		f = ast_dsp_process(ast, p->dsp, &p->subs[idx].f);
		/* Check if DSP code thinks we should be muting this frame and mute the conference if so */
		mute = ast_dsp_was_muted(p->dsp);
		if (p->muting != mute)
		{
			p->muting = mute;
			gcom_confmute(p, mute);
		}
		if (f)
		{
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_BUSY))
			{
				if ((ast_channel_state(ast) == AST_STATE_UP) && !p->outgoing) /* Treat this as a "hangup" instead of a "busy" on the assumption that  a busy */
					f = NULL;
			} 
			else if (f->frametype == AST_FRAME_DTMF_BEGIN || f->frametype == AST_FRAME_DTMF_END)
				p->pulsedial = 0; /* DSP clears us of being pulse */
		}
	}
	else
		f = &p->subs[idx].f;
	if (f)
	{
		switch (f->frametype)
		{
		 case AST_FRAME_DTMF_BEGIN:
		 case AST_FRAME_DTMF_END:
			gcom_handle_dtmf(ast, idx, &f);
			break;
		 case AST_FRAME_VOICE:
			if (p->cidspill || p->cid_suppress_expire)
			{
				/* We are/were sending a caller id spill.  Suppress any echo. */
				p->subs[idx].f.frametype = AST_FRAME_NULL;
				p->subs[idx].f.subclass.integer = 0;
				p->subs[idx].f.samples = 0;
				p->subs[idx].f.mallocd = 0;
				p->subs[idx].f.offset = 0;
				p->subs[idx].f.data.ptr = NULL;
				p->subs[idx].f.datalen= 0;
			}
			break;
		 default:
			break;
		}
	}
	ast_mutex_unlock(&p->lock);
	return f;
 }

static int my_gcom_write(struct gcom_pvt *p, unsigned char *buf, int len, int idx, int linear)
{
	int sent=0;
	int size;
	int res;
	int fd;
	fd = p->subs[idx].dfd;
	while (len)
	{
		size = len;
		if (size > (linear ? READ_SIZE * 2 : READ_SIZE))
			size = (linear ? READ_SIZE * 2 : READ_SIZE);
		res = write(fd, buf, size);
		if (res != size)
		{
			ast_log(LOG_DEBUG,"Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			return sent;
		}
		len -= size;
		buf += size;
	}
	return sent;
 }

 static int gcom_write(struct ast_channel *ast, struct ast_frame *frame)
 {
	struct gcom_pvt *p = ast_channel_tech_pvt(ast);
	int res;
	int idx;
	idx = gcom_get_index(ast, p, 0);
	if (idx < 0)
	{
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast_channel_name(ast));
		return -1;
	}
	if (frame->frametype != AST_FRAME_VOICE)
	{
		if (frame->frametype != AST_FRAME_IMAGE)
		 ast_log(LOG_WARNING, "Don't know what to do with frame type '%d'\n", frame->frametype);
		return 0;
	}
	if (p->dialing)
	{
		ast_log(LOG_DEBUG,"Dropping frame since I'm still dialing on %s...\n",ast_channel_name(ast));
		return 0;
	}
	if (!p->owner)
	{
		ast_log(LOG_DEBUG,"Dropping frame since there is no active owner on %s...\n",ast_channel_name(ast));
		return 0;
	}
	if (p->cidspill)
	{
		ast_log(LOG_DEBUG, "Dropping frame since I've still got a callerid spill on %s...\n",ast_channel_name(ast));
		return 0;
	}
	/* Return if it's not valid data */
	if (!frame->data.ptr || !frame->datalen)
		return 0;
    if (frame->subclass.format== ast_format_slin)
    {
		if (!p->subs[idx].linear)
		{
			p->subs[idx].linear = 1;
			res = gcom_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
		res = my_gcom_write(p, (unsigned char *)frame->data.ptr, frame->datalen, idx, 1);
	}
    else
    {
		/* x-law already */
		if (p->subs[idx].linear)
		{
			p->subs[idx].linear = 0;
			res = gcom_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set companded mode on channel %d\n", p->channel);
		}
		res = my_gcom_write(p, (unsigned char *)frame->data.ptr, frame->datalen, idx, 0);
	}
	if (res < 0)
	{
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
 }

 static int gcom_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen)
 {
	struct gcom_pvt *p = ast_channel_tech_pvt(chan);
	int res=-1;
	int idx;
	ast_mutex_lock(&p->lock);
	ast_log(LOG_DEBUG,"Requested indication %d on channel %s\n", condition, ast_channel_name(chan));
	idx = gcom_get_index(chan, p, 0);
	if (idx == SUB_REAL)
	{
		switch (condition)
		{
		 case AST_CONTROL_BUSY:
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_BUSY);
			break;
	 	 case AST_CONTROL_RINGING:
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_RINGTONE);
			if (ast_channel_state(chan) != AST_STATE_UP)
			{
				if ((ast_channel_state(chan) != AST_STATE_RING) )
				ast_setstate(chan, AST_STATE_RINGING);
			}
			break;
		 case AST_CONTROL_PROCEEDING:
			ast_log(LOG_DEBUG,"Received AST_CONTROL_PROCEEDING on %s\n",ast_channel_name(chan));
			res = 0;
			break;
 	 	 case AST_CONTROL_PROGRESS:
 	 		ast_log(LOG_DEBUG,"Received AST_CONTROL_PROGRESS on %s\n",ast_channel_name(chan));
			res = 0;
			break;
		 case AST_CONTROL_CONGESTION:
			/* There are many cause codes that generate an AST_CONTROL_CONGESTION. */
			switch (ast_channel_hangupcause(chan))
			{
			 case AST_CAUSE_USER_BUSY:
			 case AST_CAUSE_NORMAL_CLEARING:
			 case 0:/* Cause has not been set. */
				/* Supply a more appropriate cause. */
				ast_channel_hangupcause_set(chan,AST_CAUSE_CONGESTION);
				break;
			 default:
				break;
			}
			break;
		 case AST_CONTROL_HOLD:
			ast_moh_start(chan, data, p->mohinterpret);
			break;
		 case AST_CONTROL_UNHOLD:
			ast_moh_stop(chan);
			break;
		 case AST_CONTROL_RADIO_KEY:
			if (p->radio)
				res = gcom_set_hook(p->subs[idx].dfd, DAHDI_OFFHOOK);
			res = 0;
			break;
		 case AST_CONTROL_RADIO_UNKEY:
			if (p->radio)
				res = gcom_set_hook(p->subs[idx].dfd, DAHDI_RINGOFF);
			res = 0;
			break;
		 case AST_CONTROL_FLASH:
			break;
		 case AST_CONTROL_SRCUPDATE:
			res = 0;
			break;
		 case -1:
			res = tone_zone_play_tone(p->subs[idx].dfd, -1);
			break;
		}
	}
	else
		res = 0;
	ast_mutex_unlock(&p->lock);
	return res;
 }


 static struct ast_str *create_channel_name(struct gcom_pvt *i)
 {
	struct ast_str *chan_name;
	int x, y;
	if (!(chan_name = ast_str_create(32)))
		return NULL;
	if (i->channel == CHAN_PSEUDO)
		ast_str_set(&chan_name, 0, "pseudo-%ld", ast_random());
	else
	{
		y = 1;
		do
		{
			ast_str_set(&chan_name, 0, "%d-%d", i->channel, y);
			for (x = 0; x < 3; ++x)
			{
				if (i->subs[x].owner && !strcasecmp(ast_str_buffer(chan_name),ast_channel_name(i->subs[x].owner) + 6))
					break;
			}
			++y;
		} while (x < 3);
	}
	return chan_name;
 }



static struct ast_channel *gcom_new(struct gcom_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, struct ast_callid *callid)
{

 	ast_log(LOG_DEBUG,"gcom_new state=%d linkedid=%s idx=%d\n",state,linkedid,idx);
	struct ast_channel *tmp;
	struct ast_format *deflaw;
	int x;
	int features;
	struct ast_str *chan_name;
	struct ast_variable *v;
	char *dashptr;
	char device_name[AST_CHANNEL_NAME];
    struct ast_format_cap *caps;
 	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (i->subs[idx].owner)
	{
		ast_log(LOG_ERROR, "Channel %d already has a %s call\n", i->channel,subnames[idx]);
		return NULL;
	}
	chan_name = create_channel_name(i);
	if (!chan_name)
		return NULL;
    tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, assignedids, requestor, i->amaflags, "GCOM/%s", ast_str_buffer(chan_name));
    ast_channel_stage_snapshot(tmp);
	if (callid)
		ast_channel_callid_set(tmp, callid);
	ast_free(chan_name);
	if (!tmp)
		return NULL;
	ast_channel_tech_set(tmp,&gcom_tech);
	ast_channel_cc_params_init(tmp, i->cc_params);
	if (law)
	{
		i->law = law;
		if (law == DAHDI_LAW_ALAW)
			deflaw = ast_format_alaw;
		else
			deflaw = ast_format_ulaw;
	}
	else
	{
		i->law = i->law_default;
		if (i->law_default == DAHDI_LAW_ALAW)
			deflaw = ast_format_alaw;
		else
			deflaw = ast_format_ulaw;
	}
	ast_channel_set_fd(tmp, 0, i->subs[idx].dfd);
	ast_format_cap_append(caps, deflaw, 0);
    ast_channel_nativeformats_set(tmp, caps);
	ao2_ref(caps, -1);
    ast_channel_set_rawreadformat(tmp, deflaw);
	ast_channel_set_readformat(tmp, deflaw);
    ast_channel_set_rawwriteformat(tmp, deflaw);
    ast_channel_set_writeformat(tmp, deflaw);
	i->subs[idx].linear = 0;
	gcom_setlinear(i->subs[idx].dfd, i->subs[idx].linear);
	features = 0;
	if (idx == SUB_REAL)
	{
		if (i->busydetect && CANBUSYDETECT(i))
			features |= DSP_FEATURE_BUSY_DETECT;
		if ((i->callprogress & CALLPROGRESS_PROGRESS) && CANPROGRESSDETECT(i))
			features |= DSP_FEATURE_CALL_PROGRESS;
		if ((!i->outgoing && (i->callprogress & CALLPROGRESS_FAX_INCOMING)) ||
			(i->outgoing && (i->callprogress & CALLPROGRESS_FAX_OUTGOING)))
			features |= DSP_FEATURE_FAX_DETECT;
		x = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;
		if (ioctl(i->subs[idx].dfd, DAHDI_TONEDETECT, &x))
		{
			i->hardwaredtmf = 0;
			features |= DSP_FEATURE_DIGIT_DETECT;
		} 
	}
	if (features)
	{
		if (i->dsp)
			ast_log(LOG_DEBUG,"Already have a dsp on %s?\n", ast_channel_name(tmp));
		else
		{
			if (i->channel != CHAN_PSEUDO)
				i->dsp = ast_dsp_new();
			else
				i->dsp = NULL;
			if (i->dsp)
			{
				i->dsp_features = features;
				ast_dsp_set_features(i->dsp, features);
				ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
				if (!ast_strlen_zero(progzone))
					ast_dsp_set_call_progress_zone(i->dsp, progzone);
				if (i->busydetect && CANBUSYDETECT(i))
				{
					ast_dsp_set_busy_count(i->dsp, i->busycount);
					ast_dsp_set_busy_pattern(i->dsp, &i->busy_cadence);
				}
			}
		}
	}
	if (state == AST_STATE_RING)
		ast_channel_rings_set(tmp,1);
	ast_channel_tech_pvt_set(tmp, i);
	if (!ast_strlen_zero(i->parkinglot))
		ast_channel_parkinglot_set(tmp, i->parkinglot);
	if (!ast_strlen_zero(i->language))
		ast_channel_language_set(tmp, i->language);
	if (!i->owner)
		i->owner = tmp;
	if (!ast_strlen_zero(i->accountcode))
		ast_channel_accountcode_set(tmp, i->accountcode);
	if (i->amaflags)
        ast_channel_amaflags_set(tmp,i->amaflags);
	i->subs[idx].owner = tmp;
	ast_channel_context_set(tmp, i->context);
	if (!i->adsi)
         ast_channel_adsicpe_set(tmp,AST_ADSI_UNAVAILABLE);
	if (!ast_strlen_zero(i->exten))
		ast_channel_exten_set(tmp, i->exten);
	if (!ast_strlen_zero(i->rdnis))
	{
		ast_channel_redirecting(tmp)->from.number.valid = 1;
		ast_channel_redirecting(tmp)->from.number.str = ast_strdup(i->rdnis);
	}
	if (!ast_strlen_zero(i->dnid))
		ast_channel_dialed(tmp)->number.str = ast_strdup(i->dnid);
	if (!ast_strlen_zero(i->cid_ani))
	{
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_ani);
	}
    if (!ast_strlen_zero(i->cid_num))
    {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}
	ast_channel_caller(tmp)->id.name.presentation = i->callingpres;
	ast_channel_caller(tmp)->id.number.presentation = i->callingpres;
	ast_channel_caller(tmp)->ani2 = i->cid_ani2;
	ast_channel_caller(tmp)->id.tag = ast_strdup(i->cid_tag);
	i->fake_event = 0;
	gcom_confmute(i, 0);
	i->muting = 0;
	ast_jb_configure(tmp, &global_jbconf);
	for (v = i->vars ; v ; v = v->next)
		pbx_builtin_setvar_helper(tmp, v->name, v->value);
	ast_channel_stage_snapshot_done(tmp);
	ast_channel_unlock(tmp);
	ast_module_ref(ast_module_info->self);
	if (startpbx)
	{
		if (ast_pbx_start(tmp))
		{
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
			ast_hangup(tmp);
			return NULL;
		}
	}
	return tmp;
 }



/*! \brief enable or disable the chan_dahdi Do-Not-Disturb mode for a DAHDI channel
 * \param gcomchan "Physical" DAHDI channel (e.g: DAHDI/5)
 * \param flag on 1 to enable, 0 to disable, -1 return dnd value
 *
 * chan_dahdi has a DND (Do Not Disturb) mode for each gcomchan (physical
 * DAHDI channel). Use this to enable or disable it.
 *
 * \bug the use of the word "channel" for those dahdichans is really confusing.
 */
 static int gcom_dnd(struct gcom_pvt *gcomchan, int flag)
 {
	if (flag == -1)
		return gcomchan->dnd;
	gcomchan->dnd = flag;
	ast_log(LOG_DEBUG,"%s DND on channel %d\n",flag? "Enabled" : "Disabled",gcomchan->channel);
	manager_event(EVENT_FLAG_SYSTEM, "DNDState",
			"Channel: DAHDI/%d\r\n"
			"Status: %s\r\n", gcomchan->channel,
			flag? "enabled" : "disabled");
	return 0;
 }


 static int gcom_destroy_channel_bynum(int channel)
 {
	struct gcom_pvt *cur;
	ast_mutex_lock(&iflock);
	for (cur = iflist; cur; cur = cur->next)
	{
		if (cur->channel == channel)
		{
			int x = DAHDI_FLASH;
			/* important to create an event for gcom_wait_event to register so that all analog_ss_threads terminate */
			ioctl(cur->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
			destroy_channel(cur, 1);
			ast_mutex_unlock(&iflock);
			ast_module_unref(ast_module_info->self);
			return RESULT_SUCCESS;
		}
	}
	ast_mutex_unlock(&iflock);
	return RESULT_FAILURE;
 }

static int gsm_resolve_span(int *span, int channel, int offset, struct dahdi_spaninfo *si)
{
	int x;
	int trunkgroup;
	/* Get appropriate trunk group if there is one */
	trunkgroup = gsms[*span].mastertrunkgroup;
	if (trunkgroup)
	{
		/* Select a specific trunk group */
		for (x = 0; x < NUM_SPANS; x++)
		{
			if (gsms[x].gsm.trunkgroup == trunkgroup)
			{
				*span = x;
				return 0;
			}
		}
		ast_log(LOG_WARNING, "Channel %d on span %d configured to use nonexistent trunk group %d\n", channel, *span, trunkgroup);
		*span = -1;
	}
	else
	{
		if (gsms[*span].gsm.trunkgroup)
		{
			ast_log(LOG_WARNING, "Unable to use span %d implicitly since it is trunk group %d (please use spanmap)\n", *span, gsms[*span].gsm.trunkgroup);
			*span = -1;
		}
		else if (gsms[*span].mastertrunkgroup)
		{
			ast_log(LOG_WARNING, "Unable to use span %d implicitly since it is already part of trunk group %d\n", *span, gsms[*span].mastertrunkgroup);
			*span = -1;
		}
		else
		{
			if (si->totalchans == 2)
				gsms[*span].dchannel = 2 + offset;//offset is 0
			else
			{
				ast_log(LOG_WARNING, "Unable to use span %d, since the D-channel cannot be located (unexpected span size of %d channels)\n", *span, si->totalchans);
				*span = -1;
				return 0;
			}
			gsms[*span].gsm.span_id= *span + 1;
		}
	}
	return 0;
 }

 static int sigtype_to_signalling(int sigtype)
 {
	return sigtype;
 }

/*!
 * \internal
 * \brief Get file name and channel number from (subdir,number)
 *
 * \param subdir name of the subdirectory under /dev/dahdi/
 * \param channel name of device file under /dev/dahdi/<subdir>/
 * \param path buffer to put file name in
 * \param pathlen maximal length of path
 *
 * \retval minor number of dahdi channel.
 * \retval -errno on error.
 */
 static int device2chan(const char *subdir, int channel, char *path, int pathlen)
 {
	struct stat	stbuf;
	int		num;
	snprintf(path, pathlen, "/dev/dahdi/%s/%d", subdir, channel);
	if (stat(path, &stbuf) < 0)
	{
		ast_log(LOG_ERROR, "stat(%s) failed: %s\n", path, strerror(errno));
		return -errno;
	}
	if (!S_ISCHR(stbuf.st_mode))
	{
		ast_log(LOG_ERROR, "%s: Not a character device file\n", path);
		return -EINVAL;
	}
	num = minor(stbuf.st_rdev);
	ast_log(LOG_DEBUG, "%s -> %d\n", path, num);
	return num;
}

/*!
 * \internal
 * \brief Initialize/create a channel interface.
 *
 * \param channel Channel interface number to initialize/create.
 * \param conf Configuration parameters to initialize interface with.
 * \param reloading What we are doing now:
 * 0 - initial module load,
 * 1 - module reload,
 * 2 - module restart
 *
 * \retval Interface-pointer initialized/created
 * \retval NULL if error
 */
 static struct gcom_pvt *mkintf(int channel, const struct gcom_chan_conf *conf, int reloading)
 {
	struct gcom_pvt *tmp;/*!< Current channel structure initializing */
	char fn[80];
	struct dahdi_bufferinfo bi;
	int res;
	int span = 0;
	int here = 0;/*!< TRUE if the channel interface already exists. */
	int x;
	struct dahdi_spaninfo si;
	struct sig_gsm_chan *gsm_chan = NULL;
	struct dahdi_params p;
	/* Search channel interface list to see if it already exists. */
	for (tmp = iflist; tmp; tmp = tmp->next)
	{
		if (!tmp->destroy) {
			if (tmp->channel == channel)
			{
				/* The channel interface already exists. */
				here = 1;
				break;
			}
			if (tmp->channel > channel)
			{
				/* No way it can be in the sorted list. */
				tmp = NULL;
				break;
			}
		}
	}
	if (!here && reloading != 1)
	{
		tmp = ast_calloc(1, sizeof(*tmp));
		if (!tmp)
			return NULL;
		tmp->cc_params = ast_cc_config_params_init();
		if (!tmp->cc_params)
		{
			ast_free(tmp);
			return NULL;
		}
		ast_mutex_init(&tmp->lock);
		ifcount++;
		for (x = 0; x < 3; x++)
			tmp->subs[x].dfd = -1;
		tmp->channel = channel;
		tmp->priindication_oob = conf->chan.priindication_oob;
	}
	if (tmp)
	{
		int chan_sig = conf->chan.sig;
		/* If there are variables in tmp before it is updated to match the new config, clear them */
		if (reloading && tmp->vars)
		{
			ast_variables_destroy(tmp->vars);
			tmp->vars = NULL;
		}
		if (!here)
		{
			/* Can only get here if this is a new channel interface being created. */
			if ((channel != CHAN_PSEUDO))
			{
				int count = 0;
				snprintf(fn, sizeof(fn), "%d", channel);
				/* Open non-blocking */
				tmp->subs[SUB_REAL].dfd = gcom_open(fn);//open voice chan for gcom chan
				while (tmp->subs[SUB_REAL].dfd < 0 && reloading == 2 && count < 1000)
				{ /* the kernel may not call dahdi_release fast enough for the open flagbit to be cleared in time */
					usleep(1);
					tmp->subs[SUB_REAL].dfd = gcom_open(fn);
					count++;
				}
				/* Allocate a DAHDI structure */
				if (tmp->subs[SUB_REAL].dfd < 0)
				{
					ast_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
					destroy_gcom_pvt(tmp);
					return NULL;
				}
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
				if (res < 0)
				{
					ast_log(LOG_ERROR, "Unable to get parameters: %s\n", strerror(errno));
					destroy_gcom_pvt(tmp);
					return NULL;
				}
				if (conf->is_sig_auto)
					chan_sig = sigtype_to_signalling(p.sigtype);
				if (p.sigtype != (chan_sig & 0x3ffff))
				{
					ast_log(LOG_ERROR, "Signalling requested on channel %d is %s but line is in %s signalling\n", channel, sig2str(chan_sig), sig2str(p.sigtype));
					destroy_gcom_pvt(tmp);
					return NULL;
				}
				tmp->law_default = p.curlaw;
				tmp->law = p.curlaw;
				tmp->span = p.spanno;
				span = p.spanno - 1;
			}
			else
				chan_sig = 0;
			tmp->sig = chan_sig;
			tmp->outsigmod = conf->chan.outsigmod;
			if (chan_sig == SIG_GSM)
			{
				int offset;
				int matchesdchan;
				int x;
				int myswitchtype = 0;
				offset = 0;
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &offset))
				{
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_gcom_pvt(tmp);
					return NULL;
				}
				if (span >= NUM_SPANS)
				{
					ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
					destroy_gcom_pvt(tmp);
					return NULL;
				}
				else
				{
					si.spanno = 0;
					if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1)
					{
						ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
						destroy_gcom_pvt(tmp);
						return NULL;
					}
					tmp->logicalspan = gsms[span].gsmlogicalspan;//channel num in span
					gsm_resolve_span(&span, channel, (channel - p.chanpos), &si);
                    ast_log(LOG_WARNING, "gsm resolve span channel=%d pos=%d\n",channel,p.chanpos);
					if (span < 0)
					{
						ast_log(LOG_WARNING, "Channel %d: Unable to find locate channel/trunk group!\n", channel);
						destroy_gcom_pvt(tmp);
						return NULL;
					}
					myswitchtype = conf->gsm.gsm.conf.switchtype;
					/* Make sure this isn't a d-channel */
					matchesdchan=0;
					for (x = 0; x < NUM_SPANS; x++) 
                    {
						if (gsms[x].dchannel == tmp->channel)
                        {
							matchesdchan = 1;
							break;//
						}
					}
					if (!matchesdchan)
					{
						if (gsms[span].gsm.conf.nodetype && (gsms[span].gsm.conf.nodetype != conf->gsm.gsm.conf.nodetype))
						{
							ast_log(LOG_ERROR, "Span %d is already a %s node\n", span + 1, gsm_node2str(gsms[span].gsm.conf.nodetype));
							destroy_gcom_pvt(tmp);
							return NULL;
						}
						if (gsms[span].gsm.conf.switchtype && (gsms[span].gsm.conf.switchtype != myswitchtype))
						{
							ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, gsm_switch2str(gsms[span].gsm.conf.switchtype));
							destroy_gcom_pvt(tmp);
							return NULL;
						}
						gsm_chan = sig_gsm_chan_new(tmp, &gcom_gsm_callbacks, &gsms[span].gsm, tmp->logicalspan, p.chanpos, gsms[span].mastertrunkgroup);
						if (!gsm_chan) {
							destroy_gcom_pvt(tmp);
							return NULL;
						}
						tmp->sig_pvt = gsm_chan;
						tmp->gsm = &gsms[span].gsm;
						tmp->priexclusive = conf->chan.priexclusive;
						gsms[span].gsm.sig = chan_sig; //signnal
						gsms[span].gsm.conf.nodetype =  conf->gsm.gsm.conf.nodetype;
						gsms[span].gsm.conf.switchtype = myswitchtype;
                        gsms[span].gsm.pvts = tmp->sig_pvt;
		                gsms[span].gsm.numchans++;
						gsms[span].gsm.transfer = conf->chan.transfer;
                        gsms[span].gsm.resetinterval = conf->gsm.gsm.resetinterval;
                        ast_copy_string(gsms[span].gsm.conf.pinnum, conf->gsm.gsm.conf.pinnum, sizeof(gsms[span].gsm.conf.pinnum));
						ast_copy_string(gsms[span].gsm.conf.countrycode, conf->gsm.gsm.conf.countrycode, sizeof(gsms[span].gsm.conf.countrycode));
						ast_copy_string(gsms[span].gsm.conf.numtype, conf->gsm.gsm.conf.numtype, sizeof(gsms[span].gsm.conf.numtype));
						ast_copy_string(gsms[span].gsm.conf.sms_lang, conf->gsm.gsm.conf.sms_lang,sizeof(gsms[span].gsm.conf.sms_lang) );
					}
					else
					{
						ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", p.chanpos);
						destroy_gcom_pvt(tmp);
						return NULL;
					}
				}
			}
		}
		else
		{
			chan_sig = tmp->sig;
			if (tmp->subs[SUB_REAL].dfd > -1)
			{
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
			}
		}
		if (tmp->radio)
		{
			p.channo = channel;
			p.rxwinktime = 1;
			p.rxflashtime = 1;
			p.starttime = 1;
			p.debouncetime = 5;
		}
		else
			p.channo = channel;
		/* don't set parms on a pseudo-channel */
		if (tmp->subs[SUB_REAL].dfd >= 0)
		{
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_PARAMS, &p);
			if (res < 0)
			{
				ast_log(LOG_ERROR, "Unable to set parameters: %s\n", strerror(errno));
				destroy_gcom_pvt(tmp);
				return NULL;
			}
		}
		if (!here && (tmp->subs[SUB_REAL].dfd > -1))
		{
			memset(&bi, 0, sizeof(bi));
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
			if (!res)
			{
				bi.txbufpolicy = conf->chan.buf_policy;
				bi.rxbufpolicy = conf->chan.buf_policy;
				bi.numbufs = conf->chan.buf_no;
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
				if (res < 0)
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d: %s\n", channel, strerror(errno));
			}
			else
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d: %s\n", channel, strerror(errno));
			tmp->buf_policy = conf->chan.buf_policy;
			tmp->buf_no = conf->chan.buf_no;
			tmp->usefaxbuffers = conf->chan.usefaxbuffers;
			tmp->faxbuf_policy = conf->chan.faxbuf_policy;
			tmp->faxbuf_no = conf->chan.faxbuf_no;
			/* This is not as gnarly as it may first appear.  If the ioctl above failed, we'd be setting
			 * tmp->bufsize to zero which would cause subsequent faxbuffer-related ioctl calls to fail.
			 * The reason the ioctl call above failed should to be determined before worrying about the
			 * faxbuffer-related ioctl calls */
			tmp->bufsize = bi.bufsize;
		}
		tmp->immediate = conf->chan.immediate;
		tmp->transfertobusy = conf->chan.transfertobusy;
		tmp->firstradio = 0;
		if ((chan_sig == SIG_GSM) )
			tmp->permcallwaiting = conf->chan.callwaiting;
		else
			tmp->permcallwaiting = 0;
		/* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
		tmp->destroy = 0;
		tmp->callwaitingcallerid = conf->chan.callwaitingcallerid;
		tmp->threewaycalling = conf->chan.threewaycalling;
		tmp->adsi = conf->chan.adsi;
		tmp->permhidecallerid = conf->chan.hidecallerid;
		tmp->hidecalleridname = conf->chan.hidecalleridname;
		tmp->callreturn = conf->chan.callreturn;
		tmp->echocancel = conf->chan.echocancel;
		tmp->echotraining = conf->chan.echotraining;
		tmp->pulse = conf->chan.pulse;
		if (tmp->echocancel.head.tap_length)
			tmp->echocanbridged = conf->chan.echocanbridged;
		else
		{
			if (conf->chan.echocanbridged)
				ast_log(LOG_NOTICE, "echocancelwhenbridged requires echocancel to be enabled; ignoring\n");
			tmp->echocanbridged = 0;
		}
		tmp->busydetect = conf->chan.busydetect;
		tmp->busycount = conf->chan.busycount;
        tmp->busy_cadence = conf->chan.busy_cadence;
		tmp->callprogress = conf->chan.callprogress;
		tmp->cancallforward = conf->chan.cancallforward;
		tmp->dtmfrelax = conf->chan.dtmfrelax;
		tmp->callwaiting = tmp->permcallwaiting;
		tmp->hidecallerid = tmp->permhidecallerid;
		tmp->channel = channel;
		tmp->stripmsd = conf->chan.stripmsd;
		tmp->use_callerid = conf->chan.use_callerid;
		tmp->restrictcid = conf->chan.restrictcid;
		tmp->use_callingpres = conf->chan.use_callingpres;
		ast_copy_string(tmp->accountcode, conf->chan.accountcode, sizeof(tmp->accountcode));
		tmp->amaflags = conf->chan.amaflags;
		if (!here)
		{
			tmp->confno = -1;
			tmp->propconfno = -1;
		}
		tmp->canpark = conf->chan.canpark;
		tmp->transfer = conf->chan.transfer;
		ast_copy_string(tmp->defcontext,conf->chan.context,sizeof(tmp->defcontext));
		ast_copy_string(tmp->language, conf->chan.language, sizeof(tmp->language));
		ast_copy_string(tmp->mohinterpret, conf->chan.mohinterpret, sizeof(tmp->mohinterpret));
		ast_copy_string(tmp->mohsuggest, conf->chan.mohsuggest, sizeof(tmp->mohsuggest));
		ast_copy_string(tmp->context, conf->chan.context, sizeof(tmp->context));
		ast_copy_string(tmp->parkinglot, conf->chan.parkinglot, sizeof(tmp->parkinglot));
		tmp->cid_num[0] = '\0';
        tmp->cid_name[0] = '\0';
		ast_copy_string(tmp->cid_tag, conf->chan.cid_tag, sizeof(tmp->cid_tag));
		tmp->cid_subaddr[0] = '\0';
		tmp->group = conf->chan.group;
		if (conf->chan.vars)
		{
			struct ast_variable *v, *tmpvar;
	        for (v = conf->chan.vars ; v ; v = v->next)
	        {
        	   if ((tmpvar = ast_variable_new(v->name, v->value, v->file)))
        	   {
                  tmpvar->next = tmp->vars;
                  tmp->vars = tmpvar;
               }
            }
		}
		tmp->cid_rxgain = conf->chan.cid_rxgain;
		tmp->rxgain = conf->chan.rxgain;
		tmp->txgain = conf->chan.txgain;
		tmp->txdrc = conf->chan.txdrc;
		tmp->rxdrc = conf->chan.rxdrc;
		tmp->tonezone = conf->chan.tonezone;
		if (tmp->subs[SUB_REAL].dfd > -1)
		{
			set_actual_gain(tmp->subs[SUB_REAL].dfd, tmp->rxgain, tmp->txgain, tmp->rxdrc, tmp->txdrc, tmp->law);
			if (tmp->dsp)
				ast_dsp_set_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
			update_conf(tmp);
			ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SETTONEZONE,&tmp->tonezone);
			if ((res = get_alarms(tmp)) != DAHDI_ALARM_NONE)
			{
				tmp->inalarm = 1;
				handle_alarms(tmp, res);
			}
		}
		tmp->polarityonanswerdelay = conf->chan.polarityonanswerdelay;
		tmp->answeronpolarityswitch = conf->chan.answeronpolarityswitch;
		tmp->hanguponpolarityswitch = conf->chan.hanguponpolarityswitch;
		ast_cc_copy_config_params(tmp->cc_params, conf->chan.cc_params);
		if (!here)
			tmp->inservice = 1;
        switch (tmp->sig)
        {
          case SIG_GSM:
             if (gsm_chan)
             {
				gsm_chan->channel = tmp->channel;
				gsm_chan->hidecallerid = tmp->hidecallerid;
				gsm_chan->hidecalleridname = tmp->hidecalleridname;
				gsm_chan->immediate = tmp->immediate;
				gsm_chan->inalarm = tmp->inalarm;
				gsm_chan->priexclusive = tmp->priexclusive;
				gsm_chan->priindication_oob = tmp->priindication_oob;
				gsm_chan->use_callerid = tmp->use_callerid;
				gsm_chan->use_callingpres = tmp->use_callingpres;
				ast_copy_string(gsm_chan->context, tmp->context,
					sizeof(gsm_chan->context));
				ast_copy_string(gsm_chan->mohinterpret, tmp->mohinterpret,
					sizeof(gsm_chan->mohinterpret));
				gsm_chan->stripmsd = tmp->stripmsd;
			 }
             break;
          default:
             break;
        }
		if (tmp->channel == CHAN_PSEUDO) // Save off pseudo channel buffer policy values for dynamic creation of no B channel interfaces.
		{
			gcom_pseudo_parms.buf_no = tmp->buf_no;
			gcom_pseudo_parms.buf_policy = tmp->buf_policy;
			gcom_pseudo_parms.faxbuf_no = tmp->faxbuf_no;
			gcom_pseudo_parms.faxbuf_policy = tmp->faxbuf_policy;
		}
	}
	if (tmp && !here)
		gcom_iflist_insert(tmp); /* Add the new channel interface to the sorted channel interface list. */
	return tmp;
 }

 static int is_group_or_channel_match(struct gcom_pvt *p, int span, ast_group_t groupmatch, int *groupmatched, int channelmatch, int *channelmatched)
 {
	if (groupmatch)
	{
		if ((p->group & groupmatch) != groupmatch) /* Doesn't match the specified group, try the next one */
			return 0;
		*groupmatched = 1;
	}
	/* Check to see if we have a channel match */
	if (channelmatch != -1)
	{
		if (p->channel != channelmatch) /* Doesn't match the specified channel, try the next one */
			return 0;
		*channelmatched = 1;
	}
	return 1;
 }

 static int available(struct gcom_pvt **pvt, int is_specific_channel)
 {
	struct gcom_pvt *p = *pvt;
	if (p->inalarm)
		return 0;
	if (!p->owner)
		return 1;
	return 0;
 }

/* This function can *ONLY* be used for copying pseudo (CHAN_PSEUDO) private
   structures; it makes no attempt to safely copy regular channel private
   structures that might contain reference-counted object pointers and other
   scary bits
*/
static struct gcom_pvt *duplicate_pseudo(struct gcom_pvt *src)
{
	struct gcom_pvt *p;
	struct dahdi_bufferinfo bi;
	int res;
	p = ast_malloc(sizeof(*p));
	if (!p)
		return NULL;
	*p = *src;
	/* Must deep copy the cc_params. */
	p->cc_params = ast_cc_config_params_init();
	if (!p->cc_params)
	{
		ast_free(p);
		return NULL;
	}
	ast_cc_copy_config_params(p->cc_params, src->cc_params);
	p->which_iflist = GCOM_IFLIST_NONE;
	p->next = NULL;
	p->prev = NULL;
	ast_mutex_init(&p->lock);
	p->subs[SUB_REAL].dfd = gcom_open("/dev/dahdi/pseudo");
	if (p->subs[SUB_REAL].dfd < 0)
	{
		ast_log(LOG_ERROR, "Unable to dup channel: %s\n", strerror(errno));
		destroy_gcom_pvt(p);
		return NULL;
	}
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
	if (!res)
	{
		bi.txbufpolicy = src->buf_policy;
		bi.rxbufpolicy = src->buf_policy;
		bi.numbufs = src->buf_no;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
		if (res < 0)
			ast_log(LOG_WARNING, "Unable to set buffer policy on dup channel: %s\n", strerror(errno));
	}
	else
		ast_log(LOG_WARNING, "Unable to check buffer policy on dup channel: %s\n", strerror(errno));
	p->destroy = 1;
	gcom_iflist_insert(p);
	return p;
 }

 struct gcom_starting_point
 {
	ast_group_t groupmatch; /*! Group matching mask.  Zero if not specified. */
	int channelmatch; /*! DAHDI channel to match with.  -1 if not specified. */
	int rr_starting_point; /*! Round robin saved search location index. (Valid if roundrobin TRUE) */
	int span; /*! ISDN span where channels can be picked (Zero if not specified) */
	int cadance; /*! Analog channel distinctive ring cadance index. */
	char opt; /*! Dialing option. c/r/d if present and valid. */
	char backwards; /*! TRUE if to search the channel list backwards. */
	char roundrobin; /*! TRUE if search is done with round robin sequence. */
 };

 static struct gcom_pvt *determine_starting_point(const char *data, struct gcom_starting_point *param)
 {
	char *dest;
	char *s;
	int x;
	int res = 0;
	struct gcom_pvt *p;
	char *subdir = NULL;
	AST_DECLARE_APP_ARGS(args,
	   AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);
	/*
	 * data is ---v
	 * Dial(DAHDI/pseudo[/extension[/options]])
	 * Dial(DAHDI/<channel#>[c|r<cadance#>|d][/extension[/options]])
	 * Dial(DAHDI/<subdir>!<channel#>[c|r<cadance#>|d][/extension[/options]])
	 * Dial(DAHDI/i<span>[/extension[/options]])
	 * Dial(DAHDI/[i<span>-](g|G|r|R)<group#(0-63)>[c|r<cadance#>|d][/extension[/options]])
	 *
	 * i - ISDN span channel restriction.
	 *     Used by CC to ensure that the CC recall goes out the same span.
	 *     Also to make ISDN channel names dialable when the sequence number
	 *     is stripped off.  (Used by DTMF attended transfer feature.)
	 *
	 * g - channel group allocation search forward
	 * G - channel group allocation search backward
	 * r - channel group allocation round robin search forward
	 * R - channel group allocation round robin search backward
	 *
	 * c - Wait for DTMF digit to confirm answer
	 * r<cadance#> - Set distintive ring cadance number
	 * d - Force bearer capability for ISDN/SS7 call to digital.
	 */
	if (data)
		dest = ast_strdupa(data);
	else
	{
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	AST_NONSTANDARD_APP_ARGS(args, dest, '/');
	if (!args.argc || ast_strlen_zero(args.group))
	{
		ast_log(LOG_WARNING, "No channel/group specified\n");
		return NULL;
	}
	/* Initialize the output parameters */
	memset(param, 0, sizeof(*param));
	param->channelmatch = -1;
	if (strchr(args.group, '!') != NULL)
	{
		char *prev = args.group;
		while ((s = strchr(prev, '!')) != NULL)
		{
			*s++ = '/';
			prev = s;
		}
		*(prev - 1) = '\0';
		subdir = args.group;
		args.group = prev;
	}
	else if (args.group[0] == 'i')
	{
		/* Extract the ISDN span channel restriction specifier. */
		res = sscanf(args.group + 1, "%30d", &x);
		if (res < 1)
		{
			ast_log(LOG_WARNING, "Unable to determine ISDN span for data %s\n", data);
			return NULL;
		}
		param->span = x;
		/* Remove the ISDN span channel restriction specifier. */
		s = strchr(args.group, '-');
		if (!s)
			return iflist; /* Search all groups since we are ISDN span restricted. */
		args.group = s + 1;
		res = 0;
	}
	if (toupper(args.group[0]) == 'G' || toupper(args.group[0])=='R')
	{
		/* Retrieve the group number */
		s = args.group + 1;
		res = sscanf(s, "%30d%1c%30d", &x, &param->opt, &param->cadance);
		if (res < 1)
		{
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", data);
			return NULL;
		}
		param->groupmatch = ((ast_group_t) 1 << x);
		if (toupper(args.group[0]) == 'G')
		{
			if (args.group[0] == 'G')
			{
				param->backwards = 1;
				p = ifend;
			}
			else
				p = iflist;
		}
		else
		{
			if (ARRAY_LEN(round_robin) <= x)
			{
				ast_log(LOG_WARNING, "Round robin index %d out of range for data %s\n",x, data);
				return NULL;
			}
			if (args.group[0] == 'R')
			{
				param->backwards = 1;
				p = round_robin[x] ? round_robin[x]->prev : ifend;
				if (!p)
					p = ifend;
			}
			else
			{
				p = round_robin[x] ? round_robin[x]->next : iflist;
				if (!p)
					p = iflist;
			}
			param->roundrobin = 1;
			param->rr_starting_point = x;
		}
	}
	else
	{
		s = args.group;
		if (!strcasecmp(s, "pseudo"))
		{
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
			param->channelmatch = x;
		}
		else
		{
			res = sscanf(s, "%30d%1c%30d", &x, &param->opt, &param->cadance);
			if (res < 1)
			{
				ast_log(LOG_WARNING, "Unable to determine channel for data %s\n", data);
				return NULL;
			}
			else
				param->channelmatch = x;
		}
		if (subdir)
		{
			char path[PATH_MAX];
			struct stat stbuf;
			snprintf(path, sizeof(path), "/dev/dahdi/%s/%d",subdir, param->channelmatch);
			if (stat(path, &stbuf) < 0)
			{
				ast_log(LOG_WARNING, "stat(%s) failed: %s\n",path, strerror(errno));
				return NULL;
			}
			if (!S_ISCHR(stbuf.st_mode))
			{
				ast_log(LOG_ERROR, "%s: Not a character device file\n",path);
				return NULL;
			}
			param->channelmatch = minor(stbuf.st_rdev);
		}
		p = iflist;
	}
	if (param->opt == 'r' && res < 3)
	{
		ast_log(LOG_WARNING, "Distinctive ring missing identifier in '%s'\n", data);
		param->opt = '\0';
	}
	return p;
 }

static struct ast_channel *gcom_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
    ast_log(LOG_DEBUG,"gcom_request\n");
	int callwait = 0;
	struct gcom_pvt *p;
	struct ast_channel *tmp = NULL;
	struct gcom_pvt *exitpvt;
	int channelmatched = 0;
	int groupmatched = 0;
	int transcapdigital = 0;
	struct gcom_starting_point start;
	ast_mutex_lock(&iflock);
	p = determine_starting_point(data, &start);
	if (!p)
	{
		/* We couldn't determine a starting point, which likely means badly-formatted channel name. Abort! */
		ast_mutex_unlock(&iflock);
		if (cause) *cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
		return NULL;
	}
	/* Search for an unowned channel */
	exitpvt = p;
	while (p && !tmp)
	{
		if (start.roundrobin)
			round_robin[start.rr_starting_point] = p;
		if (is_group_or_channel_match(p, start.span, start.groupmatch, &groupmatched, start.channelmatch, &channelmatched)	&& available(&p, channelmatched))
		{
			ast_log(LOG_DEBUG, "Using channel %d\n", p->channel);
			callwait = (p->owner != NULL);
			if (p->channel == CHAN_PSEUDO)
			{
				p = duplicate_pseudo(p);
				if (!p)
					break;
			}
			p->distinctivering = 0;
			switch (start.opt)
			{
			 case '\0':
				/* No option present. */
				break;
			 case 'c':
				/* Confirm answer */
				p->confirmanswer = 1;
				break;
			 case 'r':
				/* Distinctive ring */
				p->distinctivering = start.cadance;
				break;
			 case 'd':
				/* If this is an ISDN call, make it digital */
				transcapdigital = AST_TRANS_CAP_DIGITAL;
				break;
			 default:
				ast_log(LOG_WARNING, "Unknown option '%c' in '%s'\n", start.opt, (char *)data);
				break;
			}
			p->outgoing = 1;
			if (gcom_gsm_lib_handles(p->sig))
				tmp = sig_gsm_request(p->sig_pvt, &callwait,assignedids, requestor);
			if (!tmp) 
				p->outgoing = 0;
            else
				snprintf(p->dialstring, sizeof(p->dialstring), "GCOM/%s", (char *) data);
			break;
		}
		if (start.backwards)
		{
			p = p->prev;
			if (!p)
				p = ifend;
		}
		else
		{
			p = p->next;
			if (!p)
				p = iflist;
		}
		/* stop when you roll to the one that we started from */
		if (p == exitpvt)
			break;
	}
	ast_mutex_unlock(&iflock);
	if (cause && !tmp)
	{
		if (callwait || channelmatched)
			*cause = /*AST_CAUSE_BUSY*/ AST_CAUSE_CONGESTION;
        else if (groupmatched)
    		*cause = AST_CAUSE_CONGESTION;
		else
			*cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
	}
	return tmp;
 }

static int gcom_devicestate(void *data)
{
    return 0;
}

/*!
 * \internal
 * \brief Determine the device state for a given DAHDI device if we can.
 * \since 1.8
 *
 * \param data DAHDI device name after "DAHDI/".
 *
 * \retval device_state enum ast_device_state value.
 * \retval AST_DEVICE_UNKNOWN if we could not determine the device's state.
 */


/*!
 * \brief Callback made when dial failed to get a channel out of gcom_request().
 * \since 1.8
 *
 * \param inbound Incoming asterisk channel.
 * \param dest Same dial string passed to gcom_request().
 * \param callback Callback into CC core to announce a busy channel available for CC.
 *
 * \details
 * This callback acts like a forked dial with all prongs of the fork busy.
 * Essentially, for each channel that could have taken the call, indicate that
 * it is busy.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
 static int gcom_cc_callback(struct ast_channel *inbound, const char *dest, ast_cc_callback_fn callback)
 {
	struct gcom_pvt *p;
	struct gcom_pvt *exitpvt;
	struct gcom_starting_point start;
	int groupmatched = 0;
	int channelmatched = 0;
	ast_mutex_lock(&iflock);
	p = determine_starting_point(dest, &start);
	if (!p)
	{
		ast_mutex_unlock(&iflock);
		return -1;
	}
	exitpvt = p;
	for (;;)
	{
		if (is_group_or_channel_match(p, start.span, start.groupmatch, &groupmatched, start.channelmatch, &channelmatched))
		{
			/* We found a potential match. call the callback */
			struct ast_str *device_name;
			char *dash;
			const char *monitor_type;
			char dialstring[AST_CHANNEL_NAME];
			char full_device_name[AST_CHANNEL_NAME];
			switch (ast_get_cc_monitor_policy(p->cc_params))
			{
			case AST_CC_MONITOR_NEVER:
				break;
			case AST_CC_MONITOR_NATIVE:
			case AST_CC_MONITOR_ALWAYS:
			case AST_CC_MONITOR_GENERIC:
			     {
					device_name = create_channel_name(p);
					snprintf(full_device_name, sizeof(full_device_name), "DAHDI/%s",device_name ? ast_str_buffer(device_name) : "");
					ast_free(device_name);
					/*
					 * The portion after the '-' in the channel name is either a random
					 * number, a sequence number, or a subchannel number. None are
					 * necessary so strip them off.
					 */
					dash = strrchr(full_device_name, '-');
					if (dash)
						*dash = '\0';
				}
				snprintf(dialstring, sizeof(dialstring), "DAHDI/%s", dest);
				/*
				 * Analog can only do generic monitoring.
				 * ISDN is in a trunk busy condition and any "device" is going
				 * to be busy until a B channel becomes available.  The generic
				 * monitor can do this task.
				 */
				monitor_type = AST_CC_GENERIC_MONITOR_TYPE;
				callback(inbound,p->cc_params,monitor_type, full_device_name, dialstring, NULL);
				break;
			}
		}
		p = start.backwards ? p->prev : p->next;
		if (!p)
			p = start.backwards ? ifend : iflist;
		if (p == exitpvt)
			break;
	}
	ast_mutex_unlock(&iflock);
	return 0;
 }


static char *complete_span_helper(const char *line, const char *word, int pos, int state, int rpos)
{
	int which, span;
	char *ret = NULL;
	if (pos != rpos)
		return ret;
	for (which = span = 0; span < NUM_SPANS; span++)
	{
		if (gsms[span].gsm.pvts && ++which > state)
		{
			if (asprintf(&ret, "%d", span + 1) < 0) 	/* user indexes start from 1 */
				ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			break;
		}
	}
	return ret;
}

 static char *gcom_complete_span_4(const char *line, const char *word, int pos, int state)
 {
	return complete_span_helper(line,word,pos,state,3);
 }

 static char *handle_gsm_set_debug_file(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
	int myfd;
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set debug file";
		e->usage = "Usage: gsm set debug file [output-file]\n"
			"       Sends GSM debug output to the specified output file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc < 5)
		return CLI_SHOWUSAGE;
	if (ast_strlen_zero(a->argv[4]))
		return CLI_SHOWUSAGE;
	myfd = open(a->argv[4], O_CREAT|O_WRONLY, AST_FILE_MODE);
	if (myfd < 0) {
		ast_cli(a->fd, "Unable to open '%s' for writing\n", a->argv[4]);
		return CLI_SUCCESS;
	}
	ast_mutex_lock(&gsmdebugfdlock);

	if (gsmdebugfd >= 0)
		close(gsmdebugfd);
	gsmdebugfd = myfd;
	ast_copy_string(gsmdebugfilename,a->argv[4],sizeof(gsmdebugfilename));
	ast_mutex_unlock(&gsmdebugfdlock);
	ast_cli(a->fd, "GSM debug output will be sent to '%s'\n", a->argv[4]);
	return CLI_SUCCESS;
 }


 static char *handle_gsm_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
    int span;
	int level = 0;
	switch (cmd) 
    {
 	 case CLI_INIT:
		e->command = "gsm set debug {on|off|0|1|2} span";
		e->usage =
				"Usage: gsm set debug {<level>|on|off} span <span>\n"
			    "       Enables debugging on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gcom_complete_span_4(a->line, a->word, a->pos, a->n);
	}
	if (a->argc < 6) 
		return CLI_SHOWUSAGE;
	if (!strcasecmp(a->argv[3], "on")) 
		level = 1;
    else if (!strcasecmp(a->argv[3], "off"))
		level = 0;
    else
		level = atoi(a->argv[3]);
	span = atoi(a->argv[5]);
	if ((span < 1) || (span > NUM_SPANS)) 
    {
		ast_cli(a->fd, "Invalid span %s.  Should be a number %d to %d\n", a->argv[5], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!gsms[span-1].gsm.pvts) 
    {
		ast_cli(a->fd, "No GSM running on span %d\n", span);
		return CLI_SUCCESS;
	}
	if (level == 0)
	{
		/* Close the debugging file if it's set */
		ast_mutex_lock(&gsmdebugfdlock);
		if (0 <= gsmdebugfd)
		{
			close(gsmdebugfd);
			gsmdebugfd = -1;
			ast_cli(a->fd, "Disabled GSM debug output to file '%s'\n",
				gsmdebugfilename);
		}
		ast_mutex_unlock(&gsmdebugfdlock);
	}
	gsms[span - 1].gsm.debug = (level) ? 1 : 0;
	ast_cli(a->fd, "%s debugging on span %d\n", (level) ? "Enabled" : "Disabled", span);
	return CLI_SUCCESS;
}

 static char *handle_gsm_show_spans(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
  #define FORMAT  "%5d %-20.20s %-20.20s\n"
  #define FORMAT2  "%5s %-20.20s %-20.20s\n"
  int span;
  char status[GSM_SPAN_STAUTS_LEN] = {0};
  char active[GSM_SPAN_STAUTS_LEN] = {0};
  switch (cmd)
  {
	case CLI_INIT:
		e->command = "gsm show spans";
		e->usage =
			"Usage: gsm show spans\n"
			"       Displays ALL GSM spans information\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
  }
  if (a->argc != 3)
		return CLI_SHOWUSAGE;
  ast_cli(a->fd, FORMAT2, "SPANNO","STATUS","ACTIVE");
  for (span = 0; span < NUM_SPANS; span++)
  {
		if (gsms[span].gsm.pvts)
		{
		   sig_gsm_get_span_stats(span+1, status, active,GSM_SPAN_STAUTS_LEN);
          ast_cli(a->fd, FORMAT, span + 1, status, active);
		}
  }
  return CLI_SUCCESS;
 #undef FORMAT
 #undef FORMAT2
 }

static char *handle_gsm_show_span(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span;
    char status[GSM_SPAN_STAUTS_LEN] = {0};
    char active[GSM_SPAN_STAUTS_LEN] = {0};
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gsm show span";
		e->usage =
			"Usage: gsm show span <span>\n"
			"       Displays GSM Information on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gcom_complete_span_4(a->line, a->word, a->pos, a->n);
	}
	if (a->argc < 4)
		return CLI_SHOWUSAGE;
	span = atoi(a->argv[3]);
	if ((span < 1) || (span > NUM_SPANS))
	{
		ast_cli(a->fd, "Invalid span '%s'.  Should be a number from %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!gsms[span-1].gsm.pvts)
	{
		ast_cli(a->fd, "No GSM running on span %d\n", span);
		return CLI_SUCCESS;
	}
       sig_gsm_get_span_stats(span, status, active, GSM_SPAN_STAUTS_LEN);
       ast_cli(a->fd, "gsm spanno: %d: status: %s  active: %s\n", span , status, active);
	return CLI_SUCCESS;
 }


 static char *handle_gsm_show_span_csq(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
	int span;
    int csq;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gsm show span csq";
		e->usage =
			"Usage: gsm show span <span>\n"
			"       Displays GSM Information on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gcom_complete_span_4(a->line, a->word, a->pos, a->n);
	}
	if (a->argc < 5)
		return CLI_SHOWUSAGE;
	span = atoi(a->argv[4]);
	if ((span < 1) || (span > NUM_SPANS))
	{
		ast_cli(a->fd, "Invalid span '%s'.  Should be a number from %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!gsms[span-1].gsm.pvts)
	{
		ast_cli(a->fd, "No GSM running on span %d\n", span);
		return CLI_SUCCESS;
	}
    sig_gsm_get_span_csq(span, &csq);
    ast_cli(a->fd, "gsm spanno: %d: csq: %d\n", span , csq);
	return CLI_SUCCESS;
 }


 static char *handle_gsm_show_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
	int span = 0;
	int count=0;
	int debug = 0;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gsm show debug";
		e->usage =
			"Usage: gsm show debug\n"
			"	Show the debug state of gsm spans\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	for (span = 0; span < NUM_SPANS; span++) 
    {
     if (gsms[span].gsm.pvts)
     {
	  ast_cli(a->fd, "Span %d: Debug: %s\tIntense: %s\n", span+1, (debug&GSM_DEBUG_STATE)? "Yes" : "No" ,(debug&GSM_DEBUG_RAW)? "Yes" : "No" );
	  count++;
     }
	}
	ast_mutex_lock(&gsmdebugfdlock);
	if (gsmdebugfd >= 0)
		ast_cli(a->fd, "Logging GSM debug to file %s\n", gsmdebugfilename);
	ast_mutex_unlock(&gsmdebugfdlock);
	if (!count)
		ast_cli(a->fd, "No GSM running\n");
	return CLI_SUCCESS;
 }

static char *gsm_get_version(void)
{
    return "gsmversion 1.1";
}

static char *handle_gsm_show_span_siminfo(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
    int span = 0;
    char smsc[GSM_CALL_NUM_LEN] = {0}; /* SMS Service Centre */
    char version[GSM_CALL_NUM_LEN] = {0};
    char imsi[GSM_IMEI_LEN] = {0};
    char imei[GSM_IMEI_LEN] = {0};
    switch (cmd)
    {
	 case CLI_INIT:
		e->command = "gsm show span info";
		e->usage =
			"Usage: gsm show span info <span> \n"
			"Show span sim information\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
    if (a->argc < 5)
	    return CLI_SHOWUSAGE;
	span = atoi(a->argv[4]);
	if ((span < 1) || (span > NUM_SPANS))
	{
		ast_cli(a->fd, "Invalid span '%s'.  Should be a number from %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!gsms[span-1].gsm.pvts)
	{
		ast_cli(a->fd, "No GSM running on span %d\n", span);
		return CLI_SUCCESS;
	}
    sig_gsm_get_span_siminfo_imei(span, imei, GSM_IMEI_LEN);
    sig_gsm_get_span_siminfo_imsi(span, imsi, GSM_IMEI_LEN);
    sig_gsm_get_span_siminfo_version(span, version,GSM_CALL_NUM_LEN);
    sig_gsm_get_span_siminfo_smsc(span, smsc,GSM_CALL_NUM_LEN);
    ast_cli(a->fd,"VERSION: %s\n", version);
    ast_cli(a->fd,"IMEI   : %s\n", imei);
    ast_cli(a->fd,"IMSI   : %s\n", imsi);
    ast_cli(a->fd,"SMSC   : %s\n", smsc);
    return CLI_SUCCESS;
 }

 static char *handle_gsm_show_span_configure(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
    int span = 0;
    switch (cmd)
    {
	 case CLI_INIT:
		e->command = "gsm show span configure";
		e->usage =
			"Usage: gsm show span configure <span> \n"
			"Show span configure information\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
    if (a->argc < 5)
		return CLI_SHOWUSAGE;
	span = atoi(a->argv[4]);
	if ((span < 1) || (span > NUM_SPANS))
	{
		ast_cli(a->fd, "Invalid span '%s'.  Should be a number from %d to %d\n", a->argv[3], 1, NUM_SPANS);
		return CLI_SUCCESS;
	}
	if (!gsms[span-1].gsm.pvts)
	{
		ast_cli(a->fd, "No GSM running on span %d\n", span);
		return CLI_SUCCESS;
	}
    ast_cli(a->fd, "PINNUM: %s\n", (strlen(gsms[span-1].gsm.conf.pinnum) >0)?gsms[span-1].gsm.conf.pinnum:"NULL");
    ast_cli(a->fd, "LANG  :  %s\n", (strlen(gsms[span-1].gsm.conf.sms_lang)>0)?gsms[span-1].gsm.conf.sms_lang:"en");
    return CLI_SUCCESS;
 }

 static char *handle_gsm_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gsm show version";
		e->usage =
			"Usage: gsm show version\n"
			"Show libgsm version information\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	ast_cli(a->fd, "libgsmi version: %s\n", gsm_get_version());
	return CLI_SUCCESS;
 }

 static char *gcom_destroy_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
	int channel;
	int ret;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gcom destroy channel";
		e->usage =
			"Usage: gcom destroy channel <chan num>\n"
			"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	channel = atoi(a->argv[3]);
	ret = gcom_destroy_channel_bynum(channel);
	return ( RESULT_SUCCESS == ret ) ? CLI_SUCCESS : CLI_FAILURE;
 }

 static void gcom_softhangup_all(void)//must add
 {
	struct gcom_pvt *p;
retry:
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next)
	{
		ast_mutex_lock(&p->lock);
		if (p->owner && !p->restartpending)
		{
			if (ast_channel_trylock(p->owner))
			{
				if (option_debug > 2)
					ast_verbose("Avoiding deadlock\n");
				/* Avoid deadlock since you're not supposed to lock iflock or pvt before a channel */
				ast_mutex_unlock(&p->lock);
				ast_mutex_unlock(&iflock);
				goto retry;
			}
			if (option_debug > 2)
				ast_verbose("Softhanging up on %s\n",ast_channel_name(p->owner));
			ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_EXPLICIT);
			p->restartpending = 1;
			num_restart_pending++;
			ast_channel_unlock(p->owner);
		}
		ast_mutex_unlock(&p->lock);
	}
	ast_mutex_unlock(&iflock);
 }

 static int setup_gcom(int reload);



 static int restart_monitor(void)
 {
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	ast_mutex_lock(&monlock);
	if (monitor_thread == pthread_self())
	{
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread != AST_PTHREADT_NULL)
	{
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else
	{
		/* Start a new monitor */
		if (ast_pthread_create_background(&monitor_thread, NULL, sms_scan_thread, NULL) < 0)
		{
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
 }

 static int gcom_restart(void)
 {
	int i, j;
	int cancel_code;
	struct gcom_pvt *p;
	ast_mutex_lock(&restart_lock);
	ast_log(LOG_DEBUG, "Destroying channels and reloading DAHDI configuration.\n");
	gcom_softhangup_all();
	ast_log(LOG_DEBUG, "Initial softhangup of all DAHDI channels complete.\n");
	for (i = 0; i < NUM_SPANS; i++)
	{
		if (gsms[i].gsm.master && (gsms[i].gsm.master != AST_PTHREADT_NULL))
		{
			cancel_code = pthread_cancel(gsms[i].gsm.master);
			pthread_kill(gsms[i].gsm.master, SIGURG);
			ast_log(LOG_DEBUG, "Waiting to join thread of span %d with pid=%p, cancel_code=%d\n", i, (void *) gsms[i].gsm.master, cancel_code);
			pthread_join(gsms[i].gsm.master, NULL);
			ast_log(LOG_DEBUG, "Joined thread of span %d\n", i);
		}
        gsms[i].gsm.master = AST_PTHREADT_NULL;
	}
   	for (i = 0; i < NUM_SPANS; i++)
   	{
		if (gsms[i].gsm.mon && (gsms[i].gsm.mon != AST_PTHREADT_NULL))
		{
			cancel_code = pthread_cancel(gsms[i].gsm.mon);
			pthread_kill(gsms[i].gsm.mon, SIGURG);
			ast_log(LOG_DEBUG,"Waiting to join thread of span %d with pid=%p, cancel_code=%d\n", i, (void *) gsms[i].gsm.mon, cancel_code);
			pthread_join(gsms[i].gsm.mon, NULL);
			ast_log(LOG_DEBUG,"Joined thread of span %d\n", i);
		}
        gsms[i].gsm.mon = AST_PTHREADT_NULL;
	}
	ast_mutex_lock(&monlock);
	if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL))
	{
		cancel_code = pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		ast_log(LOG_DEBUG,"Waiting to join monitor thread with pid=%p, cancel_code=%d\n", (void *) monitor_thread, cancel_code);
		pthread_join(monitor_thread, NULL);
		ast_log(LOG_DEBUG,"Joined monitor thread\n");
	}
	monitor_thread = AST_PTHREADT_NULL; /* prepare to restart thread in setup_gcom once channels are reconfigured */
	ast_mutex_lock(&ss_thread_lock);
	while (ss_thread_count > 0) /* let ss_threads finish and run gcom_hangup before dahvi_pvts are destroyed */
	{
		int x = DAHDI_FLASH;
		ast_log(LOG_DEBUG,"Waiting on %d analog_ss_thread(s) to finish\n", ss_thread_count);
		ast_mutex_lock(&iflock);
		for (p = iflist; p; p = p->next)
		{
			if (p->owner) /* important to create an event for gcom_wait_event to register so that all analog_ss_threads terminate */
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
		}
		ast_mutex_unlock(&iflock);
		ast_cond_wait(&ss_thread_complete, &ss_thread_lock);
	}
	/* ensure any created channels before monitor threads were stopped are hungup */
	gcom_softhangup_all();
	ast_log(LOG_DEBUG,"Final softhangup of all DAHDI channels complete.\n");
	destroy_all_channels();
	ast_log(LOG_DEBUG,"Channels destroyed. Now re-reading config. %d active channels remaining.\n", ast_active_channels());
	ast_mutex_unlock(&monlock);
	for (i = 0; i < NUM_SPANS; i++)
	{
		for (j = 0; j < SIG_GSM_NUM_DCHANS; j++)
			gcom_close_gsm_fd(&(gsms[i]), j);
	}
	memset(gsms, 0, sizeof(gsms));
	for (i = 0; i < NUM_SPANS; i++)
		sig_gsm_init_gsm(&gsms[i].gsm);
	if (setup_gcom(2) != 0)
	{
		ast_log(LOG_WARNING, "Reload channels from dahdi config failed!\n");
		ast_mutex_unlock(&ss_thread_lock);
       ast_mutex_unlock(&restart_lock);
		return 1;
	}
	ast_mutex_unlock(&ss_thread_lock);
	ast_mutex_unlock(&restart_lock);
	return 0;
 }

static char *gcom_restart_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gcom restart";
		e->usage =
			"Usage: gcom restart\n"
			"	Restarts the GCOM channels: destroys them all and then\n"
			"	re-reads them from chan_dahdi.conf.\n"
			"	Note that this will STOP any running CALL on GCOM channels.\n"
			"";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 2)
		return CLI_SHOWUSAGE;
	if (gcom_restart() != 0)
		return CLI_FAILURE;
	return CLI_SUCCESS;
 }


static char *gcom_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
    #define FORMAT "%7s %-10.10s %-15.15s %-10.10s %-20.20s %-10.10s %-10.10s\n"
    #define FORMAT2 "%7s %-10.10s %-15.15s %-10.10s %-20.20s %-10.10s %-10.10s\n"
	unsigned int targetnum = 0;
	int filtertype = 0;
	struct gcom_pvt *tmp = NULL;
	char tmps[20] = "";
	char statestr[20] = "";
	char blockstr[20] = "";
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gcom show channels [group|context]";
		e->usage =
			"Usage: gcom show channels [ group <group> | context <context> ]\n"
			"	Shows a list of available channels with optional filtering\n"
			"	<group> must be a number between 0 and 63\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	if (!((a->argc == 3) || (a->argc == 5)))
		return CLI_SHOWUSAGE;
	if (a->argc == 5)
	{
		if (!strcasecmp(a->argv[3], "group"))
		{
			targetnum = atoi(a->argv[4]);
			if ((targetnum < 0) || (targetnum > 63))
				return CLI_SHOWUSAGE;
			targetnum = 1 << targetnum;
			filtertype = 1;
		}
		else if (!strcasecmp(a->argv[3], "context"))
			filtertype = 2;
	}
	ast_cli(a->fd, FORMAT2, "Chan", "Extension", "Context", "Language", "MOH Interpret", "Blocked", "State");
	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next)
	{
		if (filtertype)
		{
			switch(filtertype)
			{
			 case 1: /* dahdi show channels group <group> */
				if (!(tmp->group & targetnum))
					continue;
				break;
			case 2: /* dahdi show channels context <context> */
				if (strcasecmp(tmp->context, a->argv[4]))
					continue;
				break;
			default:
				break;
			}
		 }
		if (tmp->channel > 0)
			snprintf(tmps, sizeof(tmps), "%d", tmp->channel);
		else
 		   ast_copy_string(tmps, "pseudo", sizeof(tmps));
        blockstr[1] = '\0';
 	    snprintf(statestr, sizeof(statestr), "%s", "In Service");
		ast_cli(a->fd, FORMAT, tmps, tmp->exten, tmp->context, tmp->language, tmp->mohinterpret, blockstr, statestr);
	}
	ast_mutex_unlock(&iflock);
	return CLI_SUCCESS;
 #undef FORMAT
 #undef FORMAT2
 }

 static char *gcom_show_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
	int channel;
	struct gcom_pvt *tmp = NULL;
	struct dahdi_confinfo ci;
	struct dahdi_params ps;
	int x;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gcom show channel";
		e->usage =
			"Usage: gcom show channel <chan num>\n"
			"	Detailed information about a given channel\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	channel = atoi(a->argv[3]);
	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next)
	{
		if (tmp->channel == channel)
		{
			ast_cli(a->fd, "Channel: %d\n", tmp->channel);
			ast_cli(a->fd, "File Descriptor: %d\n", tmp->subs[SUB_REAL].dfd);
			ast_cli(a->fd, "Span: %d\n", tmp->span);
			ast_cli(a->fd, "Extension: %s\n", tmp->exten);
			ast_cli(a->fd, "Dialing: %s\n", tmp->dialing ? "yes" : "no");
			ast_cli(a->fd, "Context: %s\n", tmp->context);
			ast_cli(a->fd, "Caller ID: %s\n", tmp->cid_num);
			ast_cli(a->fd, "Caller ID name: %s\n", tmp->cid_name);
			if (tmp->vars)
			{
				struct ast_variable *v;
				ast_cli(a->fd, "Variables:\n");
				for (v = tmp->vars ; v ; v = v->next)
					ast_cli(a->fd, "       %s = %s\n", v->name, v->value);
			}
			ast_cli(a->fd, "Destroy: %d\n", tmp->destroy);
			ast_cli(a->fd, "InAlarm: %d\n", tmp->inalarm);
			ast_cli(a->fd, "Signalling Type: %s\n", sig2str(tmp->sig));
			ast_cli(a->fd, "Radio: %d\n", tmp->radio);
			ast_cli(a->fd, "Owner: %s\n", tmp->owner ? ast_channel_name(tmp->owner) : "<None>");
			ast_cli(a->fd, "Real: %s%s%s\n", tmp->subs[SUB_REAL].owner ? ast_channel_name(tmp->subs[SUB_REAL].owner) : "<None>", tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : "", tmp->subs[SUB_REAL].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Callwait: %s%s%s\n", tmp->subs[SUB_CALLWAIT].owner ? ast_channel_name(tmp->subs[SUB_CALLWAIT].owner) : "<None>", tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : "", tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Threeway: %s%s%s\n", tmp->subs[SUB_THREEWAY].owner ? ast_channel_name(tmp->subs[SUB_THREEWAY].owner) : "<None>", tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : "", tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "");
			ast_cli(a->fd, "Confno: %d\n", tmp->confno);
			ast_cli(a->fd, "Propagated Conference: %d\n", tmp->propconfno);
			ast_cli(a->fd, "Real in conference: %d\n", tmp->inconference);
			ast_cli(a->fd, "DSP: %s\n", tmp->dsp ? "yes" : "no");
			ast_cli(a->fd, "Busy Detection: %s\n", tmp->busydetect ? "yes" : "no");
			if (tmp->busydetect)
			{
				ast_cli(a->fd, "    Busy Count: %d\n", tmp->busycount);
  			    ast_cli(a->fd, "	 Busy Pattern: %d,%d,%d,%d\n", tmp->busy_cadence.pattern[0], tmp->busy_cadence.pattern[1], (tmp->busy_cadence.length == 4) ? tmp->busy_cadence.pattern[2] : 0, (tmp->busy_cadence.length == 4) ? tmp->busy_cadence.pattern[3] : 0);
			}
			ast_cli(a->fd, "TDD: %s\n", tmp->tdd ? "yes" : "no");
			ast_cli(a->fd, "Relax DTMF: %s\n", tmp->dtmfrelax ? "yes" : "no");
			ast_cli(a->fd, "Dialing/CallwaitCAS: %d/%d\n", tmp->dialing, tmp->callwaitcas);
			ast_cli(a->fd, "Default law: %s\n", tmp->law_default == DAHDI_LAW_MULAW ? "ulaw" : tmp->law_default == DAHDI_LAW_ALAW ? "alaw" : "unknown");
			ast_cli(a->fd, "Fax Handled: %s\n", tmp->faxhandled ? "yes" : "no");
			ast_cli(a->fd, "Pulse phone: %s\n", tmp->pulsedial ? "yes" : "no");
			ast_cli(a->fd, "Gains (RX/TX): %.2f/%.2f\n", tmp->rxgain, tmp->txgain);
			ast_cli(a->fd, "Dynamic Range Compression (RX/TX): %.2f/%.2f\n", tmp->rxdrc, tmp->txdrc);
			ast_cli(a->fd, "DND: %s\n", gcom_dnd(tmp, -1) ? "yes" : "no");
			ast_cli(a->fd, "Echo Cancellation:\n");
			if (tmp->echocancel.head.tap_length)
			{
				ast_cli(a->fd, "\t%d taps\n", tmp->echocancel.head.tap_length);
				for (x = 0; x < tmp->echocancel.head.param_count; x++)
					ast_cli(a->fd, "\t\t%s: %ud\n", tmp->echocancel.params[x].name, tmp->echocancel.params[x].value);
				ast_cli(a->fd, "\t%scurrently %s\n", tmp->echocanbridged ? "" : "(unless TDM bridged) ", tmp->echocanon ? "ON" : "OFF");
			}
			else
				ast_cli(a->fd, "\tnone\n");
			if (tmp->master)
				ast_cli(a->fd, "Master Channel: %d\n", tmp->master->channel);
			for (x = 0; x < MAX_SLAVES; x++)
			{
				if (tmp->slaves[x])
					ast_cli(a->fd, "Slave Channel: %d\n", tmp->slaves[x]->channel);
			}
			if (tmp->gsm)
			{
				struct sig_gsm_chan *chan = tmp->sig_pvt;
				ast_cli(a->fd, "GSM Flags: ");
				if (chan->resetting)
					ast_cli(a->fd, "Resetting ");
				ast_cli(a->fd, "\n");
				if (tmp->logicalspan)
					ast_cli(a->fd, "GSM Logical Span: %d\n", tmp->logicalspan);
				else
					ast_cli(a->fd, "GSM Logical Span: Implicit\n");
			}
			memset(&ci, 0, sizeof(ci));
			ps.channo = tmp->channel;
			if (tmp->subs[SUB_REAL].dfd > -1)
			{
				memset(&ci, 0, sizeof(ci));
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONF, &ci))
					ast_cli(a->fd, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, ci.confmode);
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONFMUTE, &x))
					ast_cli(a->fd, "Actual Confmute: %s\n", x ? "Yes" : "No");
				memset(&ps, 0, sizeof(ps));
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0)
					ast_log(LOG_WARNING, "Failed to get parameters on channel %d: %s\n", tmp->channel, strerror(errno));
				else
					ast_cli(a->fd, "Hookstate (FXS only): %s\n", ps.rxisoffhook ? "Offhook" : "Onhook");
			}
			ast_mutex_unlock(&iflock);
			return CLI_SUCCESS;
		}
	}
	ast_mutex_unlock(&iflock);
	ast_cli(a->fd, "Unable to find given channel %d\n", channel);
	return CLI_FAILURE;
 }


/* Based on irqmiss.c */
static char *gcom_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	#define FORMAT "%-40.40s %-7.7s %-6d %-6d %-6d %-3.3s %-4.4s %-8.8s %s\n"
	#define FORMAT2 "%-40.40s %-7.7s %-6.6s %-6.6s %-6.6s %-3.3s %-4.4s %-8.8s %s\n"
	int span;
	int res;
	char alarmstr[50];
	int ctl;
	struct dahdi_spaninfo s;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gcom show status";
		e->usage =
			"Usage: gcom show status\n"
			"       Shows a list of DAHDI cards with status\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0)
	{
		ast_cli(a->fd, "No DAHDI found. Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		return CLI_FAILURE;
	}
	ast_cli(a->fd, FORMAT2, "Description", "Alarms", "IRQ", "bpviol", "CRC", "Framing", "Coding", "Options", "LBO");
	for (span = 1; span < DAHDI_MAX_SPANS; ++span)
	{
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res)
			continue;
		alarmstr[0] = '\0';
		if (s.alarms > 0)
		{
			if (s.alarms & DAHDI_ALARM_BLUE)
				strcat(alarmstr, "BLU/");
			if (s.alarms & DAHDI_ALARM_YELLOW)
				strcat(alarmstr, "YEL/");
			if (s.alarms & DAHDI_ALARM_RED)
				strcat(alarmstr, "RED/");
			if (s.alarms & DAHDI_ALARM_LOOPBACK)
				strcat(alarmstr, "LB/");
			if (s.alarms & DAHDI_ALARM_RECOVER)
				strcat(alarmstr, "REC/");
			if (s.alarms & DAHDI_ALARM_NOTOPEN)
				strcat(alarmstr, "NOP/");
			if (!strlen(alarmstr))
				strcat(alarmstr, "UUU/");
			if (strlen(alarmstr))
			{
				/* Strip trailing / */
				alarmstr[strlen(alarmstr) - 1] = '\0';
			}
		}
		else
		{
			if (s.numchans)
				strcpy(alarmstr, "OK");
			else
				strcpy(alarmstr, "UNCONFIGURED");
		}
		ast_cli(a->fd, FORMAT, s.desc, alarmstr, s.irqmisses, s.bpvcount, s.crc4count,
				s.lineconfig & DAHDI_CONFIG_D4 ? "D4" :
			    s.lineconfig & DAHDI_CONFIG_ESF ? "ESF" :
			    s.lineconfig & DAHDI_CONFIG_CCS ? "CCS" :
			     "CAS",
		  	    s.lineconfig & DAHDI_CONFIG_B8ZS ? "B8ZS" :
			    s.lineconfig & DAHDI_CONFIG_HDB3 ? "HDB3" :
			    s.lineconfig & DAHDI_CONFIG_AMI ? "AMI" :
			     "Unk",
			    s.lineconfig & DAHDI_CONFIG_CRC4 ?
				s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" :
				s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "YEL" : "",
			 lbostr[s.lbo]);
	}
	close(ctl);
	return CLI_SUCCESS;
 #undef FORMAT
 #undef FORMAT2
 }

static char *gcom_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int pseudo_fd = -1;
	struct dahdi_versioninfo vi;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gcom show version";
		e->usage =
			"Usage: gcom show version\n"
			"       Shows the DAHDI version in use\n";
		return NULL;
	 case CLI_GENERATE:
		return NULL;
	}
	if ((pseudo_fd = open("/dev/dahdi/ctl", O_RDONLY)) < 0)
	{
		ast_cli(a->fd, "Failed to open control file to get version.\n");
		return CLI_SUCCESS;
	}
	strcpy(vi.version, "Unknown");
	strcpy(vi.echo_canceller, "Unknown");
	if (ioctl(pseudo_fd, DAHDI_GETVERSION, &vi))
		ast_cli(a->fd, "Failed to get DAHDI version: %s\n", strerror(errno));
	else
		ast_cli(a->fd, "DAHDI Version: %s Echo Canceller: %s\n", vi.version, vi.echo_canceller);
	close(pseudo_fd);
	return CLI_SUCCESS;
 }

 static int process_sendsms(int span, char *msg, char *num, int queueflag)
 {

      struct gsm_sms_event_data event;
      char *coding = NULL;
      int res = 0;
      char smsc[GSM_CALL_NUM_LEN] = {0};
      memset(&event, 0, sizeof(event));
      if(!msg || !num || ( strlen(msg) == 0 ) || ( strlen(num) == 0 ) )
      {
            ast_log(LOG_WARNING,"process sendsms num or msg is invalid\n");
            sig_gsm_span_sms_fail_log(span,NULL, 0,"num or msg is zero\n");
            return 1;
      }
      coding = parse_lang_charset(gsms[span-1].gsm.conf.sms_lang, &event.content.charset);
      res = 0;
      if(queueflag)
            event.queue_flag = 1;
      if(res==0)
      {
         event.gsm_sms_mode = GSM_SMS_TXT;
         memcpy(event.to, num, sizeof(event.to));
         memcpy(event.content.message, msg, sizeof(event.content.message));
         memcpy(event.body, msg, strlen(msg));
         event.content.msg_len = strlen(event.content.message);
         res = sig_gsm_send_sms(span, &event);
         if(res)
            ast_log(LOG_ERROR, "sig_gsm_send_sms fail\n");
      }
      else
      {
         char out_data[GSM_MAX_SMS_LEN] = {0};
         int out_data_len = 0;
         int out_len = 0;
         size_t in_ucs2_len ;
         char Dst[GSM_MAX_SMS_LEN] = {0};
         int i=0;
         sig_gsm_decode_pdu_content((unsigned char*)out_data, &out_data_len, msg,event.content.charset);
         in_ucs2_len = out_data_len;
         i = sig_gsm_EncodeUCS2( (unsigned char*)out_data, (unsigned char*)Dst, in_ucs2_len , &out_len);
         if( i > 0)
         {
            ast_log(LOG_ERROR, "sig_gsm_send_sms  sig_gsm_EncodeUCS2 error\n");
            sig_gsm_span_sms_fail_log(span,num, strlen(num),"process_sendsms  iconv UCS2 fail\n");
            return 1;
         }
         if(((out_len*2) > GSM_MAX_HEX_CONTENT_LEN ) )//ucs2
         {
            ast_log(LOG_ERROR, "sig_gsm_send_sms send sms  len=%d  is max 1024\n",out_len);
            sig_gsm_span_sms_fail_log(span,num, strlen(num),"process_sendsms  sms len is too long\n");
            return 1;
         }
         for(i = 0; i<out_len; i++)
            sprintf((char *)&event.content.message[i*2], "%02x", (0xFF) & Dst[i]);
         sig_gsm_get_span_siminfo_smsc(span, smsc,GSM_CALL_NUM_LEN);
         if(strlen(smsc) < 2)
         {
            ast_log(LOG_ERROR, "sig_gsm_send_sms have no smsc\n");
            sig_gsm_span_sms_fail_log(span,num, strlen(num),"process_sendsms  have no smsc\n");
            return 1;
         }      
         memcpy(event.smsc, smsc, sizeof(event.smsc));
         event.content.msg_len = out_len; 
         event.dcs = 0x08;
         event.pid = 0x00;
         memcpy(event.to, num, sizeof(event.to));
         event.gsm_sms_mode = GSM_SMS_PDU;
         ast_log(LOG_WARNING,"sendsms pdu mode to num=%s content=%s len=%d\n",event.to,event.content.message,event.content.msg_len);
         if(event.content.msg_len > (GSM_PDU_UCS2_MODE_LEN) )
         {
            event.long_flag = 1;
            res = gcom_longsms_send(&event, span);
            if(res)
            {
                ast_log(LOG_ERROR, "sig_gsm_send_sms fail\n");
                sig_gsm_span_sms_fail_log(span,num, strlen(num),"process_sendsms longsms fail\n");
            }
         }
         else
         {
            res=  sig_gsm_send_sms(span, &event);
            if(res)
            {
                ast_log(LOG_ERROR, "sig_gsm_send_sms fail\n");
                sig_gsm_span_sms_fail_log(span,num, strlen(num),"process_sendsms  sms req fail\n");
            }
         }
      }
    return res;
 }

static int gcom_longsms_send(struct gsm_sms_event_data *data, int span_id)
{
      int sendlen = 0;
      int len = data->content.msg_len*2;
      int offset = 0;
      char msg[GSM_MAX_HEX_CONTENT_LEN] = {0};
      char *p = NULL;
      int res = 0;
      int times = 0;
      struct sig_gsm_span *span = &gsms[span_id-1].gsm;
      if(!data)
      {
          ast_log(LOG_WARNING, "gcom_sms_thread but data is null\n");
          return 1;
      }
      memcpy(msg, data->content.message, sizeof(msg));
      p = msg;
      while(sendlen < len)
      {
            times = 0;
            memset(&data->content.message, 0, sizeof(data->content.message));
            p = p + offset;
            if((len - sendlen) >= GSM_PDU_UCS2_MODE_LEN*2)
            {
                offset = GSM_PDU_UCS2_MODE_LEN*2;
                sendlen += offset; 
            }
            else
            {
                offset = len - sendlen;
                sendlen = len;
            }
            data->content.msg_len = offset/2;
            memcpy(data->content.message, p, offset);
            span->sms_stats = 0;
            res = sig_gsm_send_sms(span_id, data);
            if(res)
                return 1;
            else
            {
                while(!span->sms_stats)
                {
                    sleep(1);
                    times++;
                    if(times>=20)
                        break;
                 }
            }
      }
      return 0;
 }

 char *handle_gsm_send_sms(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
 {
    int span;
    struct gsm_sms_event_data event;
    char ud[GSM_MAX_SMS_LEN] = {0};
    int i = 0;
    memset(&event, 0, sizeof(event));
    switch (cmd)
    {
       case CLI_INIT:
           e->command = "gsm send sms";
           e->usage =
                "Usage: gsm send sms <span> <number> <sms>\n"
                "       Send a sms on <span> <number> <sms>\n";
                return NULL;
       case CLI_GENERATE:
            return NULL;
       }
       if (a->argc < 6)
               return CLI_SHOWUSAGE;
       snprintf(ud, sizeof(ud),"%s", a->argv[5]);
       if(a->argc > 6)
       {
          for(i=0; i < a->argc-6; i++)
          {
           if( (strlen(ud) + strlen(a->argv[i+6]) + 1 ) >  GSM_MAX_SMS_LEN )
               break;
            strcat(ud," ");
            strcat(ud, a->argv[i+6]);
           }
        }
       span = atoi(a->argv[3]);
       if ((span < 1) || (span > GSM_MAX_SPANS))
       {
          ast_cli(a->fd, "Invalid span '%s'.  Should be a number from %d to %d\n", a->argv[3], 1, GSM_MAX_SPANS);
          return CLI_SUCCESS;
       }
       if (!gsms[span-1].gsm.span_id)
       {
               ast_cli(a->fd, "No GSM running on span %d\n", span);
               return CLI_SUCCESS;
       }
      process_sendsms(span, (char*)ud, (char *)a->argv[4], 0);
      return CLI_SUCCESS;
 }

static char *handle_gsm_execute_atcommand(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int span = 0;
	char at_command[256] = {0};
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = a->argv;
	switch (cmd)
	{
	 case CLI_INIT:
		e->command = "gsm exec at";
		e->usage = 
			"Usage: gsm exec at <spanno> <AT Command>\n"
			"       Send AT Command on a specific GSM span\n";
		return NULL;
	 case CLI_GENERATE:
		return gcom_complete_span_4(a->line, a->word, a->pos, a->n);
	}
	if (argc < 5)
		return CLI_SHOWUSAGE;
	span = atoi(argv[3]);
	if ((span < 1) || (span > GSM_MAX_SPANS))
	{
		ast_cli(fd, "Invalid span %s.  Should be a number %d to %d\n", argv[3], 1, GSM_MAX_SPANS);
		return CLI_SHOWUSAGE;
	}
	if (!gsms[span-1].gsm.span_id)
	{
		ast_cli(fd, "No GSM running on span %d\n", span);
		return CLI_SHOWUSAGE;
	}
    ast_copy_string(at_command, argv[4], sizeof(at_command));
	int len = strlen(at_command);
	int i=0;
	for(i=0;i<len;i++)
	{
	   if(at_command[i] == '%')
		at_command[i] = '"';
	}
	sig_gsm_exec_at(&gsms[span-1].gsm, at_command);
	return CLI_SUCCESS;
 }

 static struct ast_cli_entry gcom_gsm_cli[] =
 {
	AST_CLI_DEFINE(handle_gsm_debug, "Enables GSM debugging on a span"),
	AST_CLI_DEFINE(handle_gsm_show_spans, "Displays GSM Information"),
	AST_CLI_DEFINE(handle_gsm_show_span_csq, "Displays GSM csq"),
	AST_CLI_DEFINE(handle_gsm_show_span, "Displays GSM Information"),
	AST_CLI_DEFINE(handle_gsm_show_debug, "Displays current GSM debug settings"),
	AST_CLI_DEFINE(handle_gsm_set_debug_file, "Sends GSM debug output to the specified file"),
	AST_CLI_DEFINE(handle_gsm_version, "Displays libgsmat version"),
	AST_CLI_DEFINE(handle_gsm_send_sms, "send sms with cli cmd"),
	AST_CLI_DEFINE(handle_gsm_execute_atcommand, "Send AT Commmand on a specific GSM span"),
	AST_CLI_DEFINE(handle_gsm_show_span_configure, "Displays span configure"),
	AST_CLI_DEFINE(handle_gsm_show_span_siminfo, "Displays span module info"),

};


 static struct ast_cli_entry gcom_cli[] =
 {
	AST_CLI_DEFINE(gcom_show_channels, "Show active GCOM channels"),
	AST_CLI_DEFINE(gcom_show_channel, "Show information on a channel"),
	AST_CLI_DEFINE(gcom_destroy_channel, "Destroy a channel"),
	AST_CLI_DEFINE(gcom_restart_cmd, "Fully restart DAHDI channels"),
	AST_CLI_DEFINE(gcom_show_status, "Show all DAHDI cards status"),
	AST_CLI_DEFINE(gcom_show_version, "Show the DAHDI version in use"),
 };

#define TRANSFER	0
#define HANGUP		1


 static int __unload_module(void)
 {
	struct gcom_pvt *p;
	int i, j;
	for (i = 0; i < NUM_SPANS; i++)
	{
		if (gsms[i].gsm.master != AST_PTHREADT_NULL)
			pthread_cancel(gsms[i].gsm.master);
	}
    for (i = 0; i < NUM_SPANS; i++)
    {
		if (gsms[i].gsm.mon != AST_PTHREADT_NULL)
			pthread_cancel(gsms[i].gsm.mon);
	}
	ast_cli_unregister_multiple(gcom_gsm_cli, ARRAY_LEN(gcom_gsm_cli));
	ast_cli_unregister_multiple(gcom_cli, ARRAY_LEN(gcom_cli));
	ast_channel_unregister(&gcom_tech);
	/* Hangup all interfaces if they have an owner */
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next)
	{
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
	}
	ast_mutex_unlock(&iflock);
	ast_mutex_lock(&monlock);
	if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL))
	{
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}
	monitor_thread = AST_PTHREADT_STOP;
	ast_mutex_unlock(&monlock);
	destroy_all_channels();
	for (i = 0; i < NUM_SPANS; i++)
	{
		if (gsms[i].gsm.master && (gsms[i].gsm.master != AST_PTHREADT_NULL))
			pthread_join(gsms[i].gsm.master, NULL);
		for (j = 0; j < SIG_GSM_NUM_DCHANS; j++)
		{
			gcom_close_gsm_fd(&(gsms[i]), j);
		}
	}
	ast_cond_destroy(&ss_thread_complete);
	return 0;
 }

 static void string_replace(char *str, int char1, int char2)
 {
	for (; *str; str++)
	{
		if (*str == char1)
			*str = char2;
	}
 }

 static char *parse_spanchan(char *chanstr, char **subdir)
 {
	char *p;
	if ((p = strrchr(chanstr, '!')) == NULL)
	{
		*subdir = NULL;
		return chanstr;
	}
	*p++ = '\0';
	string_replace(chanstr, '!', '/');
	*subdir = chanstr;
	return p;
 }


 static int build_channels(struct gcom_chan_conf *conf, const char *value, int reload, int lineno, int *found_pseudo)
 {
	char *c, *chan;
	char *subdir;
	int x, start, finish;
	struct gcom_pvt *tmp;
	if ((reload == 0) && (conf->chan.sig < 0) && !conf->is_sig_auto)
	{
		ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
		return -1;
	}
	c = ast_strdupa(value);
	c = parse_spanchan(c, &subdir);
	while ((chan = strsep(&c, ",")))
	{
		if (sscanf(chan, "%30d-%30d", &start, &finish) == 2)
		{
			/* Range */
		}
		else if (sscanf(chan, "%30d", &start))
		{
			/* Just one */
			finish = start;
		}
		else if (!strcasecmp(chan, "pseudo"))
		{
			finish = start = CHAN_PSEUDO;
			if (found_pseudo)
				*found_pseudo = 1;
		}
		else
		{
			ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", value, chan);
			return -1;
		}
		if (finish < start)
		{
			ast_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
			x = finish;
			finish = start;
			start = x;
		}
		for (x = start; x <= finish; x++)
		{
			char fn[PATH_MAX];
			int real_channel = x;
			if (!ast_strlen_zero(subdir))
			{
				real_channel = device2chan(subdir, x, fn, sizeof(fn));
				if (real_channel < 0)
				{
					if (conf->ignore_failed_channels)
					{
						ast_log(LOG_WARNING, "Failed configuring %s!%d, (got %d). But moving on to others.\n",subdir, x, real_channel);
						continue;
					}
					else
					{
						ast_log(LOG_ERROR, "Failed configuring %s!%d, (got %d).\n",subdir, x, real_channel);
						return -1;
					}
				}
			}
			tmp = mkintf(real_channel, conf, reload);
			if (tmp)
				ast_verb(3, "%s channel %d, %s signalling\n", reload ? "Reconfigured" : "Registered", real_channel, sig2str(tmp->sig));
			else
			{
				ast_log(LOG_ERROR, "Unable to %s channel '%s'\n",(reload == 1) ? "reconfigure" : "register", value);
				return -1;
			}
		}
	}
	return 0;
 }

/** The length of the parameters list of 'gcomchan'.*/
#define MAX_CHANLIST_LEN 80

 static void process_echocancel(struct gcom_chan_conf *confp, const char *data, unsigned int line)
 {
	char *parse = ast_strdupa(data);
	char *params[DAHDI_MAX_ECHOCANPARAMS + 1];
	unsigned int param_count;
	unsigned int x;
	if (!(param_count = ast_app_separate_args(parse, ',', params, ARRAY_LEN(params))))
		return;
	memset(&confp->chan.echocancel, 0, sizeof(confp->chan.echocancel));
	x = ast_strlen_zero(params[0]) ? 0 : atoi(params[0]); /* first parameter is tap length, process it here */
	if ((x == 32) || (x == 64) || (x == 128) || (x == 256) || (x == 512) || (x == 1024))
		confp->chan.echocancel.head.tap_length = x;
	else if ((confp->chan.echocancel.head.tap_length = ast_true(params[0])))
		confp->chan.echocancel.head.tap_length = 1024;
	for (x = 1; x < param_count; x++) /* now process any remaining parameters */
	{
		struct
		{
			char *name;
			char *value;
		} param;
		if (ast_app_separate_args(params[x], '=', (char **) &param, 2) < 1)
		{
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %d: '%s'\n", line, params[x]);
			continue;
		}
		if (ast_strlen_zero(param.name) || (strlen(param.name) > sizeof(confp->chan.echocancel.params[0].name)-1))
		{
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %d: '%s'\n", line, param.name);
			continue;
		}
		strcpy(confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].name, param.name);
		if (param.value)
		{
			if (sscanf(param.value, "%30d", &confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].value) != 1)
			{
				ast_log(LOG_WARNING, "Invalid echocancel parameter value supplied at line %d: '%s'\n", line, param.value);
				continue;
			}
		}
		confp->chan.echocancel.head.param_count++;
	}
 }

/*! process_gcom() - ignore keyword 'channel' and similar */
#define PROC_GCOM_OPT_NOCHAN  (1 << 0)
/*! process_gcom() - No warnings on non-existing cofiguration keywords */
#define PROC_GCOM_OPT_NOWARN  (1 << 1)

 static int process_gcom(struct gcom_chan_conf *confp, const char *cat, struct ast_variable *v, int reload, int options)
 {
	struct gcom_pvt *tmp;
	int y;
	int found_pseudo = 0;
	struct ast_variable *gcomchan = NULL;
	for (; v; v = v->next)
	{
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;
		/* Create the interface list */
		if (!strcasecmp(v->name, "channel") || !strcasecmp(v->name, "channels"))
		{
 			if (options & PROC_GCOM_OPT_NOCHAN)
 			{
				ast_log(LOG_WARNING, "Channel '%s' ignored.\n", v->value);
 				continue;
			}
			if (build_channels(confp, v->value, reload, v->lineno, &found_pseudo))
			{
				if (confp->ignore_failed_channels)
				{
					ast_log(LOG_WARNING, "Channel '%s' failure ignored: ignore_failed_channels.\n", v->value);
					continue;
				}
				else
				{
				    ast_log(LOG_WARNING, "jwj process gcom and build channels lineno=%d\n",v->lineno);
 					return -1;
				}
			}
			ast_log(LOG_DEBUG, "Channel '%s' configured.\n", v->value);
		}
		else if (!strcasecmp(v->name, "ignore_failed_channels"))
			confp->ignore_failed_channels = ast_true(v->value);
		else if (!strcasecmp(v->name, "buffers"))
		{
			if (parse_buffers_policy(v->value, &confp->chan.buf_no, &confp->chan.buf_policy))
			{
				ast_log(LOG_WARNING, "Using default buffer policy.\n");
				confp->chan.buf_no = numbufs;
				confp->chan.buf_policy = DAHDI_POLICY_IMMEDIATE;
			}
		}
		else if (!strcasecmp(v->name, "faxbuffers"))
		{
			if (!parse_buffers_policy(v->value, &confp->chan.faxbuf_no, &confp->chan.faxbuf_policy))
				confp->chan.usefaxbuffers = 1;
 		}
        else if(!strcasecmp(v->name,"trace_log"))
            trace_debug = ast_true(v->value);
        else if (!strcasecmp(v->name, "gcomchan"))
			gcomchan = v; /* Only process the last gcomchan value. */
        else if (!strcasecmp(v->name, "usecallerid"))
			confp->chan.use_callerid = ast_true(v->value);
		else if (!strcasecmp(v->name, "threewaycalling"))
			confp->chan.threewaycalling = ast_true(v->value);
		else if (!strcasecmp(v->name, "cancallforward"))
			confp->chan.cancallforward = ast_true(v->value);
		else if (!strcasecmp(v->name, "relaxdtmf"))
		{
			if (ast_true(v->value))
				confp->chan.dtmfrelax = DSP_DIGITMODE_RELAXDTMF;
			else
				confp->chan.dtmfrelax = 0;
		}
		else if (!strcasecmp(v->name, "adsi"))
			confp->chan.adsi = ast_true(v->value);
		else if (!strcasecmp(v->name, "transfer"))
			confp->chan.transfer = ast_true(v->value);
		else if (!strcasecmp(v->name, "canpark"))
			confp->chan.canpark = ast_true(v->value);
		else if (!strcasecmp(v->name, "echocancelwhenbridged"))
			confp->chan.echocanbridged = ast_true(v->value);
		else if (!strcasecmp(v->name, "busydetect"))
			confp->chan.busydetect = ast_true(v->value);
		else if (!strcasecmp(v->name, "busycount"))
			confp->chan.busycount = atoi(v->value);
		else if (!strcasecmp(v->name, "busypattern"))
		{}
		else if (!strcasecmp(v->name, "callprogress"))
		{
			confp->chan.callprogress &= ~CALLPROGRESS_PROGRESS;
			if (ast_true(v->value))
				confp->chan.callprogress |= CALLPROGRESS_PROGRESS;
		}
		else if (!strcasecmp(v->name, "faxdetect"))
		{
			confp->chan.callprogress &= ~CALLPROGRESS_FAX;
			if (!strcasecmp(v->value, "incoming"))
				confp->chan.callprogress |= CALLPROGRESS_FAX_INCOMING;
			else if (!strcasecmp(v->value, "outgoing"))
				confp->chan.callprogress |= CALLPROGRESS_FAX_OUTGOING;
			else if (!strcasecmp(v->value, "both") || ast_true(v->value))
				confp->chan.callprogress |= CALLPROGRESS_FAX_INCOMING | CALLPROGRESS_FAX_OUTGOING;
		}
		else if (!strcasecmp(v->name, "echocancel"))
			process_echocancel(confp, v->value, v->lineno);
		else if (!strcasecmp(v->name, "echotraining"))
		{
			if (sscanf(v->value, "%30d", &y) == 1)
			{
				if ((y < 10) || (y > 4000))
					ast_log(LOG_WARNING, "Echo training time must be within the range of 10 to 4000 ms at line %d.\n", v->lineno);
				else
					confp->chan.echotraining = y;
			}
			else if (ast_true(v->value))
				confp->chan.echotraining = 400;
			else
				confp->chan.echotraining = 0;
		}
		else if (!strcasecmp(v->name, "hidecallerid"))
			confp->chan.hidecallerid = ast_true(v->value);
		else if (!strcasecmp(v->name, "hidecalleridname"))
			confp->chan.hidecalleridname = ast_true(v->value);
		else if (!strcasecmp(v->name, "pulsedial"))
 			confp->chan.pulse = ast_true(v->value);
		else if (!strcasecmp(v->name, "callreturn"))
			confp->chan.callreturn = ast_true(v->value);
		else if (!strcasecmp(v->name, "callwaiting"))
			confp->chan.callwaiting = ast_true(v->value);
		else if (!strcasecmp(v->name, "callwaitingcallerid"))
			confp->chan.callwaitingcallerid = ast_true(v->value);
		else if (!strcasecmp(v->name, "context"))
			ast_copy_string(confp->chan.context, v->value, sizeof(confp->chan.context));
		else if (!strcasecmp(v->name, "language"))
			ast_copy_string(confp->chan.language, v->value, sizeof(confp->chan.language));
		else if (!strcasecmp(v->name, "progzone"))
			ast_copy_string(progzone, v->value, sizeof(progzone));
		else if (!strcasecmp(v->name, "mohinterpret") ||!strcasecmp(v->name, "musiconhold") || !strcasecmp(v->name, "musicclass"))
			ast_copy_string(confp->chan.mohinterpret, v->value, sizeof(confp->chan.mohinterpret));
		else if (!strcasecmp(v->name, "mohsuggest"))
			ast_copy_string(confp->chan.mohsuggest, v->value, sizeof(confp->chan.mohsuggest));
		else if (!strcasecmp(v->name, "parkinglot"))
			ast_copy_string(confp->chan.parkinglot, v->value, sizeof(confp->chan.parkinglot));
		else if (!strcasecmp(v->name, "stripmsd"))
		{
			ast_log(LOG_NOTICE, "Configuration option \"%s\" has been deprecated. Please use dialplan instead\n", v->name);
			confp->chan.stripmsd = atoi(v->value);
		}
		else if (!strcasecmp(v->name, "jitterbuffers"))
			numbufs = atoi(v->value);
		else if (!strcasecmp(v->name, "group"))
			confp->chan.group = ast_get_group(v->value);
		else if (!strcasecmp(v->name, "setvar"))
		{
			char *varname = ast_strdupa(v->value), *varval = NULL;
			struct ast_variable *tmpvar;
			if (varname && (varval = strchr(varname, '=')))
			{
				*varval++ = '\0';
				if ((tmpvar = ast_variable_new(varname, varval, "")))
				{
					tmpvar->next = confp->chan.vars;
					confp->chan.vars = tmpvar;
				}
			}
		}
		else if (!strcasecmp(v->name, "immediate"))
			confp->chan.immediate = ast_true(v->value);
		else if (!strcasecmp(v->name, "transfertobusy"))
			confp->chan.transfertobusy = ast_true(v->value);
		else if (!strcasecmp(v->name, "cid_rxgain"))
		{
			if (sscanf(v->value, "%30f", &confp->chan.cid_rxgain) != 1)
				ast_log(LOG_WARNING, "Invalid cid_rxgain: %s at line %d.\n", v->value, v->lineno);
		}
		else if (!strcasecmp(v->name, "rxgain"))
		{
			if (sscanf(v->value, "%30f", &confp->chan.rxgain) != 1)
				ast_log(LOG_WARNING, "Invalid rxgain: %s at line %d.\n", v->value, v->lineno);
		}
		else if (!strcasecmp(v->name, "txgain"))
		{
			if (sscanf(v->value, "%30f", &confp->chan.txgain) != 1)
				ast_log(LOG_WARNING, "Invalid txgain: %s at line %d.\n", v->value, v->lineno);
		}
		else if (!strcasecmp(v->name, "txdrc"))
		{
			if (sscanf(v->value, "%f", &confp->chan.txdrc) != 1)
				ast_log(LOG_WARNING, "Invalid txdrc: %s\n", v->value);
		}
		else if (!strcasecmp(v->name, "rxdrc"))
		{
			if (sscanf(v->value, "%f", &confp->chan.rxdrc) != 1)
				ast_log(LOG_WARNING, "Invalid rxdrc: %s\n", v->value);
		}
		else if (!strcasecmp(v->name, "tonezone"))
		{
			if (sscanf(v->value, "%30d", &confp->chan.tonezone) != 1)
				ast_log(LOG_WARNING, "Invalid tonezone: %s at line %d.\n", v->value, v->lineno);
		}
		else if (!strcasecmp(v->name, "callerid"))
		{
			if (!strcasecmp(v->value, "asreceived"))
			{
				confp->chan.cid_num[0] = '\0';
				confp->chan.cid_name[0] = '\0';
			}
			else
				ast_callerid_split(v->value, confp->chan.cid_name, sizeof(confp->chan.cid_name), confp->chan.cid_num, sizeof(confp->chan.cid_num));
		}
		else if (!strcasecmp(v->name, "fullname"))
			ast_copy_string(confp->chan.cid_name, v->value, sizeof(confp->chan.cid_name));
		else if (!strcasecmp(v->name, "cid_number"))
			ast_copy_string(confp->chan.cid_num, v->value, sizeof(confp->chan.cid_num));
		else if (!strcasecmp(v->name, "cid_tag"))
			ast_copy_string(confp->chan.cid_tag, v->value, sizeof(confp->chan.cid_tag));
		else if (!strcasecmp(v->name, "restrictcid"))
			confp->chan.restrictcid = ast_true(v->value);
	    else if (!strcasecmp(v->name, "usecallingpres"))
			confp->chan.use_callingpres = ast_true(v->value);
		else if (!strcasecmp(v->name, "accountcode"))
			ast_copy_string(confp->chan.accountcode, v->value, sizeof(confp->chan.accountcode));
	     else if (!strcasecmp(v->name, "amaflags"))
		 {}
		 else if (!strcasecmp(v->name, "polarityonanswerdelay"))
			confp->chan.polarityonanswerdelay = atoi(v->value);
		 else if (!strcasecmp(v->name, "answeronpolarityswitch"))
			confp->chan.answeronpolarityswitch = ast_true(v->value);
		 else if (!strcasecmp(v->name, "hanguponpolarityswitch"))
			confp->chan.hanguponpolarityswitch = ast_true(v->value);
         else if (ast_cc_is_config_param(v->name))
			ast_cc_set_param(confp->chan.cc_params, v->name, v->value);
         else if (reload != 1)
         {
			 if (!strcasecmp(v->name, "signalling") || !strcasecmp(v->name, "signaling"))
			 {
				int orig_radio = confp->chan.radio;
				int orig_outsigmod = confp->chan.outsigmod;
				int orig_auto = confp->is_sig_auto;
				confp->chan.radio = 0;
				confp->chan.outsigmod = -1;
				confp->is_sig_auto = 0;
				if (!strcasecmp(v->value, "gsm"))
				{
					confp->chan.sig = SIG_GSM;
                   confp->gsm.gsm.conf.nodetype=GSM_CPE;
				}
				else if (!strcasecmp(v->value, "auto"))
					confp->is_sig_auto = 1;
                else if (!strcasecmp(v->value, "fxs_ks"))
                {
					confp->chan.sig = SIG_GSM;
                    confp->gsm.gsm.conf.nodetype=GSM_CPE;
				}
                else
                {
					confp->chan.outsigmod = orig_outsigmod;
					confp->chan.radio = orig_radio;
					confp->is_sig_auto = orig_auto;
					ast_log(LOG_ERROR, "Unknown signalling method '%s' at line %d.\n", v->value, v->lineno);
				}
			 }
			 else if (!strcasecmp(v->name, "switchtype"))
			 {
				if (!strcasecmp(v->value, "m50"))
					confp->gsm.gsm.conf.switchtype = 1;//jwj must define m50
				else
				{
					ast_log(LOG_ERROR, "Unknown switchtype '%s' at line %d.\n", v->value, v->lineno);
					return -1;
				}
			 }
			  else if(!strcasecmp(v->name, "pinnum"))
			   ast_copy_string(confp->gsm.gsm.conf.pinnum, v->value, sizeof(confp->gsm.gsm.conf.pinnum));
			  else if (!strcasecmp(v->name, "resetinterval"))
			  {
				if (!strcasecmp(v->value, "never"))
					confp->gsm.gsm.resetinterval = -1;
				else if (atoi(v->value) >= 60)
					confp->gsm.gsm.resetinterval = atoi(v->value);
				else
					ast_log(LOG_WARNING, "'%s' is not a valid reset interval, should be >= 60 seconds or 'never' at line %d.\n",
						v->value, v->lineno);
			   }
               else if(!strcasecmp(v->name, "countrycode"))
                   ast_copy_string(confp->gsm.gsm.conf.countrycode, v->value, sizeof(confp->gsm.gsm.conf.countrycode));
               else if(!strcasecmp(v->name, "numtype"))
                   ast_copy_string(confp->gsm.gsm.conf.numtype, v->value, sizeof(confp->gsm.gsm.conf.numtype));
               else if(!strcasecmp(v->name, "sms_lang"))
                   ast_copy_string(confp->gsm.gsm.conf.sms_lang, v->value, sizeof(confp->gsm.gsm.conf.sms_lang));
               else if (!strcasecmp(v->name, "hold_disconnect_transfer"))
				confp->gsm.gsm.hold_disconnect_transfer = ast_true(v->value);
               else if (!strcasecmp(v->name, "toneduration"))
               {
 				 int toneduration;
				 int ctlfd;
				 int res;
				 struct dahdi_dialparams dps;
				 ctlfd = open("/dev/dahdi/ctl", O_RDWR);
				 if (ctlfd == -1)
				 {
					ast_log(LOG_ERROR, "Unable to open /dev/dahdi/ctl to set toneduration at line %d.\n", v->lineno);
					return -1;
				 }
				 toneduration = atoi(v->value);
				 if (toneduration > -1)
				 {
					memset(&dps, 0, sizeof(dps));
					dps.dtmf_tonelen = dps.mfv1_tonelen = toneduration;
					res = ioctl(ctlfd, DAHDI_SET_DIALPARAMS, &dps);
					if (res < 0)
					{
						ast_log(LOG_ERROR, "Invalid tone duration: %d ms at line %d: %s\n", toneduration, v->lineno, strerror(errno));
						close(ctlfd);
						return -1;
					}
				  }
				  close(ctlfd);
			    }
                else if (!strcasecmp(v->name, "dtmfcidlevel"))
				   dtmfcid_level = atoi(v->value);
			    else if (!strcasecmp(v->name, "reportalarms"))
			    {
				  if (!strcasecmp(v->value, "all"))
					report_alarms = REPORT_CHANNEL_ALARMS | REPORT_SPAN_ALARMS;
				  if (!strcasecmp(v->value, "none"))
					  report_alarms = 0;
				  else if (!strcasecmp(v->value, "channels"))
					report_alarms = REPORT_CHANNEL_ALARMS;
			       else if (!strcasecmp(v->value, "spans"))
					 report_alarms = REPORT_SPAN_ALARMS;
			    }
		}
         else if (!(options & PROC_GCOM_OPT_NOWARN) )
			ast_log(LOG_WARNING, "Ignoring any changes to '%s' (on reload) at line %d.\n", v->name, v->lineno);
	}
	if (confp->chan.vars)
	{
		ast_variables_destroy(confp->chan.vars);
		confp->chan.vars = NULL;
	}
	if (gcomchan) //users.conf for pbx
	{
		/* Process the deferred gcomchan value. */
		if (build_channels(confp, gcomchan->value, reload, gcomchan->lineno,&found_pseudo))
		{
			if (confp->ignore_failed_channels)
				ast_log(LOG_WARNING,"Dahdichan '%s' failure ignored: ignore_failed_channels.\n",gcomchan->value);
			else
			{
			        ast_log(LOG_WARNING, "jwj process gcom and gcomchan lineno=%d\n",gcomchan->lineno);
				return -1;
			}
		}
	}
	/* mark the first channels of each DAHDI span to watch for their span alarms */
	for (tmp = iflist, y=-1; tmp; tmp = tmp->next)
	{
		if (!tmp->destroy && tmp->span != y)
		{
			tmp->manages_span_alarms = 1;
			y = tmp->span; 
		}
		else
			tmp->manages_span_alarms = 0;
	}

	/*< \todo why check for the pseudo in the per-channel section.
	 * Any actual use for manual setup of the pseudo channel? */
	if (!found_pseudo && reload != 1 && !(options & PROC_GCOM_OPT_NOCHAN))
	{
		/* use the default configuration for a channel, so
		   that any settings from real configured channels
		   don't "leak" into the pseudo channel config
		*/
		struct gcom_chan_conf conf = gcom_chan_conf_default();
		if (conf.chan.cc_params)
			tmp = mkintf(CHAN_PSEUDO, &conf, reload);
		else
			tmp = NULL;
		if (tmp)
			ast_verb(3, "Automatically generated pseudo channel\n");
		else
			ast_log(LOG_WARNING, "Unable to register pseudo channel!\n");
		ast_cc_config_params_destroy(conf.chan.cc_params);
	}
	return 0;
 }

/*!
 * \internal
 * \brief Deep copy struct gcom_chan_conf.
 * \since 1.8
 *
 * \param dest Destination.
 * \param src Source.
 *
 * \return Nothing
 */
 static void deep_copy_gcom_chan_conf(struct gcom_chan_conf *dest, const struct gcom_chan_conf *src)
 {
	struct ast_cc_config_params *cc_params;
	cc_params = dest->chan.cc_params;
	*dest = *src;
	dest->chan.cc_params = cc_params;
	ast_cc_copy_config_params(dest->chan.cc_params, src->chan.cc_params);
 }

/*!
 * \internal
 * \brief Setup DAHDI channel driver.
 *
 * \param reload enum: load_module(0), reload(1), restart(2).
 * \param default_conf Default config parameters.  So cc_params can be properly destroyed.
 * \param base_conf Default config parameters per section.  So cc_params can be properly destroyed.
 * \param conf Local config parameters.  So cc_params can be properly destroyed.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
 static int setup_gcom_int(int reload, struct gcom_chan_conf *default_conf, struct gcom_chan_conf *base_conf, struct gcom_chan_conf *conf)
 {
	struct ast_config *cfg;
	struct ast_config *ucfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload == 1 ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *chans;
	const char *cat;
	int res = 0;
	char *c;
	int spanno = 0;
	int i = 0;
	int logicalspan = 0;
	int trunkgroup = 0;
	int dchannel = 0;
	int have_cfg_now;
	static int had_cfg_before = 1;/* So initial load will complain if we don't have cfg. */
	cfg = ast_config_load(config, config_flags);//1
	have_cfg_now = !!cfg;
	if (!cfg)
	{
		/* Error if we have no config file */
		if (had_cfg_before)
		{
			ast_log(LOG_ERROR, "Unable to load config %s\n", config);
			ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		}
		cfg = ast_config_new();/* Dummy config */
		if (!cfg)
			return 0;
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED)
		{
			ast_config_destroy(cfg);
			return 0;
		}
		if (ucfg == CONFIG_STATUS_FILEINVALID)
		{
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
	}
	else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
	{
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED)
			return 0;
		if (ucfg == CONFIG_STATUS_FILEINVALID)
		{
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			return 0;
		}
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		cfg = ast_config_load(config, config_flags);
		have_cfg_now = !!cfg;
		if (!cfg)
		{
			if (had_cfg_before)
			{
				/* We should have been able to load the config. */
				ast_log(LOG_ERROR, "Bad. Unable to load config %s\n", config);
				ast_config_destroy(ucfg);
				return 0;
			}
			cfg = ast_config_new();/* Dummy config */
			if (!cfg)
			{
				ast_config_destroy(ucfg);
				return 0;
			}
		}
        else if (cfg == CONFIG_STATUS_FILEINVALID)
        {
			ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
			ast_config_destroy(ucfg);
			return 0;
		}
	}
    else if (cfg == CONFIG_STATUS_FILEINVALID)
    {
		ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
		return 0;
	}
    else
    {
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEINVALID)
		{
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
	}
	had_cfg_before = have_cfg_now;
	/* It's a little silly to lock it, but we might as well just to be sure */
	ast_mutex_lock(&iflock);
	if (reload != 1)
	{
		/* Process trunkgroups first */
		v = ast_variable_browse(cfg, "trunkgroups");
		while (v)
		{
			if (!strcasecmp(v->name, "trunkgroup"))
			{
				trunkgroup = atoi(v->value);
				if (trunkgroup > 0)
				{
					if ((c = strchr(v->value, ',')))
					{
						i = 0;
						while (c && (i < SIG_GSM_NUM_DCHANS)) 
                        {
							dchannel = atoi(c + 1);
							if (dchannel < 0)
								ast_log(LOG_WARNING, "D-channel for trunk group %d must be a postiive number at line %d of chan_gcom.conf\n", trunkgroup, v->lineno);
							else
								i++;
							c = strchr(c + 1, ',');
						}
						if (i)
						{
						  if (gsm_create_trunkgroup(trunkgroup, &dchannel))
							ast_log(LOG_WARNING, "Unable to create trunk group %d with Primary D-channel %d at line %d of chan_gcom.conf\n", trunkgroup, dchannel, v->lineno);
						  else
							  ast_log(LOG_DEBUG, "Created trunk group %d with Primary D-channel %d and %d backup%s\n", trunkgroup, dchannel, i - 1, (i == 1) ? "" : "s");
						}
						else
							ast_log(LOG_WARNING, "Trunk group %d lacks any valid D-channels at line %d of chan_gcom.conf\n", trunkgroup, v->lineno);
					}
					else
					ast_log(LOG_WARNING, "Trunk group %d lacks a primary D-channel at line %d of chan_gcom.conf\n", trunkgroup, v->lineno);
				}
				else
				ast_log(LOG_WARNING, "Trunk group identifier must be a positive integer at line %d of chan_gcom.conf\n", v->lineno);
			}
			else if (!strcasecmp(v->name, "spanmap"))
			{
				spanno = atoi(v->value);
				if (spanno > 0)
				{
					if ((c = strchr(v->value, ',')))
					{
						trunkgroup = atoi(c + 1);
						if (trunkgroup > 0)
						{
							if ((c = strchr(c + 1, ',')))
								logicalspan = atoi(c + 1);
							else
								logicalspan = 0;
							if (logicalspan >= 0)
							{
								if (gsm_create_spanmap(spanno - 1, trunkgroup, logicalspan))
									ast_log(LOG_WARNING, "Failed to map span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
							     else
									ast_verb(2, "Mapped span %d to trunk group %d (logical span %d)\n", spanno, trunkgroup, logicalspan);
							}
							else
							  ast_log(LOG_WARNING, "Logical span must be a postive number, or '0' (for unspecified) at line %d of chan_gcom.conf\n", v->lineno);
						}
						else
						  ast_log(LOG_WARNING, "Trunk group must be a postive number at line %d of chan_gcom.conf\n", v->lineno);
					}
					else
						ast_log(LOG_WARNING, "Missing trunk group for span map at line %d of chan_gcom.conf\n", v->lineno);
				}
				else
					ast_log(LOG_WARNING, "Span number must be a postive integer at line %d of chan_gcom.conf\n", v->lineno);
			}
			else
				ast_log(LOG_NOTICE, "Ignoring unknown keyword '%s' in trunkgroups\n", v->name);
			v = v->next;
		}
	}
	memcpy(&global_jbconf, &default_jbconf, sizeof(global_jbconf));
	v = ast_variable_browse(cfg, "channels");
	if ((res = process_gcom(base_conf,"" ,v, reload, 0)))
	{
		ast_mutex_unlock(&iflock);
		ast_config_destroy(cfg);
		if (ucfg)
			ast_config_destroy(ucfg);
		return res;
	}
	for (cat = ast_category_browse(cfg, NULL); cat ; cat = ast_category_browse(cfg, cat))
	{
		/* [channels] and [trunkgroups] are used. Let's also reserve
		 * [globals] and [general] for future use
		 */
		if (!strcasecmp(cat, "general") ||
			!strcasecmp(cat, "trunkgroups") ||
			!strcasecmp(cat, "globals") ||
			!strcasecmp(cat, "channels")) {
			continue;
		}
		chans = ast_variable_retrieve(cfg, cat, "gcomchan");
		if (ast_strlen_zero(chans))
			continue;
		deep_copy_gcom_chan_conf(conf, base_conf);
		if ((res = process_gcom(conf, cat, ast_variable_browse(cfg, cat), reload, PROC_GCOM_OPT_NOCHAN)))
		{
			ast_mutex_unlock(&iflock);
			ast_config_destroy(cfg);
			if (ucfg)
				ast_config_destroy(ucfg);
			return res;
		}
	}
	ast_config_destroy(cfg);
	if (ucfg)
	{
	    deep_copy_gcom_chan_conf(base_conf, default_conf);
    	process_gcom(base_conf,
		"" /* Must be empty for the general category.  Silly voicemail mailbox. */,
			ast_variable_browse(ucfg, "general"), 1, 0);
		for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat))
		{
			if (!strcasecmp(cat, "general"))
				continue;
			chans = ast_variable_retrieve(ucfg, cat, "gcomchan");
			if (ast_strlen_zero(chans))
				continue; /* Section is useless without a gcomchan value present. */
			/* Copy base_conf to conf. */
			deep_copy_gcom_chan_conf(conf, base_conf);
			if ((res = process_gcom(conf, cat, ast_variable_browse(ucfg, cat), reload, PROC_GCOM_OPT_NOCHAN | PROC_GCOM_OPT_NOWARN)))
			{
				ast_config_destroy(ucfg);
				ast_mutex_unlock(&iflock);
				return res;
			}
		}
		ast_config_destroy(ucfg);
	}
	ast_mutex_unlock(&iflock);
	if (reload != 1)
    {
		int x;
		for (x = 0; x < NUM_SPANS; x++) 
        {
			if (gsms[x].gsm.pvts) 
            {
               if(prepare_gsm(gsms + x))
               {
                 ast_log(LOG_ERROR, "Unable to prepare_gsm on span %d\n", x + 1);
                 return -1;
               }
               init_gsm_spans(&gsms[x].gsm);
               init_sig_gsm_span(&gsms[x].gsm.conf, gsms[x].gsm.span_id);//SPANID
               gsms[x].gsm.load = reload;
			   if (sig_gsm_start_gsm(&gsms[x].gsm))
               {
					ast_log(LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
               }
               else
				ast_verb(2, "Starting D-Channel on span %d\n", x + 1);
			}
		}
	}
	/* And start the monitor for the first time */
	restart_monitor();
	return 0;
 }

/*!
 * \internal
 * \brief Setup DAHDI channel driver.
 *
 * \param reload enum: load_module(0), reload(1), restart(2).
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
 static int setup_gcom(int reload)
 {
	int res = 0;
	struct gcom_chan_conf default_conf = gcom_chan_conf_default();
	struct gcom_chan_conf base_conf = gcom_chan_conf_default();
	struct gcom_chan_conf conf = gcom_chan_conf_default();
	if (default_conf.chan.cc_params && base_conf.chan.cc_params && conf.chan.cc_params)
		res = setup_gcom_int(reload, &default_conf, &base_conf, &conf);
	else
		res = -1;
	ast_cc_config_params_destroy(default_conf.chan.cc_params);
	ast_cc_config_params_destroy(base_conf.chan.cc_params);
	ast_cc_config_params_destroy(conf.chan.cc_params);
	return res;
 }

 static int gcom_status_data_provider_get(const struct ast_data_search *search,struct ast_data *data_root)
 {
	int ctl, res, span;
	struct ast_data *data_span, *data_alarms;
	struct dahdi_spaninfo s;
	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0)
	{
		ast_log(LOG_ERROR, "No DAHDI found. Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		return -1;
	}
	for (span = 1; span < DAHDI_MAX_SPANS; ++span)
	{
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res)
			continue;
		data_span = ast_data_add_node(data_root, "span");
		if (!data_span)
			continue;
		ast_data_add_str(data_span, "description", s.desc);
		/* insert the alarms status */
		data_alarms = ast_data_add_node(data_span, "alarms");
		if (!data_alarms)
			continue;
		ast_data_add_bool(data_alarms, "BLUE", s.alarms & DAHDI_ALARM_BLUE);
		ast_data_add_bool(data_alarms, "YELLOW", s.alarms & DAHDI_ALARM_YELLOW);
		ast_data_add_bool(data_alarms, "RED", s.alarms & DAHDI_ALARM_RED);
		ast_data_add_bool(data_alarms, "LOOPBACK", s.alarms & DAHDI_ALARM_LOOPBACK);
		ast_data_add_bool(data_alarms, "RECOVER", s.alarms & DAHDI_ALARM_RECOVER);
		ast_data_add_bool(data_alarms, "NOTOPEN", s.alarms & DAHDI_ALARM_NOTOPEN);
		ast_data_add_int(data_span, "irqmisses", s.irqmisses);
		ast_data_add_int(data_span, "bpviol", s.bpvcount);
		ast_data_add_int(data_span, "crc4", s.crc4count);
		ast_data_add_str(data_span, "framing",	s.lineconfig & DAHDI_CONFIG_D4 ? "D4" :
							s.lineconfig & DAHDI_CONFIG_ESF ? "ESF" :
							s.lineconfig & DAHDI_CONFIG_CCS ? "CCS" :
							"CAS");
		ast_data_add_str(data_span, "coding",	s.lineconfig & DAHDI_CONFIG_B8ZS ? "B8ZS" :
							s.lineconfig & DAHDI_CONFIG_HDB3 ? "HDB3" :
							s.lineconfig & DAHDI_CONFIG_AMI ? "AMI" :
							"Unknown");
		ast_data_add_str(data_span, "options",	s.lineconfig & DAHDI_CONFIG_CRC4 ?
							s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" :
							s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "YEL" : "");
		ast_data_add_str(data_span, "lbo", lbostr[s.lbo]);

		/* if this span doesn't match remove it. */
		if (!ast_data_search_match(search, data_span))
			ast_data_remove_node(data_root, data_span);

	}
	close(ctl);
	return 0;
 }


/*!
 * \internal
 * \brief Callback used to generate the dahdi channels tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
 static int gcom_channels_data_provider_get(const struct ast_data_search *search,struct ast_data *data_root)
 {
	struct gcom_pvt *tmp;
	struct ast_data *data_channel;
	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next)
	{
		data_channel = ast_data_add_node(data_root, "channel");
		if (!data_channel)
			continue;
		ast_data_add_structure(gcom_pvt, data_channel, tmp);
		/* if this channel doesn't match remove it. */
		if (!ast_data_search_match(search, data_channel))
			ast_data_remove_node(data_root, data_channel);
	}
	ast_mutex_unlock(&iflock);
	return 0;
 }

/*!
 * \internal
 * \brief Callback used to generate the dahdi channels tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
 static int gcom_version_data_provider_get(const struct ast_data_search *search,	struct ast_data *data_root)
 {
	int pseudo_fd = -1;
	struct dahdi_versioninfo vi = {
		.version = "Unknown",
		.echo_canceller = "Unknown"
	};
	if ((pseudo_fd = open("/dev/dahdi/ctl", O_RDONLY)) < 0) {
		ast_log(LOG_ERROR, "Failed to open control file to get version.\n");
		return -1;
	}
	if (ioctl(pseudo_fd, DAHDI_GETVERSION, &vi))
		ast_log(LOG_ERROR, "Failed to get DAHDI version: %s\n", strerror(errno));
	close(pseudo_fd);
	ast_data_add_str(data_root, "value", vi.version);
	ast_data_add_str(data_root, "echocanceller", vi.echo_canceller);
	return 0;
 }

 static const struct ast_data_handler gcom_status_data_provider =
 {
	.version = AST_DATA_HANDLER_VERSION,
	.get = gcom_status_data_provider_get
 };

 static const struct ast_data_handler gcom_channels_data_provider =
 {
	.version = AST_DATA_HANDLER_VERSION,
	.get = gcom_channels_data_provider_get
 };

 static const struct ast_data_handler gcom_version_data_provider =
 {
	.version = AST_DATA_HANDLER_VERSION,
	.get = gcom_version_data_provider_get
 };

  static const struct ast_data_entry gcom_data_providers[] =
  {
	AST_DATA_ENTRY("asterisk/channel/dahdi/status", &gcom_status_data_provider),
	AST_DATA_ENTRY("asterisk/channel/dahdi/channels", &gcom_channels_data_provider),
	AST_DATA_ENTRY("asterisk/channel/dahdi/version", &gcom_version_data_provider)
 };


 static int sendsms_exec(struct ast_channel *ast, const char *data)
 {
    char *parse = NULL;
    char* cmd = "SendSMS(Span, Phone, Content)";
    int span;
    char dest[512] = {0};
    char sms[1024] = {0};
    AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(Span);
		AST_APP_ARG(Phone);
		AST_APP_ARG(Content);
	);
    if (ast_strlen_zero((char *) data))
    {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
    parse = ast_strdupa(data);
    AST_STANDARD_APP_ARGS(args, parse);
    if (ast_strlen_zero(args.Span))
    {
		ast_log(LOG_WARNING, "sendsms app span no is zero\n");
		return -1;
	}
    span = atoi(args.Span);
    if(( (span)  < 1 ) || (span > NUM_SPANS))
    {
        ast_log(LOG_WARNING, "sendsms app span no is invalid\n");
        return -1;
    }
    if (ast_strlen_zero(args.Phone)) {
		ast_log(LOG_WARNING, "sendsms app Phone is zero\n");
		return -1;
	}
    memcpy(dest, args.Phone, sizeof(dest));
    if (ast_strlen_zero(args.Content))
    {
		ast_log(LOG_WARNING, "sendsms app Content is zero\n");
		return -1;
	}
    memcpy(sms, args.Content, sizeof(sms));
    if (!gsms[span-1].gsm.span_id)
    {
               ast_log(LOG_WARNING, "sendsms app gsm span =%d  is invalid\n", span);
               return -1;
    }
    process_sendsms(span, dest, sms, 0);
    return 0;
 }


 static int unload_module(void)
 {
    int y = 0;
	for (y = 0; y < NUM_SPANS; y++)
		ast_mutex_destroy(&gsms[y].gsm.lock);
	return __unload_module();
       return 0;
 }

 static void *process_sms_thread(void *data)
 {
    struct queue_sms *q_sms = (struct queue_sms*)data;
    int i = 0;
    int span = 0;
    int times = 0;
    if(!q_sms)
        return NULL;
    span = q_sms->spanid;
    while(i < q_sms->idx)
    {
       gsms[span-1].gsm.sms_stats = 0;
       process_sendsms(span,(char*) q_sms->context, q_sms->called[i], 1);
       i++;
       times = 0;
       while(!gsms[span-1].gsm.sms_stats)
       {
         sleep(1);
         times++;
         if(times >= 20)
              break;
        }
      }
     if(q_sms)
        ast_free(q_sms);
     return NULL;
 }

static int read_sms_file(char *fn, struct ast_xml_doc *p_xml)
{
        struct ast_xml_node *root_node = NULL;
        struct ast_xml_node *node = NULL;
        char called[GSM_MAX_SMS_QUEUE_NUM][GSM_CALL_NUM_LEN];
        unsigned char context[ GSM_MAX_CONTENT_LEN ] = {0};
        int  span = 0;
        int idx = 0;
        int i = 0;
        const char *tmp;
        const char *tmp1;
        int len = 0;
        int res = 0;
        struct gsm_sms_event_data event;
        struct queue_sms *q_sms;
        pthread_t t;
        memset(&event, 0, sizeof(event));
        if(!p_xml)
        {
             if(fn)
		       unlink(fn);
            return -1;
        }
        for(idx = 0; idx < GSM_MAX_SMS_QUEUE_NUM; idx++)
            memset(called[idx], 0, sizeof(called[idx]));
        idx = 0;
        root_node = ast_xml_get_root(p_xml);
        if(!root_node)
        {
           if(fn)
		      unlink(fn);
	       if(p_xml) 
              ast_xml_close(p_xml);
            return -1;
        }
        tmp = ast_xml_node_get_name(root_node);
        if(tmp)
        {
              if(strcasecmp(tmp,"sms"))
              {
                 ast_log(LOG_NOTICE,"parse xml file,find no item of sms,send sms failed\n");
                 if(fn)  unlink(fn);
	             if(p_xml) ast_xml_close(p_xml);
                 sig_gsm_sms_fail_log("parse xml file,find no item of sms\n");
                 return -1;
              }
         } 
        else
        {
               ast_log(LOG_NOTICE,"parse xml file,find no root item,send sms failed \n");
               if(fn)  unlink(fn);
	           if(p_xml) ast_xml_close(p_xml);
               sig_gsm_sms_fail_log("parse xml file,find no root item\n");
               return -1;
        }
        node = ast_xml_node_get_children(root_node);
        while(node != NULL)
        {
              node = ast_xml_find_element(node,"receiver",NULL,NULL);
              if(node)
              {
                 tmp = ast_xml_get_text(node);
                 if(tmp)
                 {
                    if(idx < GSM_MAX_SMS_QUEUE_NUM)
                    {
                       ast_log(LOG_NOTICE,"pbx_spool find called num=%s\n",tmp);
                       ast_copy_string(called[idx], tmp, GSM_CALL_NUM_LEN);
                       idx++;
                    }
                    ast_xml_free_text(tmp);
                 }
                 node = ast_xml_node_get_next(node);
              }
          }
          if(idx == 0)
          {
                ast_log(LOG_NOTICE,"pbx_spool find no called num\n");
                if(fn) unlink(fn);
 	            if(p_xml)  ast_xml_close(p_xml);
                sig_gsm_sms_fail_log("parse xml file,find no called num\n");
                return -1;
          }
          node = ast_xml_node_get_children(root_node);
          node = ast_xml_find_element(node,"content",NULL,NULL);
          if(node)
          {
              tmp = ast_xml_get_text(node);
              if(tmp)
              {
                  tmp1 = ast_strip((char*)tmp);
                  len = strlen(tmp1);
                  if(len  >  512)
                  {
                      ast_log(LOG_NOTICE,"the content length of the xml file is too long len=%d exceed 512,send sms failed\n",len);
                      ast_xml_free_text(tmp);
                      if(fn) unlink(fn);
  	                  if(p_xml) ast_xml_close(p_xml);
                      sig_gsm_sms_fail_log("parse xml file,the content length of the xml file is too long,length of utf8 is exceed 512\n");
                      return -1;
                  }
                  ast_copy_string((char*)context, tmp1, sizeof(context));
                  context[len] = '\0';
                  ast_xml_free_text(tmp);
              }
           }
           else
           {
              ast_log(LOG_NOTICE,"the content length of the xml file is NULL,send sms failed\n");
              if(fn) unlink(fn);
	          if(p_xml)  ast_xml_close(p_xml);
              sig_gsm_sms_fail_log("parse xml file,the content is not found\n");
              return -1;             
           }
           node = ast_xml_node_get_children(root_node);           
           node = ast_xml_find_element(node,"span",NULL,NULL);
           if(node)
           {
              tmp = ast_xml_get_text(node);
              if(tmp)
              {
                  span = atoi(tmp);
                  ast_xml_free_text(tmp);
                  if(span <= 0 || span > GSM_MAX_SPANS)
                  {
                     ast_log(LOG_NOTICE,"pbx spool thread find xml file span is invalued,send sms failed\n");
                     if(fn)  unlink(fn);
	                 if(p_xml) ast_xml_close(p_xml);
                     sig_gsm_sms_fail_log("parse xml file,find span is invalued\n");
                     return -1;
                  } 
                  ast_log(LOG_WARNING,"pbx thread find xml file span is =%d\n",span);
              }
           }
           else
           {
              ast_log(LOG_NOTICE,"pbx spool thread find xml file span is null,send sms failed\n");
              if(fn)  unlink(fn);
	          if(p_xml) ast_xml_close(p_xml);
              sig_gsm_sms_fail_log("parse xml file,find no span\n");
              return -1;             
           }
           if (!gsms[span-1].gsm.span_id)
           {
               ast_log(LOG_WARNING, "sendsms app gsm span =%d  is invalid\n", span);
               if(fn) unlink(fn);
	           if(p_xml)  ast_xml_close(p_xml);
               sig_gsm_sms_fail_log("parse xml file,find span is invalid\n");
               return -1;
           }
 	       if(fn) unlink(fn);
	       if(p_xml) ast_xml_close(p_xml);
           q_sms = (struct queue_sms*)ast_malloc(sizeof(struct queue_sms));
           if(!q_sms)
           {
                sig_gsm_sms_fail_log("parse xml file,malloc mem is fail\n");
                return -1;
           }
          memcpy(q_sms->context, context, sizeof(q_sms->context));
          for(i = 0; i< idx; i++)
              ast_copy_string(q_sms->called[i], called[i], GSM_CALL_NUM_LEN);
          q_sms->idx = idx;
          q_sms->spanid = span;
	      if ((res = ast_pthread_create_detached(&t, NULL, process_sms_thread, q_sms)))
	      {
		    ast_log(LOG_WARNING, "Unable to create thread :( (returned error: %d)\n", res);
		    ast_free(q_sms);
          }
       return 0;    
 }


 static struct ast_xml_doc *ast_xml_open_utf8(char *filename)
 {
        xmlDoc *doc;
        if (!filename)
                return NULL;
        doc = xmlReadFile(filename, "UTF-8", XML_PARSE_RECOVER);
        if (doc)
        {
                /* process xinclude elements. */
                if (xmlXIncludeProcess(doc) < 0)
                {
                        xmlFreeDoc(doc);
                        return NULL;
                }
        }
        return (struct ast_xml_doc *) doc;
 }


 static int scan_service(const char *fn)
 {
     struct ast_xml_doc *p_xml = NULL;
     if(strstr(fn,".xml"))
     {
           p_xml = ast_xml_open_utf8((char*)fn);//ast_xml_open(fn);           
           //p_xml = ast_xml_open(fn);
           if(!p_xml)
           {        
               ast_log(LOG_WARNING," open utf8  xml file = %s fail\n",fn);
               sig_gsm_sms_fail_log("open utf8  xml file fail\n");
                if(fn)
                  unlink(fn);
              return -1;
           }
           if(read_sms_file((char*)fn,p_xml))
           {
              ast_xml_close(p_xml);
		      ast_log(LOG_WARNING, "Invalid file contents in %s, deleting\n", fn);
		      return -1;
           }
    }
    return 0;
}


 static void *sms_scan_thread(void *data)
 {
    struct stat st;
	DIR *dir;
	struct dirent *de;
	char fn[256] = {0};
	int res;
	time_t last = 0, next = 0, now;
	struct timespec ts = { .tv_sec = 2 };
    for(;;)
    {
		/* Wait a sec */
		nanosleep(&ts, NULL);
		time(&now);
		if (stat(qdir, &st))
		{
			ast_log(LOG_WARNING, "Unable to stat %s\n", qdir);
			continue;
		}
		/* Make sure it is time for us to execute our check */
		if ((st.st_mtime == last) && (next && (next > now)))
			continue;
		next = 0;
		last = st.st_mtime;
		if (!(dir = opendir(qdir)))
		{
			ast_log(LOG_WARNING, "Unable to open directory %s: %s\n", qdir, strerror(errno));
			continue;
		}
		while ((de = readdir(dir)))
		{
			snprintf(fn, sizeof(fn), "%s/%s", qdir, de->d_name);
			if (stat(fn, &st))
			{
				ast_log(LOG_WARNING, "Unable to stat %s: %s\n", fn, strerror(errno));
				continue;
			}
			if (!S_ISREG(st.st_mode))
				continue;
			if (st.st_mtime <= now)
			{
				res = scan_service(fn);
				if (res > 0)
				{
					/* Update next service time */
					if (!next || (res < next))
						next = res;
				}
				else if (res)
					ast_log(LOG_WARNING, "Failed to scan service '%s'\n", fn);
			    else if (!next)
					next = st.st_mtime; /* Expired entry: must recheck on the next go-around */
			}
			else
			{
				/* Update "next" update if necessary */
				if (!next || (st.st_mtime < next))
					next = st.st_mtime;
			}
		}
		closedir(dir);
	}
   return NULL;
 }

static int load_module(void)
{
   int res;
   int y;
   res = sig_libgsm_load();
   if(res)
      return -1;
   memset(gsms, 0, sizeof(gsms));
   for (y = 0; y < NUM_SPANS; y++)
		sig_gsm_init_gsm(&gsms[y].gsm);
	res = setup_gcom(0);
    gcom_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	ast_format_cap_append(gcom_tech.capabilities, ast_format_slin, 0);
	ast_format_cap_append(gcom_tech.capabilities, ast_format_ulaw, 0);
	ast_format_cap_append(gcom_tech.capabilities, ast_format_alaw, 0);

	/* Make sure we can register our DAHDI channel type */
	if (res)
		return AST_MODULE_LOAD_DECLINE;
	if (ast_channel_register(&gcom_tech))
	{
		ast_log(LOG_ERROR, "Unable to register channel class 'GCOM'\n");
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_cli_register_multiple(gcom_gsm_cli, ARRAY_LEN(gcom_gsm_cli));
    ast_register_application(app_sendsms, sendsms_exec, sendsms_synopsis, sendsms_desc);
	ast_cli_register_multiple(gcom_cli, ARRAY_LEN(gcom_cli));
	/* register all the data providers */
	ast_data_register_multiple(gcom_data_providers, ARRAY_LEN(gcom_data_providers));//highversion
	memset(round_robin, 0, sizeof(round_robin));
	ast_cond_init(&ss_thread_complete, NULL);
    snprintf(qdir, sizeof(qdir), "%s/%s", ast_config_AST_SPOOL_DIR, "xmlsms");
	if (ast_mkdir(qdir, 0777))
	{
	  ast_log(LOG_WARNING, "Unable to create queue directory %s -- msms spool disabled\n", qdir);
	  return AST_MODULE_LOAD_DECLINE;
	}
    restart_monitor();
	return 0;
 }

  static int gcom_sendtext(struct ast_channel *c, const char *text)
  {
	return(0);
  }


 static int reload(void)
 {
	int res = 0;
	res = setup_gcom(1);
	if (res)
	{
		ast_log(LOG_WARNING, "Reload of chan_gcom.so is unsuccessful!\n");
		return -1;
	}
    restart_monitor();
    ast_log(LOG_WARNING, "Reload of ok!\n");
	return 0;
 }

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "GSM/GPRS",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
		.nonoptreq = "res_smdi",
	);
