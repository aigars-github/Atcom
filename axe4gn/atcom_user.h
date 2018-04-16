#ifndef _ATCOM_USER_H_
#define _ATCOM_USER_H_

#define DAHDI_SPAN_OPS 1


#define NUM_CARDS 8//max cards

#define AX4G_NUM_CARDS 	4
#define AX2G4A_NUM_CARDS  8
#define NUM_SYNC  4
#define voicepal 1	//add for ax2g4a
#define AX2G4A_ATCMD_CHANNELS   4
#define AX4G_ATCMD_CHANNELS   2
#define MAX_ALARMS 10
#define MOD_TYPE_UNDEF	 0
#define MOD_TYPE_FXS	1
#define MOD_TYPE_FXO	2
#define MOD_TYPE_GSM    3
	

#define MINPEGTIME	10 * 8		/* 30 ms peak to peak gets us no more than 100 Hz */
#define PEGTIME		50 * 8		/* 50ms peak to peak gets us rings of 10 Hz or more */
#define PEGCOUNT	5		/* 5 cycles of pegging means RING */

#define NUM_CAL_REGS 12

#define GSM_ADD_EVENT					0x27
#define GSM_ADD_VERSION					0x2B

#define AXGCOM_ATCMD_BUF_LEN  480
#define NUM_SPANS   4
#define AXGCOM_LINEAR_LEN 16

#define T4_IGNORE_LATENCY	5

 struct calregs
 {
	unsigned char vals[NUM_CAL_REGS];
 };

 enum proslic_power_warn
 {
	PROSLIC_POWER_UNKNOWN = 0,
	PROSLIC_POWER_ON,
	PROSLIC_POWER_WARNED,
 };

 enum battery_state
 {
	BATTERY_UNKNOWN = 0,
	BATTERY_PRESENT,
	BATTERY_LOST,
 };

 struct devtype
 {
	char *desc;
	unsigned int flags;
 };

 struct axgcom_span
 {
    struct axgcom *owner;
    struct dahdi_span span;
    volatile unsigned int *writechunk;				/* Double-word aligned write memory */
    volatile unsigned int *readchunk;
    atomic_t hdlc_pending;
 };

 struct axgcom 
 {
	struct pci_dev *dev;
	char *variety;
	struct axgcom_span axspan[NUM_CARDS];
	const struct devtype *devtype;
	struct dahdi_device *ddev;
    int power_on_flag;
    int poweron[NUM_CARDS];
    int check_start[NUM_CARDS];
    int queue_power_on_flag;
    int queue_power_reset_flag;
    int already_run;
    unsigned char realspan;
	unsigned char ios;
	int usecount;
	unsigned int intcount;
	int dead;
	int pos;
	int flags[NUM_CARDS];
	int freeregion;
	int alt;
	int curcard;
	int cardflag;		/* Bit-map of present cards */
	unsigned int HW_VER;	 //add for ax2g4a
	unsigned int FPGA_FIREWARE_VER;
	unsigned int gsm_port_num;	//add for ax2g4a
	int num_card;	//add for ax2g4a
    int sumspan;
    int numchans;
    int analog_chans;
    int spanflag;
    unsigned char offset_span;
    unsigned char span_type;
    unsigned int gpio;
    unsigned int gpioctl;
	unsigned char   last_int_val;
	unsigned char query_flag; 
   	unsigned char rx_buf[NUM_CARDS][DAHDI_CHUNKSIZE];
	unsigned char tx_buf[NUM_CARDS][DAHDI_CHUNKSIZE];
    unsigned short rx_buf_linear[NUM_CARDS][DAHDI_CHUNKSIZE];
	unsigned short tx_buf_linear[NUM_CARDS][DAHDI_CHUNKSIZE];
    unsigned char sig_rx_buf[NUM_CARDS][AXGCOM_ATCMD_BUF_LEN];
	unsigned char sig_tx_buf[NUM_CARDS][AXGCOM_ATCMD_BUF_LEN];
    unsigned short sig_rx_idex[NUM_CARDS];
    unsigned short sig_tx_idex[NUM_CARDS];
    char last_cmd[20];
	enum proslic_power_warn proslic_power;
	spinlock_t lock;
	union
	{
		struct fxo
		{
			int wasringing;
			int lastrdtx;
			int ringdebounce;
			int offhook;
			unsigned int battdebounce;
			unsigned int battalarm;
			enum battery_state battery;
            int lastpol;
	        int polarity;
	        int polaritydebounce;
            int readcid;
	        unsigned int cidtimer;
		} fxo;
		struct fxs
		{
			int oldrxhook;
			int debouncehook;
			int lastrxhook;
			int debounce;
			int ohttimer;
			int idletxhookstate;		/* IDLE changing hook state */
			int lasttxhook;
			int palarms;
			int reversepolarity;		/* Reverse Line */
			int mwisendtype;
			struct dahdi_vmwi_info vmwisetting;
			int vmwi_active_messages;
			int vmwi_lrev:1;		/* MWI Line Reversal*/
			int vmwi_hvdc:1;		/* MWI High Voltage DC Idle line */
			int vmwi_hvac:1;		/* MWI Neon High Voltage AC Idle line */
			int neonringing:1; /* Ring Generator is set for NEON */
			struct calregs calregs;
		} fxs;
        struct gsm
        {
            int sms_state;
            int chan_state;
            int call_state;
        }gsm;
   } mod[NUM_CARDS];

	/* Receive hook state and debouncing */
	int modtype[NUM_CARDS];
	unsigned char reg0shadow[NUM_CARDS];
	unsigned char reg1shadow[NUM_CARDS];
   	unsigned long memaddr;		/* Base address of card */
	unsigned long memlen;
	__iomem volatile unsigned int *membase;	/* Base address of card */
    unsigned int dmactrl;
	dma_addr_t 	readdma;
	dma_addr_t	writedma;
	volatile unsigned int *writechunk;				/* Double-word aligned write memory */
	volatile unsigned int *readchunk;				/* Double-word aligned read memory */
	struct dahdi_chan **chans[NUM_CARDS];//
	struct dahdi_chan *sigchan[NUM_CARDS];
    int GSM_BOARD_version[NUM_CARDS];
    unsigned char rxident;
	unsigned char lastindex;
	int numbufs;
	int needed_latency;
    unsigned long checkflag;
 };


struct axgcom_desc
{
	char *name;
	int flags;
};


#ifndef AXGCOM_SLEEP_MILLI_SEC
#define AXGCOM_SLEEP_MILLI_SEC(nMilliSec, ms) \
do {                                          \
	set_current_state(TASK_UNINTERRUPTIBLE);  \
	ms = (nMilliSec) * HZ / 1000;             \
	while(ms > 0)                             \
	{                                         \
		ms = schedule_timeout(ms);            \
	}                                         \
}while(0);
#endif


#endif// _ATCOM_USER_H_
