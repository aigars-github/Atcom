/*
 * ATCOM GSM Card Driver for DAHDI Telephony interface
 *
 * Written by Bluce <bluce@atcom.com.cn>
 * Rectified by Robert.Ao <robert.ao@atcom.com.cn>
 * Copyright (C) 2001-2008, Digium, Inc.
 *
 * All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <linux/version.h>
#include <linux/kthread.h> 
#include <dahdi/kernel.h>
#include <linux/jiffies.h>
#include "atcom_user.h"
#include <dahdi/proslic.h>
#include <linux/time.h>
#include <linux/delay.h>

#define SPANTYPE_GSM SPANTYPE_ANALOG_MIXED
/*
TTY driver
*/

/*
 *  Define for audio vs. register based ring detection
 *  
 */
/* #define AUDIO_RINGCHECK  */

/*
  Experimental max loop current limit for the proslic
  Loop current limit is from 20 mA to 41 mA in steps of 3
  (according to datasheet)
  So set the value below to:
  0x00 : 20mA (default)
  0x01 : 23mA
  0x02 : 26mA
  0x03 : 29mA
  0x04 : 32mA
  0x05 : 35mA
  0x06 : 37mA
  0x07 : 41mA
*/
static int loopcurrent = 20;
#define POLARITY_XOR (\
		(reversepolarity != 0) ^ (fxs->reversepolarity != 0) ^\
		(fxs->vmwi_lrev != 0) ^\
		((fxs->vmwisetting.vmwi_type & DAHDI_VMWI_HVAC) != 0))

static int reversepolarity = 0;

static alpha  indirect_regs[] =
{
 {0,255,"DTMF_ROW_0_PEAK",0x55C2},
 {1,255,"DTMF_ROW_1_PEAK",0x51E6},
 {2,255,"DTMF_ROW2_PEAK",0x4B85},
 {3,255,"DTMF_ROW3_PEAK",0x4937},
 {4,255,"DTMF_COL1_PEAK",0x3333},
 {5,255,"DTMF_FWD_TWIST",0x0202},
 {6,255,"DTMF_RVS_TWIST",0x0202},
 {7,255,"DTMF_ROW_RATIO_TRES",0x0198},
 {8,255,"DTMF_COL_RATIO_TRES",0x0198},
 {9,255,"DTMF_ROW_2ND_ARM",0x0611},
 {10,255,"DTMF_COL_2ND_ARM",0x0202},
 {11,255,"DTMF_PWR_MIN_TRES",0x00E5},
 {12,255,"DTMF_OT_LIM_TRES",0x0A1C},
 {13,0,"OSC1_COEF",0x7B30},
 {14,1,"OSC1X",0x0063},
 {15,2,"OSC1Y",0x0000},
 {16,3,"OSC2_COEF",0x7870},
 {17,4,"OSC2X",0x007D},
 {18,5,"OSC2Y",0x0000},
 {19,6,"RING_V_OFF",0x0000},
 {20,7,"RING_OSC",0x7EF0},
 {21,8,"RING_X",0x0160},
 {22,9,"RING_Y",0x0000},
 {23,255,"PULSE_ENVEL",0x2000},
 {24,255,"PULSE_X",0x2000},
 {25,255,"PULSE_Y",0x0000},
 {26,13,"RECV_DIGITAL_GAIN",0x2000},	// playback volume set lower
 {27,14,"XMIT_DIGITAL_GAIN",0x4000},
 {28,15,"LOOP_CLOSE_TRES",0x1000},
 {29,16,"RING_TRIP_TRES",0x3600},
 {30,17,"COMMON_MIN_TRES",0x1000},
 {31,18,"COMMON_MAX_TRES",0x0200},
 {32,19,"PWR_ALARM_Q1Q2",0x07C0},
 {33,20,"PWR_ALARM_Q3Q4",0x2600},
 {34,21,"PWR_ALARM_Q5Q6",0x1B80},
 {35,22,"LOOP_CLOSURE_FILTER",0x8000},
 {36,23,"RING_TRIP_FILTER",0x0320},
 {37,24,"TERM_LP_POLE_Q1Q2",0x008C},
 {38,25,"TERM_LP_POLE_Q3Q4",0x0100},
 {39,26,"TERM_LP_POLE_Q5Q6",0x0010},
 {40,27,"CM_BIAS_RINGING",0x0C00},
 {41,64,"DCDC_MIN_V",0x0C00},
 {42,255,"DCDC_XTRA",0x1000},
 {43,66,"LOOP_CLOSE_TRES_LOW",0x1000},
};

#include <dahdi/kernel.h>
#include <dahdi/wctdm_user.h>

#include <dahdi/fxo_modes.h>

#define NUM_FXO_REGS 60 //

#define WC_MAX_IFACES 128//use
#define WC_SPI_DIV 0x03//0x7//8div
#define WC_UART_DIV 0x0

#define WC_RDADDR	0
#define WC_WRADDR	1
#define WC_COUNT	2
#define WC_DMACTRL	3	
#define WC_INTR		4
#define WC_VERSION	6
#define WC_GPIOCTL	8 
#define WC_GPIO		9
#define WC_LADDR	10 
#define WC_LDATA		11
#define WC_FPGA_FIREWARE_VER	14
#define WC_SUART_RATE  15
#define WC_SPI_REG  16
#define WC_MOD_START_FLAG 17
#define WC_MOD_RESET 18
#define WC_SPI_STAT 19
#define WC_LED_STAT 20

#define WC_LFRMR_CS	(1 << 14)	/* Framer's ChipSelect signal */
#define WC_LREAD			(1 << 15)
#define WC_LWRITE		(1 << 16)

#define MOD_CTRL_DTR         (1<<0)
#define MOD_CTRL_DCD         (1<<1)
#define MOD_CTRL_RI          (1<<2)
#define MOD_CTRL_RST        (1<<4)//rst adda
#define MOD_CTRL_PWR        (1<<5)
#define MOD_CTRL_STATUS     (1<<3)
#define MOD_CTRL_PORTSEL    (1<<6)
#define MOD_CTRL_SLOTUSE    (1<<7)
#define AXGCOM_SBORAD_I2C_BASE 0x80
#define AXGCOM_SBORAD_LOCAL_BASE 0x00

#define AXGCOM_I2C_REG1 0x0B//0B-10 1 REG
#define AXGCOM_I2C_REG2 0x0C
#define AXGCOM_I2C_REG3 0x0D
#define AXGCOM_I2C_REG4 0x0E
#define AXGCOM_I2C_REG5 0x0F
#define AXGCOM_I2C_REG6 0x10
#define AXGCOM_I2C_STATUS 0x11

#define AXGCOM_PCM_MODE 0x12
#define AXGCOM_LOCAL_LOOP 0x13
#define AXGCOM_REMOTE_LOOP 0x14


#define AXGCOM_I2C_DEVICE_ADDR_OK 0x01
#define AXGCOM_I2C_REG_ADDR_OK 0x02
#define AXGCOM_I2C_READ_DATA_OK 0x04
#define AXGCOM_I2C_WRITE_DATA_OK 0x08

#define AXGCOM_TDM_TIMESLOT 0x00
#define AXGCOM_UART_RATE 0x01
#define AXGCOM_SBORAD_VERSION 0x02

#define AXGCOM_SBORAD_POWER_STATE 0x03
#define AXGCOM_POWER_DATA_GSM 0x05
#define AXGCOM_POWER_CTRL_GSM 0x06
#define AXGCOM_RESTART_GSM 0x07//MAYBE DEL SOME REG
#define AXGCOM_SBORAD_ACTIVE_REG 0x08
#define AXGCOM_SBORAD_PCM_CHAN_REG 0x09
#define AXGCOM_I2C_TIMEOUT  0x84 //10ms
#define AXGCOM_START_AT_CMD_COUNT 0x1E

#define FLAG_EMPTY	0
#define FLAG_WRITE	1
#define FLAG_READ	2

#define DEFAULT_RING_DEBOUNCE	64		/* Ringer Debounce (64 ms) */
#define POLARITY_DEBOUNCE 	64		/* Polarity debounce (64 ms) */
#define OHT_TIMER		6000	/* How long after RING to retain OHT */

/* NEON MWI pulse width - Make larger for longer period time
 * For more information on NEON MWI generation using the proslic
 * refer to Silicon Labs App Note "AN33-SI321X NEON FLASHING"
 * RNGY = RNGY 1/2 * Period * 8000
 */
#define NEON_MWI_RNGY_PULSEWIDTH	0x3e8	/*=> period of 250 mS */

#define FLAG_3215	(1 << 0)

static int burn = 0;
static int pcm_mode = 0;

static int adcgain      =-6;       /* adc gain of codec, in db, from -42 to 20, mute=21; */
static int dacgain      = -6;//-15;      /* dac gain of codec, in db, from -42 to 20, mute=21; */
static int sidetone     = 0;        /* digital side tone, 0=mute, others can be -3 to -21db, step by -3; */
static int inputgain    = 0; 

static int pedanticpci = 1;
#define ENABLE_PREFETCH 

static struct task_struct * task_poweron;
static struct task_struct * task_poweron_reset;

DECLARE_WAIT_QUEUE_HEAD(Queue_poweron);
DECLARE_WAIT_QUEUE_HEAD(Queue_poweron_reset);

static struct axgcom_desc axgcomi = { "ATCOM GSM/WCDMA", 0 };
static int acim2tiss[16] = { 0x0, 0x1, 0x4, 0x5, 0x7, 0x0, 0x0, 0x6, 0x0, 0x0, 0x0, 0x2, 0x0, 0x3 };

static struct axgcom *ifaces[WC_MAX_IFACES];

static void axgcom_release(struct axgcom *wc);

static unsigned int fxovoltage;
static unsigned int battdebounce;
static unsigned int battalarm;
static unsigned int battthresh;
static int ringdebounce = DEFAULT_RING_DEBOUNCE;
static int fwringdetect = 0;
static int debug = 0;
static int robust = 0;
static int timingonly = 0;
static int lowpower = 0;
static int boostringer = 0;
static int fastringer = 0;
static int _opermode = 0;
static char *opermode = "FCC";
static int fxshonormode = 0;
static int alawoverride = 0;
static int fastpickup = 0;
static int fxotxgain = 0;
static int fxorxgain = 0;
static int fxstxgain = 0;
static int fxsrxgain = 0;

static int axgcom_init_proslic(struct axgcom *wc, int card, int fast , int manual, int sane);
static int axgcom_init_ring_generator_mode(struct axgcom *wc, int card);
static int axgcom_set_ring_generator_mode(struct axgcom *wc, int card, int mode);
static int axgcom_hooksig(struct dahdi_chan *chan, enum dahdi_txsig txsig);

static int dtmf = 0;

static int mallocsize = 192;

 static inline unsigned short swaphl(unsigned short i)
 {
	return ((i >>8)  & 0x0ff) | (i << 8);
 }

 static inline unsigned int __axgcom_pci_in(struct axgcom *wc, const unsigned int addr)
 {
	unsigned int res = readl(&wc->membase[addr]);
	return res;
 }

 static inline void __axgcom_pci_out(struct axgcom *wc, const unsigned int addr, const unsigned int value)
 {
	unsigned int tmp;
	writel(value, &wc->membase[addr]);
	if (pedanticpci)
	{
		tmp = __axgcom_pci_in(wc, WC_VERSION);
		if ((tmp & 0xffff0000) != 0xa1c00000)
			printk(KERN_NOTICE "AX4G/AX2G4A: Version Synchronization Error!\n");
	}		
 }

 static inline void __axgcom_gpio_set(struct axgcom *wc, unsigned bits, unsigned int val)
 {
	unsigned int newgpio;
	newgpio = wc->gpio & (~bits);
	newgpio |= val;
	if (newgpio != wc->gpio) {
		wc->gpio = newgpio;
		__axgcom_pci_out(wc, WC_GPIO, wc->gpio);
	}	
 }

 static inline void __axgcom_gpio_setdir(struct axgcom *wc, unsigned int bits, unsigned int val)
 {
	unsigned int newgpioctl;
	newgpioctl = wc->gpioctl & (~bits);
	newgpioctl |= val;
	if (newgpioctl != wc->gpioctl) {
		wc->gpioctl = newgpioctl;
		__axgcom_pci_out(wc, WC_GPIOCTL, wc->gpioctl);
	}
 }

static inline void axgcom_gpio_setdir(struct axgcom *wc, unsigned int bits, unsigned int val)
{
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	__axgcom_gpio_setdir(wc, bits, val);
	spin_unlock_irqrestore(&wc->lock, flags);
}

 static inline void axgcom_gpio_set(struct axgcom *wc, unsigned int bits, unsigned int val)
 {
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	__axgcom_gpio_set(wc, bits, val);
	spin_unlock_irqrestore(&wc->lock, flags);
 }

 static inline void axgcom_pci_out(struct axgcom *wc, const unsigned int addr, const unsigned int value)
 {
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	__axgcom_pci_out(wc, addr, value);
	spin_unlock_irqrestore(&wc->lock, flags);
 }

 static inline unsigned int axgcom_pci_in(struct axgcom *wc, const unsigned int addr)
 {
	unsigned int ret;
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	ret = __axgcom_pci_in(wc, addr);
	spin_unlock_irqrestore(&wc->lock, flags);
	return ret;
 }

 static inline unsigned int __axgcom_framer_in(struct axgcom *wc, int unit, const unsigned int addr)
 {
       unsigned int ret = 0;
       unsigned char spi_mode;
       unsigned int spi_addr = 0;
       int count = 0;
 	   unit &= 0x1f;//0-3 is 4
       __axgcom_pci_in(wc, WC_SPI_STAT);
       if((wc->modtype[unit] == MOD_TYPE_FXO))
       {
            spi_mode = 0x7;
            spi_addr = addr &  0x7F;
       }
       else if((wc->modtype[unit] == MOD_TYPE_FXS))
       {
             spi_mode = 0x3;
             spi_addr = addr | 0x80;
       }
       else
       { 
            spi_mode = 0x3;
            spi_addr = addr &  0xBF;
       } 
 	   __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode<<10) );
	   __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff) | WC_LREAD| WC_LFRMR_CS|(spi_mode<<10));
       while(1)
       {
            ret = __axgcom_pci_in(wc, WC_SPI_STAT);
            if((ret == 0x05) || (count > 29))
                break;
            else
              count++;
       }
   	   ret = __axgcom_pci_in(wc, WC_LDATA);
       __axgcom_pci_out(wc, WC_SPI_STAT, 0);
 	   __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode<<10));//cs,read,write flag will change to 0 ,clear flag
	   return ret & 0xff;
}

 static inline unsigned int axgcom_framer_in(struct axgcom *wc, int unit, const unsigned int addr)
 {
	unsigned long flags;
	unsigned int ret;
	spin_lock_irqsave(&wc->lock, flags);
	ret = __axgcom_framer_in(wc, unit, addr);//
	spin_unlock_irqrestore(&wc->lock, flags);
	return ret;
 }

 static inline void __axgcom_framer_out(struct axgcom *wc, int unit, const unsigned int addr, const unsigned int value)
 {
       int count =0;
       int ret = 0;
       unsigned char spi_mode;
       unsigned int spi_addr = 0;
       unit &= 0x1f;
	   __axgcom_pci_out(wc, WC_SPI_STAT, 0);
       if( (wc->modtype[unit] == MOD_TYPE_FXO)  )
       {
            spi_mode = 0x7;
            spi_addr = addr & 0x7F;
       }
       else if((wc->modtype[unit] == MOD_TYPE_FXS))
       {
            spi_mode = 0x3;
            spi_addr = addr & 0x7F;
       }
       else
       {
            spi_mode = 0x3;
            spi_addr = addr|0x40;
       }
	   __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode << 10)); //
	   __axgcom_pci_out(wc, WC_LDATA, value); //
	   __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)  | WC_LWRITE| WC_LFRMR_CS|(spi_mode<<10));
	   __axgcom_pci_out(wc, WC_VERSION, 0);
       __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode<<10));//
       while(1)
       {
            ret = __axgcom_pci_in(wc, WC_SPI_STAT);
            if((ret & 0x1) || (count > 29))
                break;
            else
              count++;
       }
       __axgcom_pci_out(wc, WC_SPI_STAT, 0);
 }

 static inline void axgcom_framer_out(struct axgcom *wc, int unit, const unsigned int addr, const unsigned int value)
 {
	unsigned long flags;
	spin_lock_irqsave(&wc->lock, flags);
	__axgcom_framer_out(wc, unit, addr, value);
	spin_unlock_irqrestore(&wc->lock, flags);
 }

 static inline void axgcom_set_led(struct axgcom* wc, int onoff)
 {
      __axgcom_pci_out(wc,WC_LED_STAT,onoff);
 }

 static inline unsigned int __axgcom_i2c_framer_in(struct axgcom *wc, int unit, const unsigned int addr)
 {
	  unsigned int ret;
    unsigned long origjiffies;
    unsigned char spi_mode;
    unsigned int spi_addr = 0;
	  unit &= 0x1f;//0-3 is 4  cs,read write is low active
    if((wc->modtype[unit] == MOD_TYPE_FXO))
       spi_mode = 0x7;
    else
    {
       spi_mode = 0x3;
       spi_addr = addr&0xBF;
    }
	  __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode <<10));
	  __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)  | WC_LREAD| WC_LFRMR_CS|(spi_mode<<10));
    origjiffies = jiffies;
    if(addr != AXGCOM_I2C_STATUS)
    {
         while (1)
         {
            if (jiffies_to_msecs(jiffies - origjiffies) >= AXGCOM_I2C_TIMEOUT)
                break;
         }
    }
    ret = __axgcom_pci_in(wc, WC_LDATA);
 	  __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode<<10));//cs,read,write flag will change to 0 ,clear flag
	  return ret & 0xff;
 }

 static inline unsigned int axgcom_i2c_framer_in(struct axgcom *wc, int unit, const unsigned int addr)
 {
	unsigned int ret;
  unsigned int res;
	ret = __axgcom_i2c_framer_in(wc, unit, addr);//
  if(res)
     return -1;
  else
 	  return ret;
 }

 static inline void __axgcom_i2c_framer_out(struct axgcom *wc, int unit, const unsigned int addr, const unsigned int value)
 {
       unsigned char spi_mode;
       unsigned long origjiffies;
       unsigned int spi_addr = 0;
       if((wc->modtype[unit] == MOD_TYPE_FXO))
            spi_mode = 0x7;
       else 
       {
            spi_addr = addr |0x40;
            spi_mode = 0x3;
       }
 	   unit &= 0x1f;
 	   if (debug)
	      printk( " i2c Writing %02x to address %02x of unit %d\n", value, addr, unit);
	   __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff) | (spi_mode << 10));
	   __axgcom_pci_out(wc, WC_LDATA, value);
       origjiffies = jiffies;
     __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)  | WC_LWRITE| WC_LFRMR_CS|(spi_mode<<10));  
 	  __axgcom_pci_out(wc, WC_LADDR, (unit << 24) | (spi_addr & 0xff)|(spi_mode<<10));//
      if(addr != AXGCOM_I2C_STATUS)
      {
          while (1)
          {
             if (jiffies_to_msecs(jiffies - origjiffies) >= AXGCOM_I2C_TIMEOUT)
                break;
          }
      }  
 }

 static inline int axgcom_i2c_framer_out(struct axgcom *wc, int unit, const unsigned int addr, const unsigned int value)
 {
    unsigned int ret = 0;
	__axgcom_i2c_framer_out(wc, unit, addr, value);
    return ret;
 }

 static inline int  i2c_read(struct axgcom *wc, int card, unsigned char reg)
 {
        return axgcom_i2c_framer_in(wc,card,reg);
 }

 static inline int  i2c_write(struct axgcom *wc, int card, unsigned char reg, unsigned char value)
 {
         return axgcom_i2c_framer_out(wc,card,reg,value);
 }

 // init AD/DA
 static  void axgcom_init_uart(struct axgcom *wc)
 {
    int card = 0;
    int i =0;
    for(card = 0; card < 4; card++)
    {
         i = 2*card;
         if((wc->cardflag & (1<<i)) && (wc->modtype[i] == MOD_TYPE_GSM) )
                axgcom_framer_out(wc, i, AXGCOM_UART_RATE, 0x00);
    }
 }

 static void axgcom_init_env(struct axgcom *wc)
 {
    axgcom_pci_out(wc, WC_SPI_REG, WC_SPI_DIV);
 }

 static void axgcom_env_init(struct axgcom *wc)
 {
    axgcom_init_env(wc);
    axgcom_init_uart(wc);
 }

 static inline unsigned char format_adcgain(int gain)
 {
	unsigned char ret = 0x2a;   /* default adcgain=0db */
	if((gain > 21) || (gain < -42))
		printk(KERN_WARNING "ax2g4a: Invalid adcgain %d\n", gain);
	else
	{
		int i = 0x2a;
		if(gain == 21)
			ret = 0x3f; /* Mute. */
		else
		    ret = (unsigned char)(i + gain);    /* change to correct format; */

	}
	return ret;
 }

 static inline unsigned char format_dacgain(int gain)
 {
	unsigned char ret = 0x56;   /* default dacgain=-20db */
	if(gain > 21 || gain < -42)
		printk(KERN_WARNING "ax2g4a: Invalid dacgain %d\n", gain);
	else
	{
		int i = 0x6a;
		if(gain == 21)
			ret = 0x7f; /* Mute. */
		else
			ret = (unsigned char)(i + gain);    /* change to correct format; */
	}
	return ret;
 }

 static inline unsigned char format_sidetone(int sidetone)
 {
	unsigned char ret = 0x7;    /* default mute. */
	int i;
	int valid_values[] = {0, -3, -6, -9, -12, -15, -18, -21};
	int num_values = sizeof(valid_values) / sizeof(valid_values[0]);
	for (i = 0; i < num_values; i ++)
	{
		if(sidetone == valid_values[i])
			break;
	}
	if (i < num_values)
	{
		if(i != 0)
		{
		   ret = i-1;
		   printk(KERN_DEBUG "ax2g4a: sidetone set to %ddb\n", valid_values[i]);
		}
	}
	else
		printk(KERN_WARNING "ax2g4a: sidetone value %d not correct, set to default 7(MUTE)\n", sidetone);
	return ret;
 }

 static inline unsigned char format_inputgain(int gain)
 {
	unsigned char ret = 0;  /* default 0db. */
	int i;
	int valid_values[] = {0, 6, 12, 24 };
	int num_values = sizeof(valid_values) / sizeof(valid_values[0]);
	for(i = 0; i < num_values; i ++)
	{
		if(gain == valid_values[i])
			break;
	}
	if(i < num_values)
		ret = i;
	else
		printk(KERN_WARNING "ax2g4a: input gain value %d not correct, set to default 0db\n", gain);
	return ret;
 }


 static inline int axgcom_init_adda_reg(struct axgcom *wc, int card)
 {
     int res;
     unsigned char rt;
     printk("start axgcom_init_adda_reg card =%d\n",card);
     res = i2c_write(wc, card,  AXGCOM_I2C_REG2, 0xa0);//0xa0
     if(debug)
       printk("start axgcom_init_adda_reg 1\n");
     res = i2c_write(wc, card, AXGCOM_I2C_REG1  ,  0x49);//0x41,43(digital) 45(analog),49(bias) 4d
     res = i2c_write(wc, card, AXGCOM_I2C_REG3, 0x01);
     res = i2c_write(wc, card,  AXGCOM_I2C_REG4, 0x88);
     res = i2c_write(wc, card,  AXGCOM_I2C_REG4, 0x01);
     rt = format_adcgain(adcgain);
     res = i2c_write(wc, card, AXGCOM_I2C_REG5, rt);//0x2a 0db 00
     rt = format_dacgain(dacgain);
     res = i2c_write(wc, card, AXGCOM_I2C_REG5, rt );//0db 01 b7-b6
     rt = 0x80;
     rt |= (format_sidetone(sidetone) << 3) | format_inputgain(inputgain);
     res = i2c_write(wc, card, AXGCOM_I2C_REG5, rt);
     res = i2c_write(wc, card, AXGCOM_I2C_REG5, 0xff);
     if(debug)
        printk("start axgcom_init_adda_reg 6\n");
     res = i2c_write(wc, card,  AXGCOM_I2C_REG6, 0xb0);
     return 0;
 }

 static inline void axgcom_init_addactrol(struct axgcom *wc)
 {
     long ms;
     int card;
     int i = 0; 
     for(card = 0; card < 4; card++)
     {
         i =2*card;
         if((wc->cardflag & (1<<i)) && ((wc->modtype[i] == MOD_TYPE_GSM)))
         {
            if(debug)
                printk("jwj start axgcom_init_addactrol card =%d\n",card);
            axgcom_framer_out(wc, i, AXGCOM_POWER_DATA_GSM, 0x00);
            axgcom_framer_out(wc, i, AXGCOM_POWER_CTRL_GSM, MOD_CTRL_RST);
            AXGCOM_SLEEP_MILLI_SEC(100, ms);
            if(debug)
                printk("start axgcom_init_addactrol card =%d RST ADDA\n",i);
            axgcom_framer_out(wc, i, AXGCOM_POWER_DATA_GSM, MOD_CTRL_RST);
            axgcom_framer_out(wc, i, AXGCOM_POWER_CTRL_GSM, MOD_CTRL_RST);
            axgcom_init_adda_reg(wc,i);
         }
      }
 }

//Linear to xlaw for GSM 16 bit data 
 static inline void  dahdi_line_to_x(struct axgcom *wc, struct dahdi_chan * ss,  unsigned char card_flag)
 {
    unsigned char i;
    unsigned short tmp;
    for (i = 0; i < DAHDI_CHUNKSIZE; i++)
    {
	   tmp = swaphl(wc->rx_buf_linear[card_flag][i]);
          wc->rx_buf[card_flag][i] = DAHDI_LIN2X(tmp, ss);
    }
 }

 static inline void  dahdi_x_to_line(struct axgcom *wc, struct dahdi_chan * ss,  unsigned char card_flag)
 {
       unsigned char i;
       unsigned short tmp;
       if(!wc || !ss)
            return ;
  	   for (i = 0; i < DAHDI_CHUNKSIZE; i++)
	   {
		 tmp = DAHDI_XLAW(wc->tx_buf[card_flag][i], ss);
		 wc->tx_buf_linear[card_flag][i]= swaphl(tmp);
 	   }
 }

 static void axgcom_stop_dma(struct axgcom *wc);

 static inline void wait_a_little(void)
 {
	unsigned long newjiffies=jiffies+2;
	while(jiffies < newjiffies);
 }

#define MAX 6000
 static int __wait_access(struct axgcom *wc, int card)
 {
    unsigned char data = 0;
    long origjiffies;
    int count = 0;
    origjiffies = jiffies;
    while (count++ < MAX)
    {
	    data = __axgcom_framer_in(wc, card, I_STATUS);
		if (!data)
			return 0;
    }
   if(count > (MAX-1)) printk(KERN_NOTICE " ##### Loop error (%02x) #####\n", data);
	return 0;
 }

 static unsigned char translate_3215(unsigned char address)
 {
	int x;
	for (x=0;x<sizeof(indirect_regs)/sizeof(indirect_regs[0]);x++)
	{
		if (indirect_regs[x].address == address)
		{
			address = indirect_regs[x].altaddr;
			break;
		}
	}
	return address;
 }

 static int axgcom_proslic_setreg_indirect(struct axgcom *wc, int card, unsigned char address, unsigned short data)
 {
	unsigned long flags;
	int res = -1;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215)
	{
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	spin_lock_irqsave(&wc->lock, flags);
	if(!__wait_access(wc, card))
	{
		__axgcom_framer_out(wc, card, IDA_LO,(unsigned char)(data & 0xFF));
		__axgcom_framer_out(wc, card, IDA_HI,(unsigned char)((data & 0xFF00)>>8));
		__axgcom_framer_out(wc, card, IAA,address);
		res = 0;
	};
	spin_unlock_irqrestore(&wc->lock, flags);
	return res;
 }

 static int axgcom_proslic_getreg_indirect(struct axgcom *wc, int card, unsigned char address)
 {
	unsigned long flags;
	int res = -1;
	char *p=NULL;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215)
	{
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	spin_lock_irqsave(&wc->lock, flags);
	if (!__wait_access(wc, card))
	{
		__axgcom_framer_out(wc, card, IAA, address);
		if (!__wait_access(wc, card))
		{
			unsigned char data1, data2;
			data1 = __axgcom_framer_in(wc, card, IDA_LO);
			data2 = __axgcom_framer_in(wc, card, IDA_HI);
			res = data1 | (data2 << 8);
		}
		else
			p = "Failed to wait inside\n";
	} else
		p = "failed to wait\n";
	spin_unlock_irqrestore(&wc->lock, flags);
	if (p)
		printk(KERN_NOTICE "%s", p);
	return res;
 }

 static  int axgcom_proslic_init_indirect_regs(struct axgcom *wc, int card)
 {
	unsigned char i;
	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++)
	{
		if(axgcom_proslic_setreg_indirect(wc, card, indirect_regs[i].address,indirect_regs[i].initial))
			return -1;
	}
	return 0;
 }

 static int axgcom_proslic_verify_indirect_regs(struct axgcom *wc, int card)
 {
	int passed = 1;
	unsigned short i, initial;
	int j;
	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++) 
	{
		if((j = axgcom_proslic_getreg_indirect(wc, card, (unsigned char) indirect_regs[i].address)) < 0)
		{
			printk(KERN_NOTICE "Failed to read indirect register %d\n", i);
			return -1;
		}
		initial= indirect_regs[i].initial;
		if ( j != initial && (!(wc->flags[card] & FLAG_3215) || (indirect_regs[i].altaddr != 255)))
		{
			 printk(KERN_NOTICE "!!!!!!! %s  iREG %X = %X  should be %X\n",indirect_regs[i].name,indirect_regs[i].address,j,initial );
			 passed = 0;
		}	
	}
    if (passed)
    {
		if (debug)
			printk(KERN_DEBUG "Init Indirect Registers completed successfully.\n");
    }
    else
    {
		printk(KERN_NOTICE " !!!!! Init Indirect Registers UNSUCCESSFULLY.\n");
		return -1;
    }
    return 0;
 }

 static inline void axgcom_proslic_recheck_sanity(struct axgcom *wc, int card)
 {
	struct fxs *const fxs = &wc->mod[card].fxs;
	int res;
	/* Check loopback */
	res = wc->reg1shadow[card];
	if (!res && (res != fxs->lasttxhook))
	{
		res = axgcom_framer_in(wc, card, 8);
		if (res)
		{
			printk(KERN_NOTICE "Ouch, part reset, quickly restoring reality (%d)\n", card+1);
			axgcom_init_proslic(wc, card, 1, 0, 1);
		}
		else
		{
			if (fxs->palarms++ < MAX_ALARMS)
			{
				printk(KERN_NOTICE "Power alarm on module %d, resetting!\n", card + 1);
				if (fxs->lasttxhook == SLIC_LF_RINGING)
					fxs->lasttxhook = SLIC_LF_ACTIVE_FWD;
				axgcom_framer_out(wc, card, 64, fxs->lasttxhook);
			}
			else
			{
				if (fxs->palarms == MAX_ALARMS)
					printk(KERN_NOTICE "Too many power alarms on card %d, NOT resetting!\n", card + 1);
			}
		}
	}
 }


static inline void axgcom_voicedaa_check_hook(struct axgcom *wc, int card)
{
#define MS_PER_CHECK_HOOK 16
	unsigned char res;
	signed char b;
	int errors = 0;
	struct fxo *fxo = &wc->mod[card].fxo;
    int spanno  = 0;
    int offset = 0;
    offset = card ;
    spanno = wc->realspan;
    if((offset < 0) || (spanno < 0 ))
          return ;
	/* Try to track issues that plague slot one FXO's */
	b = wc->reg0shadow[card];
	if ((b & 0x2) || !(b & 0x8))
	{
		if (debug)
			printk(KERN_DEBUG "Error (%02x) on card %d!\n", b, card + 1); 
		errors++;
	}
	b &= 0x9b;
	if (fxo->offhook)
	{
	   if(b != 0x9)
		    axgcom_framer_out(wc, card, 5, 0x9);
	}
	else
	{
		if (b != 0x8)
			axgcom_framer_out(wc, card, 5, 0x8);
	}
	if (errors)
		return;
	if (!fxo->offhook)
	{
		if (fwringdetect)
		{
			res = wc->reg0shadow[card] & 0x60;
			if (fxo->ringdebounce)
			{
				--fxo->ringdebounce;
				if (res && (res != fxo->lastrdtx) && (fxo->battery == BATTERY_PRESENT))
				{
					if (!fxo->wasringing)
					{
						fxo->wasringing = 1;
						if (debug)
							printk(KERN_DEBUG "RING on %d/%d!\n", wc->axspan[spanno].span.spanno, card + 1);
						dahdi_hooksig(wc->chans[spanno][offset], DAHDI_RXSIG_RING);
					}
					fxo->lastrdtx = res;
					fxo->ringdebounce = 10;
				}
				else if (!res)
				{
					if ((fxo->ringdebounce == 0) && fxo->wasringing)
					{
						fxo->wasringing = 0;
						if (debug)
							printk(KERN_DEBUG "NO RING on %d/%d!\n", wc->axspan[spanno].span.spanno, card + 1);
						dahdi_hooksig(wc->chans[spanno][offset], DAHDI_RXSIG_OFFHOOK);
					}
				}
			}
			else if (res && (fxo->battery == BATTERY_PRESENT))
			{
				fxo->lastrdtx = res;
				fxo->ringdebounce = 10;
			}
		}
		else
		{
			res = wc->reg0shadow[card];
			if ((res & 0x60) && (fxo->battery == BATTERY_PRESENT))
			{
				fxo->ringdebounce += (DAHDI_CHUNKSIZE * 16);
				if (fxo->ringdebounce >= DAHDI_CHUNKSIZE * ringdebounce)
				{
					if (!fxo->wasringing)
					{
						fxo->wasringing = 1;
                        wc->mod[card].fxo.readcid = 0;
						wc->mod[card].fxo.cidtimer = wc->intcount;
						dahdi_hooksig(wc->chans[spanno][offset], DAHDI_RXSIG_RING);
						if (debug)
							printk(KERN_DEBUG "RING on %d/%d!\n", wc->axspan[spanno].span.spanno, card + 1);
					}
					fxo->ringdebounce = DAHDI_CHUNKSIZE * ringdebounce;
				}
			}
			else
			{
				fxo->ringdebounce -= DAHDI_CHUNKSIZE * 4;
				if (fxo->ringdebounce <= 0)
				{
					if (fxo->wasringing)
					{
						fxo->wasringing = 0;
                        wc->mod[card].fxo.readcid = 0;
                        wc->mod[card].fxo.cidtimer = wc->intcount;
						dahdi_hooksig(wc->chans[spanno][offset], DAHDI_RXSIG_OFFHOOK);
						if (debug)
							printk(KERN_DEBUG "NO RING on %d/%d!\n", wc->axspan[spanno].span.spanno, card + 1);
					}
					fxo->ringdebounce = 0;
				}
			}
		}
	}
	b = wc->reg1shadow[card];
	if (fxovoltage)
	{
		static int count = 0;
		if (!(count++ % 100))
			printk(KERN_DEBUG "Card %d: Voltage: %d Debounce %d\n", card + 1, b, fxo->battdebounce);
	}
	if (unlikely(DAHDI_RXSIG_INITIAL == wc->chans[spanno][offset]->rxhooksig))
	{
		/*
		 * dahdi-base will set DAHDI_RXSIG_INITIAL after a
		 * DAHDI_STARTUP or DAHDI_CHANCONFIG ioctl so that new events
		 * will be queued on the channel with the current received
		 * hook state.  Channels that use robbed-bit signalling always
		 * report the current received state via the dahdi_rbsbits
		 * call. Since we only call axgcom_hooksig when we've detected
		 * a change to report, let's forget our current state in order
		 * to force us to report it again via axgcom_hooksig.
		 */
		fxo->battery = BATTERY_UNKNOWN;
	}
	if (abs(b) < battthresh)
	{
		/* possible existing states:
		   battery lost, no debounce timer
		   battery lost, debounce timer (going to battery present)
		   battery present or unknown, no debounce timer
		   battery present or unknown, debounce timer (going to battery lost)
		*/
		if (fxo->battery == BATTERY_LOST)
		{
			if (fxo->battdebounce)
			{
				/* we were going to BATTERY_PRESENT, but battery was lost again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		}
		else
		{
			if (fxo->battdebounce)
			{
				/* going to BATTERY_LOST, see if we are there yet */
				if (--fxo->battdebounce == 0)
				{
					fxo->battery = BATTERY_LOST;
					if (debug)
						printk(KERN_DEBUG "NO BATTERY on %d/%d!\n", wc->axspan[spanno].span.spanno, card + 1);
					dahdi_hooksig(wc->chans[spanno][offset], DAHDI_RXSIG_ONHOOK);
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
				}
			}
			else
			{
				/* start the debounce timer to verify that battery has been lost */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}
	}
	else
	{
		/* possible existing states:
		   battery lost or unknown, no debounce timer
		   battery lost or unknown, debounce timer (going to battery present)
		   battery present, no debounce timer
		   battery present, debounce timer (going to battery lost)
		*/
		if (fxo->battery == BATTERY_PRESENT)
		{
			if (fxo->battdebounce)
			{
				/* we were going to BATTERY_LOST, but battery appeared again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		}
		else
		{
			if (fxo->battdebounce)
			{
				/* going to BATTERY_PRESENT, see if we are there yet */
				if (--fxo->battdebounce == 0)
				{
					fxo->battery = BATTERY_PRESENT;
					if (debug)
						printk(KERN_DEBUG "BATTERY on %d/%d (%s)!\n", wc->axspan[spanno].span.spanno, card + 1,(b < 0) ? "-" : "+");
					dahdi_hooksig(wc->chans[spanno][offset], DAHDI_RXSIG_OFFHOOK);
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
				}
			}
			else
			{
				/* start the debounce timer to verify that battery has appeared */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}
	}
	if (fxo->lastpol >= 0)
	{
		if (b < 0)
		{
			fxo->lastpol = -1;
			fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
		}
	} 
	if (fxo->lastpol <= 0)
	{
		if (b > 0)
		{
			fxo->lastpol = 1;
			fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
		}
	}
	if (fxo->battalarm)
	{
		if (--fxo->battalarm == 0)
		{
			/* the alarm timer has expired, so update the battery alarm state
			   for this channel */
			dahdi_alarm_channel(wc->chans[spanno][offset], fxo->battery == BATTERY_LOST ? DAHDI_ALARM_RED : DAHDI_ALARM_NONE);
		}
	}
	if (fxo->polaritydebounce)
	{
		if (--fxo->polaritydebounce == 0)
		{
		    if (fxo->lastpol != fxo->polarity)
		    {
				if (debug)
					printk(KERN_DEBUG "%lu Polarity reversed (%d -> %d)\n", jiffies,fxo->polarity,fxo->lastpol);
				if (fxo->polarity)
					dahdi_qevent_lock(wc->chans[spanno][offset], DAHDI_EVENT_POLARITY);
				fxo->polarity = fxo->lastpol;
		    }
		}
	}
#undef MS_PER_CHECK_HOOK
 }

 static inline void axgcom_proslic_check_hook(struct axgcom *wc, int card)
 {
	struct fxs *const fxs = &wc->mod[card].fxs;
	char res;
	int hook;
	/* For some reason we have to debounce the
	   hook detector.  */
    int span = 0;
    int offset = 0;
    offset = card;
    span = wc->realspan;
    if((span < 0)||(offset <0))
          return;
    res = wc->reg0shadow[card];
	hook = (res & 1);
	if (hook != fxs->lastrxhook)
	{
		/* Reset the debounce (must be multiple of 4ms) */
		fxs->debounce = 8 * (4 * 8);
	}
	else
	{
		if (fxs->debounce > 0)
		{
			fxs->debounce -= 16 * DAHDI_CHUNKSIZE;
			if (!fxs->debounce)
				fxs->debouncehook = hook;
			if (!fxs->oldrxhook && fxs->debouncehook)
			{
				if (debug)
					printk(KERN_DEBUG "axgcom: Card %d Going off hook\n", card);
				switch (fxs->lasttxhook)
				{
				  case SLIC_LF_RINGING:
				  case SLIC_LF_OHTRAN_FWD:
				  case SLIC_LF_OHTRAN_REV:
					/* just detected OffHook, during Ringing or OnHookTransfer */
					fxs->idletxhookstate =POLARITY_XOR ?SLIC_LF_ACTIVE_REV :SLIC_LF_ACTIVE_FWD;
					break;
				}
			    dahdi_hooksig(wc->chans[span][offset], DAHDI_RXSIG_OFFHOOK);
				if (robust)
					axgcom_init_proslic(wc, card, 1, 0, 1);
				fxs->oldrxhook = 1;
			}
			else if (fxs->oldrxhook && !fxs->debouncehook)
			{
				/* On hook */
				if (debug)
					printk(KERN_DEBUG "axgcom: Card %d Going on hook\n", card);
				dahdi_hooksig(wc->chans[span][offset], DAHDI_RXSIG_ONHOOK);
				fxs->oldrxhook = 0;
			}
		}
	}
	fxs->lastrxhook = hook;
 }

 static  void ax2g4a_irq_handle3(struct axgcom *wc)
 {
    int x = 0;
    int mode = 0;
    int y = 0;
    int z = 0;
    x = (wc->intcount & 0x3);//0-3
    mode = wc->intcount & 0xc;//0-12
    {
       for(y=0; y < 3;y++)
       {
            z = x + 2*y;//0-3//0-2
            if (wc->cardflag & (1 << z))
            {
			switch(mode)
			{
			 case 0:
				/* Rest */
				break;
			 case 4:
				/* Read first shadow reg */
				if (wc->modtype[z] == MOD_TYPE_FXS)
					wc->reg0shadow[z] = axgcom_framer_in(wc, z, 68);
				else if (wc->modtype[z] == MOD_TYPE_FXO)
					wc->reg0shadow[z] = axgcom_framer_in(wc, z, 5);
				break;
			 case 8:
				/* Read second shadow reg */
				if (wc->modtype[z] == MOD_TYPE_FXS)
					wc->reg1shadow[z] = axgcom_framer_in(wc, z, LINE_STATE);
				else if (wc->modtype[z] == MOD_TYPE_FXO)
					wc->reg1shadow[z] = axgcom_framer_in(wc, z, 29);
				break;
		 	case 12:
				/* Perform processing */
				if (wc->modtype[z] == MOD_TYPE_FXS)
				{
					axgcom_proslic_check_hook(wc, z);
					if (!(wc->intcount & 0xf0))
						axgcom_proslic_recheck_sanity(wc, z);
				}
				else if (wc->modtype[z] == MOD_TYPE_FXO)
					axgcom_voicedaa_check_hook(wc, z);
				break;
			}
		}
      }
     }
     return ;
 }

 static void analog_irq_handle1(struct axgcom *wc)
 {
    int x;
    for (x=0;x<wc->num_card;x++)
    {
	if (wc->cardflag & (1 << x) && (wc->modtype[x] == MOD_TYPE_FXS))
    {
		struct fxs *const fxs = &wc->mod[x].fxs;
		if (fxs->lasttxhook == SLIC_LF_RINGING &&	!fxs->neonringing)
		{
			/* RINGing, prepare for OHT */
			fxs->ohttimer = OHT_TIMER << 3;
			/* logical XOR 3 variables   module parameter 'reversepolarity', global reverse all FXS lines.
		        ioctl channel variable fxs 'reversepolarity', Line Reversal Alert Signal if required.
			    ioctl channel variable fxs 'vmwi_lrev', VMWI pending. */
			/* OHT mode when idle */
			fxs->idletxhookstate = POLARITY_XOR ?SLIC_LF_OHTRAN_REV :SLIC_LF_OHTRAN_FWD;
	   }
       else if (fxs->ohttimer)
	   {
		/* check if still OnHook */
		if (!fxs->oldrxhook)
        {
			fxs->ohttimer -= DAHDI_CHUNKSIZE;
			if (!fxs->ohttimer)
            {
				fxs->idletxhookstate = POLARITY_XOR ? SLIC_LF_ACTIVE_REV : SLIC_LF_ACTIVE_FWD; /* Switch to Active, Rev or Fwd */
				/* if currently OHT */
				if ((fxs->lasttxhook == SLIC_LF_OHTRAN_FWD) || (fxs->lasttxhook == SLIC_LF_OHTRAN_REV))
                {
					if (fxs->vmwi_hvac)
                    {
						/* force idle polarity Forward if ringing */
						fxs->idletxhookstate = SLIC_LF_ACTIVE_FWD;
						/* Set ring generator for neon */
						axgcom_set_ring_generator_mode(wc, x, 1);
						fxs->lasttxhook = SLIC_LF_RINGING;
					}
                    else
						fxs->lasttxhook = fxs->idletxhookstate;
					/* Apply the change as appropriate */
					axgcom_framer_out(wc, x, LINE_STATE, fxs->lasttxhook);
				 }
			 }
		  }
          else
          {
			fxs->ohttimer = 0;
			/* Switch to Active, Rev or Fwd */
			fxs->idletxhookstate = POLARITY_XOR ? SLIC_LF_ACTIVE_REV : SLIC_LF_ACTIVE_FWD;
		  }
		}
	  }
	}
 }

 static  void axgcom_tdm_decode(struct axgcom *wc, int numspan)
 {
    int i=0;
    int x =0;
    unsigned short length ;
    int flag;
    int offset = 0;
    unsigned char sig_recv = 0;
    volatile unsigned int * read_chunk_int = wc->axspan[numspan].readchunk;
    if(wc->span_type == 1)//ax4g
           offset = 2*numspan;
    else if(wc->span_type == 2)//ax2g4a
    {
        if(numspan < 2)
              offset =wc->offset_span+2*numspan ;
    }
    if(wc->span_type==2) //AX2G4A
    {
       for (x=0;x<DAHDI_CHUNKSIZE;x++)
	   {
		 switch (numspan)
         {
            case 0:
		       if (wc->cardflag & (1 << 4))//16bit
			   {
                   wc->rx_buf_linear[numspan][x]  = (unsigned short)((read_chunk_int[x*2] >> 16) & 0xffff);
                   if( wc->sig_rx_idex[numspan] < AXGCOM_ATCMD_BUF_LEN)
                   {
                         sig_recv = (unsigned char)(read_chunk_int[x*2] >>8) & 0xff;
                         if((sig_recv > 0x0))
                               wc->sig_rx_buf[numspan][wc->sig_rx_idex[numspan]++] = sig_recv;
                   }
              }
              break;
          case 1:
              if (wc->cardflag & (1 << 6))
                   wc->rx_buf_linear[numspan][x]  = (unsigned short)((read_chunk_int[x*2] >> 16) & 0xffff);
              if( wc->sig_rx_idex[numspan] < AXGCOM_ATCMD_BUF_LEN)
              {
                   sig_recv = (unsigned char)(read_chunk_int[x*2] >>8) & 0xff;
                   if( (sig_recv > 0x0) )
                        wc->sig_rx_buf[numspan][wc->sig_rx_idex[numspan]++] = sig_recv;
              }
              break;
          case 2:
              if (wc->cardflag & (1 << 0))//int to char jwj add 2012-5-26
		         wc->chans[numspan][0]->readchunk[x] = (unsigned char)(read_chunk_int[2*x]) & 0xff;
			  if (wc->cardflag & (1 << 1))
			     wc->chans[numspan][1]->readchunk[x] = (unsigned char)(read_chunk_int[2*x] >> 8) & 0xff;
			  if (wc->cardflag & (1 << 2))
			      wc->chans[numspan][2]->readchunk[x] = (unsigned char)(read_chunk_int[2*x] >> 16) & 0xff;
			  if (wc->cardflag & (1 << 3))
			      wc->chans[numspan][3]->readchunk[x] = (unsigned char)(read_chunk_int[2*x] >> 24) & 0xff;//4//4��ģ��ͨ��
              break;
           default:
              break;
          }
        }
    }
	else if(wc->span_type == 1)
    {
       for (x=0;x<DAHDI_CHUNKSIZE;x++)
       {
          switch (numspan)
          {
             case 0:
				if (wc->cardflag & (1 << 0))
 			    {
				    wc->rx_buf_linear[numspan][x]  = (unsigned short)((read_chunk_int[x*2] >> 16) & 0xffff);
                    if( wc->sig_rx_idex[numspan] < AXGCOM_ATCMD_BUF_LEN)
                    {
                        sig_recv = (unsigned char)(read_chunk_int[x*2] >>8) & 0xff;
                        if((sig_recv > 0x0))
                             wc->sig_rx_buf[numspan][wc->sig_rx_idex[numspan]++] = sig_recv;
                    }
                 }
                 break;
              case 1:
                 if(wc->cardflag & (1 << 2))
				 {
                      wc->rx_buf_linear[numspan][x] = (unsigned short)((read_chunk_int[x*2]>>16) & 0xffff);
                      if( wc->sig_rx_idex[numspan] < AXGCOM_ATCMD_BUF_LEN)
                      {
                          sig_recv = (unsigned char)(read_chunk_int[x*2] >>8) & 0xff;
                          if((sig_recv > 0x0))
                              wc->sig_rx_buf[numspan][wc->sig_rx_idex[numspan]++] = sig_recv;
                       }
                  }
                  break;
               case 2:
                  if (wc->cardflag & (1 << 4))
  				  {
                       wc->rx_buf_linear[numspan][x] = (unsigned short)((read_chunk_int[x*2] >> 16) & 0xffff);
                       if( wc->sig_rx_idex[numspan] < AXGCOM_ATCMD_BUF_LEN)
                       {
                            sig_recv = (unsigned char)(read_chunk_int[x*2] >>8) & 0xff;
                            if((sig_recv > 0x0))
                               wc->sig_rx_buf[numspan][wc->sig_rx_idex[numspan]++] = sig_recv;
                       }
                  }
                  break;
               case 3:
                  if (wc->cardflag & (1 <<6))
				  {
                     wc->rx_buf_linear[numspan][x]  = (unsigned short)((read_chunk_int[x*2]>>16) & 0xffff);
                     if( wc->sig_rx_idex[numspan] < AXGCOM_ATCMD_BUF_LEN)
                     {
                        sig_recv = (unsigned char)(read_chunk_int[x*2] >>8) & 0xff;
                        if( (sig_recv > 0x0) )
                        {
                            wc->sig_rx_buf[numspan][wc->sig_rx_idex[numspan]++] = sig_recv;
                            if(debug >2)
                                 printk("sig_rx_idx=%d buf=%x idx=%d 4 \n",wc->sig_rx_idex[numspan], sig_recv,x);
                        }
                      }
                      else
                         printk("span =%d sig rx idx =%d  is over max\n",numspan,wc->sig_rx_idex[numspan] );
                    }
                    break;
                default:
                    break;
             }
         }
                            
	  }
      else
      {
         for (x=0;x<DAHDI_CHUNKSIZE;x++)
	     {
                if (wc->cardflag & (1 << 0))
			        wc->chans[numspan][0]->readchunk[x] = (unsigned char)(read_chunk_int[2*x]) & 0xff;
		        if (wc->cardflag & (1 << 1))
			        wc->chans[numspan][1]->readchunk[x] = (unsigned char)(read_chunk_int[2*x] >> 8) & 0xff;
		        if (wc->cardflag & (1 << 2))
				    wc->chans[numspan][2]->readchunk[x] = (unsigned char)(read_chunk_int[2*x] >> 16) & 0xff;
			    if (wc->cardflag & (1 << 3))
			        wc->chans[numspan][3]->readchunk[x] = (unsigned char)(read_chunk_int[2*x] >> 24) & 0xff;
                if (wc->cardflag & (1 << 4))
		            wc->chans[numspan][4]->readchunk[x] = (unsigned char)(read_chunk_int[2*x+1]) & 0xff;
		        if (wc->cardflag & (1 << 5))
		            wc->chans[numspan][5]->readchunk[x] = (unsigned char)(read_chunk_int[2*x+1] >> 8) & 0xff;
		        if (wc->cardflag & (1 << 6))
				    wc->chans[numspan][6]->readchunk[x] = (unsigned char)(read_chunk_int[2*x+1] >> 16) & 0xff;
			    if (wc->cardflag & (1 << 7))
			        wc->chans[numspan][7]->readchunk[x] = (unsigned char)(read_chunk_int[2*x+1] >> 24) & 0xff;
	    }
     }
	 if(wc->modtype[offset] == MOD_TYPE_GSM)
     {
		dahdi_line_to_x(wc, wc->chans[numspan][0], numspan);
        length = wc->sig_rx_idex[numspan];
        flag = 0;
        for(i = 0; i< length; i++)
        {
          if(wc->sig_rx_buf[numspan][i] == 0xd)
            flag = 1;//recv
          else if(wc->sig_rx_buf[numspan][i] == 0xa)
            flag = 2;//recv
          else if(wc->sig_rx_buf[numspan][i] >= 32 && wc->sig_rx_buf[numspan][i] <=127)
            {}
        }
        if(wc->check_start[numspan]==1)
        {
            if(((wc->sig_rx_buf[numspan][0] == 'A') && (wc->sig_rx_buf[numspan][1] == 'T'))||
               ((wc->sig_rx_buf[numspan][0] == 'O')&&(wc->sig_rx_buf[numspan][1] == 'K')))
            {
                if(debug)
                   printk("gsm start and recv atok now\n");
                wc->check_start[numspan] = 0;
                wc->queue_power_reset_flag= 1;
				wake_up_interruptible(&Queue_poweron_reset);
            }
        }
        if(flag >= 2  || (wc->check_start[numspan]==1))
        {
            if(wc->check_start[numspan]==1)
            {
                 wc->sig_rx_idex[numspan] = 0;
                 memset(wc->sig_rx_buf[numspan],0x0,sizeof(wc->sig_rx_buf[numspan]));
            }
            else
            {
                for(i = 0;i< wc->sig_rx_idex[numspan]; i++)
                {
                    if((wc->sig_rx_buf[numspan][i] == 0xa) ||
                       (!strncasecmp(wc->last_cmd,"AT+CMGS",strlen("AT+CMGS"))  &&
                       (wc->sig_rx_buf[numspan][i] == 0x20) && (wc->sig_rx_buf[numspan][i-1] == 0x3e )))
                    {
                        length = i + 1;
                        dahdi_hdlc_putbuf(wc->sigchan[numspan], wc->sig_rx_buf[numspan],length);
                        dahdi_hdlc_finish(wc->sigchan[numspan]);
                        if(debug>2)
                            printk("recv end char =%x\n",wc->sig_rx_buf[numspan][i]);
                        if(wc->sig_rx_idex[numspan] >= length)
                        {
                            wc->sig_rx_idex[numspan] -= length ;
                            i = 0;
                            memmove(&wc->sig_rx_buf[numspan][0], &wc->sig_rx_buf[numspan][length], wc->sig_rx_idex[numspan]);
                        }
                        else
                        {
                            wc->sig_rx_idex[numspan] = 0;
                            memset(wc->sig_rx_buf[numspan],0x0,sizeof(wc->sig_rx_buf[numspan]));
                        }
                   }
               }
            }
        }
        if (wc->last_int_val == AXGCOM_START_AT_CMD_COUNT)
        {
  	        if(wc->check_start[numspan]==1)
            {
		        wc->check_start[numspan] = 0;
		        wc->queue_power_reset_flag = 1;
                if(debug)
                  printk("cheknumber and retSET check start 0\n");
		        wake_up_interruptible(&Queue_poweron_reset);
            }
	    }
	 }
     if(wc->modtype[offset] == MOD_TYPE_GSM)
     {
         if (wc->cardflag & (1 << offset))
            dahdi_ec_chunk(wc->chans[numspan][0], wc->chans[numspan][0]->readchunk, wc->chans[numspan][0]->writechunk);
     }
	 else
     {
         for(x = 0; x < wc->analog_chans; x++)
         {
	         if (wc->cardflag & (1 << x))
		          dahdi_ec_chunk(wc->chans[numspan][x], wc->chans[numspan][x]->readchunk, wc->chans[numspan][x]->writechunk);
         }
     }
 }

 static void axgcom_tdm_encode(struct axgcom *wc, int numspan)//trans
 {
      int x=0;
      static unsigned char buf[AXGCOM_ATCMD_BUF_LEN];
      int size = sizeof(buf)/sizeof(buf[0]);
      int res = 0;
      int offset  =0;
      unsigned int * write_chunk_int = wc->axspan[numspan].writechunk;
      memset(buf, 0 ,sizeof(buf));
      memset(write_chunk_int,0x0,64);//order by byte 
      if(wc->span_type == 1)//ax4g
            offset = 2*numspan ;
      else if(wc->span_type == 2)//ax2g4a
      {
            if(numspan < 2)
                offset =wc->offset_span+2*numspan ;
            else
                offset = 0;
      } 
      if(!wc || (numspan >= NUM_SPANS)  || !write_chunk_int)
            return ;
      if((wc->modtype[offset]==MOD_TYPE_GSM) )
      {
          if( (wc->check_start[numspan] == 1) && (wc->power_on_flag==0) )
          {
               if( (wc->last_int_val < AXGCOM_START_AT_CMD_COUNT)&&((wc->intcount & 0xFF) == 0xfa ))
               {
                    memcpy(buf, "AT\r\n", 4);
                    res = 4;
                    wc->last_int_val++;
                    if(debug>2)
                        printk("send atcmd int=%d num=%d\n", wc->last_int_val,numspan);
               }
               else
                 res = 0;
            }
            else if((wc->power_on_flag==1) && (atomic_read(&wc->axspan[numspan].hdlc_pending)))
            {
                 res = 0;
                 dahdi_hdlc_getbuf(wc->sigchan[numspan], buf, &size);
                 if( (size >0) && (size < AXGCOM_ATCMD_BUF_LEN) )
                      res = size;
                 atomic_dec(&wc->axspan[numspan].hdlc_pending);
            }
            if(res > 0)
            {
                if(res < DAHDI_CHUNKSIZE)
                     res = DAHDI_CHUNKSIZE;//AXGCOM_ATCMD_BUF_LEN
                if( (wc->sig_tx_idex[numspan] + res+2)  >= sizeof(wc->sig_tx_buf[numspan] ))
                {
                     if(debug)
                          printk(" sig buf is full,numspan=%d\n",numspan);
                }
                else
                {
                      memcpy(&wc->sig_tx_buf[numspan][wc->sig_tx_idex[numspan]], buf, res);
                      memcpy(wc->last_cmd, buf, sizeof(wc->last_cmd));
				      wc->sig_tx_idex[numspan] += res;
                 }
            }
            else//have no at cmd send 0
            {
                 memset(buf, 0 ,sizeof(buf));
                 if(wc->sig_tx_idex[numspan] < DAHDI_CHUNKSIZE)
                 {
                     res = DAHDI_CHUNKSIZE - wc->sig_tx_idex[numspan];
                     memcpy(&wc->sig_tx_buf[numspan][wc->sig_tx_idex[numspan]], buf, res);//8
                     wc->sig_tx_idex[numspan] += res; //����8BYTES
                 }
            }
       }
	   if(wc->span_type==2)//ax2g4a, 0-31 first two channels, 64--92 anglog four lines,
 	   {
		    for (x = 0; x < DAHDI_CHUNKSIZE; x++)
			{
			    switch (numspan)
			    {
					case 0:
						if ( (wc->cardflag & (1 << 4)) )
						{
				           dahdi_x_to_line(wc, wc->chans[numspan][0],  numspan);
				    	   write_chunk_int[2*x] |= wc->tx_buf_linear[numspan][x]<< 16;
                           write_chunk_int[2*x] |= wc->sig_tx_buf[numspan][x]<<8;
                        }
				        break;
                    case 1:
					    if (wc->cardflag & (1 << 6))
						{
						    dahdi_x_to_line(wc, wc->chans[numspan][0],  numspan);
						    write_chunk_int[2*x] |= wc->tx_buf_linear[numspan][x]<<16;
                            write_chunk_int[2*x] |= wc->sig_tx_buf[numspan][x]<<8;
                        }
                        break;
                    case 2:
						if (wc->cardflag & (1 << 0))
							write_chunk_int[2*x] |= (wc->chans[numspan][0]->writechunk[x]);//send first
					    if (wc->cardflag & (1 << 1))
							write_chunk_int[2*x] |= (wc->chans[numspan][1]->writechunk[x] << 8);
						if (wc->cardflag & (1 << 2))
							write_chunk_int[2*x] |= (wc->chans[numspan][2]->writechunk[x] << 16);
						if (wc->cardflag & (1 << 3))
							write_chunk_int[2*x] |= (wc->chans[numspan][3]->writechunk[x] << 24);
                        break;
                   default:
                        break;
                }
            }
		}
		else if(wc->span_type==1)//ax4g
		{
		    for (x = 0; x < DAHDI_CHUNKSIZE; x++)
			{
			     switch (numspan)
			     {
                     case 0:
						if (wc->cardflag & (1 << 0)) 
						{
						     dahdi_x_to_line(wc, wc->chans[numspan][0],  numspan);
                             write_chunk_int[x*2] |= wc->tx_buf_linear[numspan][x] << 16;
                             write_chunk_int[x*2] |= wc->sig_tx_buf[numspan][x]<<8;
                        }
                        break;
                     case 1:
                        if (wc->cardflag & (1 << 2))
						{
						    dahdi_x_to_line(wc, wc->chans[numspan][0],  numspan);
						    write_chunk_int[x*2] |= wc->tx_buf_linear[numspan][x]<<16;
                            write_chunk_int[x*2]|= wc->sig_tx_buf[numspan][x]<<8;
                        }
                        break;
                      case 2:
                        if (wc->cardflag & (1 << 4))
						{
						    dahdi_x_to_line(wc, wc->chans[numspan][0],  numspan);
						    write_chunk_int[x*2] |= wc->tx_buf_linear[numspan][x] << 16;
                            write_chunk_int[x*2]|= wc->sig_tx_buf[numspan][x]<<8;
                         }
                         break;
                      case 3:
                         if (wc->cardflag & (1 << 6))
 						 {
						    dahdi_x_to_line(wc, wc->chans[numspan][0],  numspan);
						    write_chunk_int[x*2] |= wc->tx_buf_linear[numspan][x]<<16;
                            write_chunk_int[x*2]|= wc->sig_tx_buf[numspan][x]<<8;
                         }
                         break;
                     default:
                         break;
                  }
			   }
			}
            else//800pe
            {
               for (x = 0; x < DAHDI_CHUNKSIZE; x++)
			   {
 				      if(wc->cardflag & (1 << 0))
							write_chunk_int[x*2] |= (wc->chans[numspan][0]->writechunk[x]);//send first
                      if(wc->cardflag & (1 << 1))
						  write_chunk_int[x*2] |= (wc->chans[numspan][1]->writechunk[x] << 8);
				      if(wc->cardflag & (1 << 2))
						  write_chunk_int[x*2] |= (wc->chans[numspan][2]->writechunk[x] << 16);
				      if (wc->cardflag & (1 << 3))
					      write_chunk_int[x*2] |= (wc->chans[numspan][3]->writechunk[x] << 24);
                      if (wc->cardflag & (1 << 4))
  					      write_chunk_int[x*2+1] |= (wc->chans[numspan][4]->writechunk[x]);
                      if (wc->cardflag & (1 << 5))
					      write_chunk_int[x*2+1] |= (wc->chans[numspan][5]->writechunk[x] << 8);
                      if (wc->cardflag & (1 << 6))
					      write_chunk_int[x*2+1] |= (wc->chans[numspan][6]->writechunk[x] << 16);
                      if (wc->cardflag & (1 << 7))
					      write_chunk_int[x*2+1] |= (wc->chans[numspan][7]->writechunk[x] << 24);
 			    }
            }
            if(wc->modtype[offset] == MOD_TYPE_GSM)
            {
                 if(wc->sig_tx_idex[numspan] >= DAHDI_CHUNKSIZE)
                       wc->sig_tx_idex[numspan] -= DAHDI_CHUNKSIZE;
                 if(wc->sig_tx_idex[numspan] > 0)
                        memmove(&wc->sig_tx_buf[numspan][0], &wc->sig_tx_buf[numspan][DAHDI_CHUNKSIZE], wc->sig_tx_idex[numspan]);
            }
    }

 static inline void __receive_span(struct axgcom_span *ts,int num,struct axgcom *wc)
 {

       prefetch((void *)(ts->readchunk));//3//6 int max 8 int 
       prefetch((void *)(ts->writechunk));
	   prefetch((void *)(ts->readchunk+8));//3//6 int max 8 int
       prefetch((void *)(ts->writechunk+8));
 }

 static  void  axgcom_tdm_run(struct axgcom *wc, unsigned int ints)
 {
       int x;
       struct dahdi_span *tmp_span = NULL;
       struct axgcom_span *gcom = NULL;
       for(x = 0; x<wc->sumspan; x++)
       {
            if( (wc->spanflag & (1<<x)) )
            {
                 tmp_span = &wc->axspan[x].span;
                 gcom =&wc->axspan[x];
                 dahdi_transmit(tmp_span);
                 axgcom_tdm_encode(wc, x);//MAY BE PART TO HDLC
                 __receive_span(&wc->axspan[x],x,wc);
                 axgcom_tdm_decode(wc, x);
                 dahdi_receive(tmp_span);
            }
        }
 }




 DAHDI_IRQ_HANDLER(axgcom_interrupt)
 {
	struct axgcom *wc = dev_id;
	unsigned int ints;
    ints = __axgcom_pci_in(wc, WC_INTR);//CLEAR INTR
	/* Ignore if it's not for us */
	if (!(ints & 0x7))
		return IRQ_RETVAL(1);
	//add for ax2g4a
	if (ints & 0x02) 
    {
       if(wc->span_type != 1)
          analog_irq_handle1(wc);
       wc->intcount++;
       if(wc->span_type != 1)
             ax2g4a_irq_handle3(wc);
       axgcom_tdm_run(wc,  ints);
    }
    else
       printk("intr state is invalid\n");
     return IRQ_RETVAL(1);    
 }

 static int axgcom_GSM_insane(struct axgcom *wc, int card)
 {
    unsigned char blah = 0;
	blah = axgcom_framer_in(wc, card, AXGCOM_SBORAD_ACTIVE_REG);
	if (blah != 0x0a)
	{
		printk("read-GSM-reg = 0x%x\n", blah);
		return -2;
	}
	printk("VoiceGSM System:%02x\n", blah & 0xf);
	return 0;
 }

 static int wctdm_voicedaa_insane(struct axgcom *wc, int card)
 {
	int blah;
	blah = axgcom_framer_in(wc, card, 2);
	printk("voicedaa reg2 = 0x%x\n",blah);
	if (blah != 0x3)
		return -2;
	blah = axgcom_framer_in(wc, card, 11);
	if (debug)
		printk(KERN_DEBUG "VoiceDAA System: %02x\n", blah & 0xf);
	return 0;
 }

 static int axgcom_proslic_insane(struct axgcom *wc, int card)
 {
	int blah,insane_report;
	insane_report=0;
	blah = axgcom_framer_in(wc, card, 0);
	if (debug) 
		printk(KERN_DEBUG "ProSLIC on module %d, product %d, version %d\n", card, (blah & 0x30) >> 4, (blah & 0xf));
	if (((blah & 0xf) == 0) || ((blah & 0xf) == 0xf))
		return -1;
	if ((blah & 0xf) < 2)
	{
		printk(KERN_NOTICE "ProSLIC 3210 version %d is too old\n", blah & 0xf);
		return -1;
	}
	if (axgcom_framer_in(wc, card, 1) & 0x80) /* ProSLIC 3215, not a 3210 */
		wc->flags[card] |= FLAG_3215;
	blah = axgcom_framer_in(wc, card, 8);
    if (blah != 0x2)
    {
		printk(KERN_NOTICE "ProSLIC on module %d insane (1) %d should be 2\n", card, blah);
		return -1;
	}
    else if (insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 8 Reads %d Expected is 0x2\n",card,blah);
	blah = axgcom_framer_in(wc, card, 64);
    if (blah != 0x0)
    {
		printk(KERN_NOTICE "ProSLIC on module %d insane (2)\n", card);
		return -1;
	}
    else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 64 Reads %d Expected is 0x0\n",card,blah);
	blah = axgcom_framer_in(wc, card, 11);
	if (blah != 0x33)
	{
		printk(KERN_NOTICE "ProSLIC on module %d insane (3)\n", card);
		return -1;
	}
	else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 11 Reads %d Expected is 0x33\n",card,blah);
	/* Just be sure it's setup right. */
	axgcom_framer_out(wc, card, 30, 0);
	if (debug) 
		printk(KERN_DEBUG "ProSLIC on module %d seems sane.\n", card);
	return 0;
 }

 static int axgcom_proslic_powerleak_test(struct axgcom *wc, int card)
 {
	unsigned long origjiffies;
	unsigned char vbat;
	/* Turn off linefeed */
	axgcom_framer_out(wc, card, 64, 0);
	/* Power down */
	axgcom_framer_out(wc, card, 14, 0x10);
	/* Wait for one second */
	origjiffies = jiffies;
	while((vbat = axgcom_framer_in(wc, card, 82)) > 0x6)
	{
		if ((jiffies - origjiffies) >= (HZ/2))
			break;;
	}
	if (vbat < 0x06)
	{
		printk(KERN_NOTICE "Excessive leakage detected on module %d: %d volts (%02x) after %d ms\n", card,
		       376 * vbat / 1000, vbat, (int)((jiffies - origjiffies) * 1000 / HZ));
		return -1;
	}
	else if (debug)
	{
		printk(KERN_NOTICE "Post-leakage voltage: %d volts\n", 376 * vbat / 1000);
	}
	return 0;
 }

 static int axgcom_powerup_proslic(struct axgcom *wc, int card, int fast)
 {
	unsigned char vbat;
	unsigned long origjiffies;
	int lim;
	/* Set period of DC-DC converter to 1/64 khz */
	axgcom_framer_out(wc, card, 92, 0xff /* was 0xff */);
	/* Wait for VBat to powerup */
	origjiffies = jiffies;
	/* Disable powerdown */
	axgcom_framer_out(wc, card, 14, 0);
	/* If fast, don't bother checking anymore */
	if (fast)
		return 0;
	while((vbat = axgcom_framer_in(wc, card, 82)) < 0xc0)
	{
		/* Wait no more than 500ms */
		if ((jiffies - origjiffies) > HZ/2)
			break;
	}
	if (vbat < 0xc0)
	{
		if (wc->proslic_power == PROSLIC_POWER_UNKNOWN)
			 printk(KERN_NOTICE "ProSLIC on module %d failed to powerup within %d ms (%d mV only)\n\n -- DID YOU REMEMBER TO PLUG IN THE HD POWER CABLE TO THE TDM400P??\n",
					card, (int)(((jiffies - origjiffies) * 1000 / HZ)),vbat * 375);
		wc->proslic_power = PROSLIC_POWER_WARNED;
		return -1;
	}
	else if (debug)
		printk(KERN_DEBUG "ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
	wc->proslic_power = PROSLIC_POWER_ON;
 /* Proslic max allowed loop current, reg 71 LOOP_I_LIMIT */
 /* If out of range, just set it to the default value     */
    lim = (loopcurrent - 20) / 3;
    if ( loopcurrent > 41 )
    {
       lim = 0;
       if (debug)
            printk(KERN_DEBUG "Loop current out of range! Setting to default 20mA!\n");
    }
    else if (debug)
         printk(KERN_DEBUG "Loop current set to %dmA!\n",(lim*3)+20);
    axgcom_framer_out(wc,card,LOOP_I_LIMIT,lim);

    /* Engage DC-DC converter */
	axgcom_framer_out(wc, card, 93, 0x19);
	return 0;
 }

 static int axgcom_proslic_manual_calibrate(struct axgcom *wc, int card)
 {
	unsigned long origjiffies;
	unsigned char i;
	axgcom_framer_out(wc, card, 21, 0);//(0)  Disable all interupts in DR21
	axgcom_framer_out(wc, card, 22, 0);//(0)Disable all interupts in DR21
	axgcom_framer_out(wc, card, 23, 0);//(0)Disable all interupts in DR21
	axgcom_framer_out(wc, card, 64, 0);//(0)
	axgcom_framer_out(wc, card, 97, 0x18); //(0x18)Calibrations without the ADC and DAC offset and without common mode calibration.
	axgcom_framer_out(wc, card, 96, 0x47); //(0x47)	Calibrate common mode and differential DAC mode DAC + ILIM
	origjiffies=jiffies;
	while( axgcom_framer_in(wc,card,96)!=0 )
	{
		if((jiffies-origjiffies)>80)
			return -1;
	}
//Initialized DR 98 and 99 to get consistant results.
// 98 and 99 are the results registers and the search should have same intial conditions.
// Delay 10ms
	origjiffies=jiffies; 
	while((jiffies-origjiffies)<1);
	axgcom_proslic_setreg_indirect(wc, card, 88, 0);
	axgcom_proslic_setreg_indirect(wc, card, 89, 0);
	axgcom_proslic_setreg_indirect(wc, card, 90, 0);
	axgcom_proslic_setreg_indirect(wc, card, 91, 0);
	axgcom_proslic_setreg_indirect(wc, card, 92, 0);
	axgcom_proslic_setreg_indirect(wc, card, 93, 0);
	axgcom_framer_out(wc, card, 98, 0x10); // This is necessary if the calibration occurs other than at reset time
	axgcom_framer_out(wc, card, 99, 0x10);
	for ( i=0x1f; i>0; i--)
	{
		axgcom_framer_out(wc, card, 98, i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((axgcom_framer_in(wc, card, 88)) == 0)
			break;
	}
	for ( i=0x1f; i>0; i--)
	{
		axgcom_framer_out(wc, card, 99, i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((axgcom_framer_in(wc, card, 89)) == 0)
			break;
	}
	axgcom_framer_out(wc,card,64,1);
	while((jiffies-origjiffies)<10); // Sleep 100?
	axgcom_framer_out(wc, card, 64, 0);
	axgcom_framer_out(wc, card, 23, 0x4);  // enable interrupt for the balance Cal
	axgcom_framer_out(wc, card, 97, 0x1); // this is a singular calibration bit for longitudinal calibration
	axgcom_framer_out(wc, card, 96, 0x40);
	axgcom_framer_in(wc, card, 96); /* Read Reg 96 just cause */
	axgcom_framer_out(wc, card, 21, 0xFF);
	axgcom_framer_out(wc, card, 22, 0xFF);
	axgcom_framer_out(wc, card, 23, 0xFF);
	/**The preceding is the longitudinal Balance Cal***/
	return(0);
 }

 static int axgcom_proslic_calibrate(struct axgcom *wc, int card)
 {
	unsigned long origjiffies;
	int x;
	/* Perform all calibrations */
	axgcom_framer_out(wc, card, 97, 0x1f);
	/* Begin, no speedup */
	axgcom_framer_out(wc, card, 96, 0x5f);
	/* Wait for it to finish */
	origjiffies = jiffies;
	while(axgcom_framer_in(wc, card, 96))
	{
		if ((jiffies - origjiffies) > 2 * HZ)
		{
			printk(KERN_NOTICE "Timeout waiting for calibration of module %d\n", card);
			return -1;
		}
	}
	if (debug)
	{
		/* Print calibration parameters */
		printk(KERN_DEBUG "Calibration Vector Regs 98 - 107: \n");
		for (x=98;x<108;x++)
			printk(KERN_DEBUG "%d: %02x\n", x, axgcom_framer_in(wc, card, x));
	}
	return 0;
 }

 static void wait_just_a_bit(int foo)
 {
	long newjiffies;
	newjiffies = jiffies + foo;
	while(jiffies < newjiffies);
 }

/*********************************************************************
 * Set the hwgain on the analog modules
 *
 * card = the card position for this module (0-23)
 * gain = gain in dB x10 (e.g. -3.5dB  would be gain=-35)
 * tx = (0 for rx; 1 for tx)
 *
 *******************************************************************/
 static int axgcom_set_hwgain(struct axgcom *wc, int card, __s32 gain, __u32 tx)
 {
	if (!(wc->modtype[card] == MOD_TYPE_FXO))
	{
		printk(KERN_NOTICE "Cannot adjust gain.  Unsupported module type!\n");
		return -1;
	}
	if (tx)
	{
		if (debug)
			printk(KERN_DEBUG "setting FXO tx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0)
		{
			axgcom_framer_out(wc, card, 38, 16 + (gain/-10));
			axgcom_framer_out(wc, card, 40, 16 + (-gain%10));
		}
		else if (gain <= 120 && gain > 0)
		{
			axgcom_framer_out(wc, card, 38, gain/10);
			axgcom_framer_out(wc, card, 40, (gain%10));
		}
		else
		{
			printk(KERN_INFO "FXO tx gain is out of range (%d)\n", gain);
			return -1;
		}
	}
	else
	{ /* rx */
		if (debug)
			printk(KERN_DEBUG "setting FXO rx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0)
		{
			axgcom_framer_out(wc, card, 39, 16+ (gain/-10));
			axgcom_framer_out(wc, card, 41, 16 + (-gain%10));
		}
		else if (gain <= 120 && gain > 0)
		{
			axgcom_framer_out(wc, card, 39, gain/10);
			axgcom_framer_out(wc, card, 41, (gain%10));
		}
		else
		{
			printk(KERN_INFO "FXO rx gain is out of range (%d)\n", gain);
			return -1;
		}
	}
	return 0;
 }

 static int set_vmwi(struct axgcom * wc, int chan_idx)
 {
	struct fxs *const fxs = &wc->mod[chan_idx].fxs;
	if (fxs->vmwi_active_messages)
	{
		fxs->vmwi_lrev =(fxs->vmwisetting.vmwi_type & DAHDI_VMWI_LREV) ? 1 : 0;
		fxs->vmwi_hvdc =(fxs->vmwisetting.vmwi_type & DAHDI_VMWI_HVDC) ? 1 : 0;
		fxs->vmwi_hvac =(fxs->vmwisetting.vmwi_type & DAHDI_VMWI_HVAC) ? 1 : 0;
	}
	else
	{
		fxs->vmwi_lrev = 0;
		fxs->vmwi_hvdc = 0;
		fxs->vmwi_hvac = 0;
	}
	if (debug)
		printk(KERN_DEBUG "Setting VMWI on channel %d, messages=%d,lrev=%d, hvdc=%d, hvac=%d\n",chan_idx,fxs->vmwi_active_messages,fxs->vmwi_lrev,fxs->vmwi_hvdc,fxs->vmwi_hvac);
	if (fxs->vmwi_hvac)
	{
		/* Can't change ring generator while in On Hook Transfer mode*/
		if (!fxs->ohttimer)
		{
			if (POLARITY_XOR)
				fxs->idletxhookstate |= SLIC_LF_REVMASK;
			else
				fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
			/* Set ring generator for neon */
			axgcom_set_ring_generator_mode(wc, chan_idx, 1);
			/* Activate ring to send neon pulses */
			fxs->lasttxhook = SLIC_LF_RINGING;
			axgcom_framer_out(wc, chan_idx, LINE_STATE, fxs->lasttxhook);
		}
	}
	else
	{
		if (fxs->neonringing)
		{
			/* Set ring generator for normal ringer */
			axgcom_set_ring_generator_mode(wc, chan_idx, 0);
			/* ACTIVE, polarity determined later */
			fxs->lasttxhook = SLIC_LF_ACTIVE_FWD;
		}
		else if ((fxs->lasttxhook == SLIC_LF_RINGING) || (fxs->lasttxhook == SLIC_LF_OPEN))
		{
			/* Can't change polarity while ringing or when open,
				set idlehookstate instead */
			if (POLARITY_XOR)
				fxs->idletxhookstate |= SLIC_LF_REVMASK;
			else
				fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
			printk(KERN_DEBUG "Unable to change polarity on channel %d, lasttxhook=0x%X\n",chan_idx,fxs->lasttxhook);
			return 0;
		}
		if (POLARITY_XOR)
		{
			fxs->idletxhookstate |= SLIC_LF_REVMASK;
			fxs->lasttxhook |= SLIC_LF_REVMASK;
		}
		else
		{
			fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
			fxs->lasttxhook &= ~SLIC_LF_REVMASK;
		}
		axgcom_framer_out(wc, chan_idx, LINE_STATE, fxs->lasttxhook);
	}
	return 0;
 }

 static int axgcom_init_GSM(struct axgcom *wc, int card, int fast, int manual, int sane)
 {
    // int timeslot;
    printk("axgcom_init_GSM CARD:%d\n", card);
	wc->modtype[card] = MOD_TYPE_GSM;
	if (!sane && axgcom_GSM_insane(wc, card))
		return -2;
    wc ->GSM_BOARD_version[card] = axgcom_framer_in(wc,card,AXGCOM_SBORAD_VERSION);
	printk("read small board version =%0x\n",wc ->GSM_BOARD_version[card] );
	return 0;
 }

 static int  axgcom_reset_spi(struct axgcom *wc, int card)
 {
      int blah;
	  blah = axgcom_framer_in(wc, card, 1);
      blah = axgcom_framer_in(wc, card, 2);
      blah = axgcom_framer_in(wc, card, 2);
      return 0;
 }

 static int axgcom_init_voicedaa(struct axgcom *wc, int card, int fast, int manual, int sane)
 {
	unsigned char reg16=0, reg26=0, reg30=0, reg31=0;
	long newjiffies;
    axgcom_reset_spi(wc,  card);
	if (!sane && wctdm_voicedaa_insane(wc, card))
	     return -2;
	/* Software reset */
	axgcom_framer_out(wc, card, 1, 0x80);
	/* Wait just a bit */
	wait_just_a_bit(HZ/10);
	/* Enable PCM, ulaw */
	if (alawoverride)
		axgcom_framer_out(wc, card, 33, 0x20);
	else
		axgcom_framer_out(wc, card, 33, 0x28);
	/* Set On-hook speed, Ringer impedence, and ringer threshold */
	reg16 |= (fxo_modes[_opermode].ohs << 6);
	reg16 |= (fxo_modes[_opermode].rz << 1);
	reg16 |= (fxo_modes[_opermode].rt);
	axgcom_framer_out(wc, card, 16, reg16);
	if(fwringdetect)
	{
		/* Enable ring detector full-wave rectifier mode */
		axgcom_framer_out(wc, card, 18, 2);
		axgcom_framer_out(wc, card, 24, 0);
	}
	else
	{
		/* Set to the device defaults */
		axgcom_framer_out(wc, card, 18, 0);
		axgcom_framer_out(wc, card, 24, 0x19);
	}
	/* Set DC Termination:
	   Tip/Ring voltage adjust, minimum operational current, current limitation */
	reg26 |= (fxo_modes[_opermode].dcv << 6);
	reg26 |= (fxo_modes[_opermode].mini << 4);
	reg26 |= (fxo_modes[_opermode].ilim << 1);
	axgcom_framer_out(wc, card, 26, reg26);
	/* Set AC Impedence */
	reg30 = (fxo_modes[_opermode].acim);
	axgcom_framer_out(wc, card, 30, reg30);
	/* Misc. DAA parameters */
	if (fastpickup)
		reg31 = 0xb3;
	else
		reg31 = 0xa3;
	reg31 |= (fxo_modes[_opermode].ohs2 << 3);
	axgcom_framer_out(wc, card, 31, reg31);
    axgcom_framer_out(wc, card, 34, card*8+64*4);
	axgcom_framer_out(wc, card, 36, card*8+64*4);
	/* Set Transmit/Receive timeslot */
	axgcom_framer_out(wc, card, 35, 0x00);
	axgcom_framer_out(wc, card, 37, 0x00);
	/* Enable ISO-Cap */
	axgcom_framer_out(wc, card, 6, 0x00);
	if (fastpickup)
		axgcom_framer_out(wc, card, 17, axgcom_framer_in(wc, card, 17) | 0x20);

	/* Wait 1000ms for ISO-cap to come up */
	newjiffies = jiffies;
	newjiffies += 2 * HZ;
	while((jiffies < newjiffies) && !(axgcom_framer_in(wc, card, 11) & 0xf0))
		wait_just_a_bit(HZ/10);
	if (!(axgcom_framer_in(wc, card, 11) & 0xf0))
	{
		printk(KERN_NOTICE "VoiceDAA did not bring up ISO link properly!\n");
		return -1;
	}
	if (debug)
		printk(KERN_DEBUG "ISO-Cap is now up, line side: %02x rev %02x\n",axgcom_framer_in(wc, card, 11) >> 4,(axgcom_framer_in(wc, card, 13) >> 2) & 0xf);
	/* Enable on-hook line monitor */
	axgcom_framer_out(wc, card, 5, 0x08);
	/* Take values for fxotxgain and fxorxgain and apply them to module */
	axgcom_set_hwgain(wc, card, fxotxgain, 1);
	axgcom_set_hwgain(wc, card, fxorxgain, 0);
	/* NZ -- crank the tx gain up by 7 dB */
	if (!strcmp(fxo_modes[_opermode].name, "NEWZEALAND"))
	{
		printk(KERN_INFO "Adjusting gain\n");
		axgcom_set_hwgain(wc, card, 7, 1);
	}
	if(debug)
		printk(KERN_DEBUG "DEBUG fxotxgain:%i.%i fxorxgain:%i.%i\n", (axgcom_framer_in(wc, card, 38)/16)?-(axgcom_framer_in(wc, card, 38) - 16) : axgcom_framer_in(wc, card, 38), (axgcom_framer_in(wc, card, 40)/16)? -(axgcom_framer_in(wc, card, 40) - 16):axgcom_framer_in(wc, card, 40), (axgcom_framer_in(wc, card, 39)/16)? -(axgcom_framer_in(wc, card, 39) - 16) : axgcom_framer_in(wc, card, 39),(axgcom_framer_in(wc, card, 41)/16)?-(axgcom_framer_in(wc, card, 41) - 16):axgcom_framer_in(wc, card, 41));
	return 0;
 }


 static int axgcom_init_proslic(struct axgcom *wc, int card, int fast, int manual, int sane)
 {
	unsigned short tmp[5];
	unsigned char r19,r9;
	int x;
	int fxsmode=0;
	struct fxs *const fxs = &wc->mod[card].fxs;
	/* Sanity check the ProSLIC */
	if (!sane && axgcom_proslic_insane(wc, card))
		return -2;
	/* default messages to none and method to FSK */
	memset(&fxs->vmwisetting, 0, sizeof(fxs->vmwisetting));
	fxs->vmwi_lrev = 0;
	fxs->vmwi_hvdc = 0;
	fxs->vmwi_hvac = 0;
	/* By default, don't send on hook */
	if (!reversepolarity != !fxs->reversepolarity)
		fxs->idletxhookstate = SLIC_LF_ACTIVE_REV;
	else
		fxs->idletxhookstate = SLIC_LF_ACTIVE_FWD;
	if (sane) /* Make sure we turn off the DC->DC converter to prevent anything from blowing up */
		axgcom_framer_out(wc, card, 14, 0x10);
	if (axgcom_proslic_init_indirect_regs(wc, card))
	{
		printk(KERN_INFO "Indirect Registers failed to initialize on module %d.\n", card);
		return -1;
	}
	/* Clear scratch pad area */
	axgcom_proslic_setreg_indirect(wc, card, 97,0);
	/* Clear digital loopback */
	axgcom_framer_out(wc, card, 8, 0);
	/* Revision C optimization */
	axgcom_framer_out(wc, card, 108, 0xeb);
	/* Disable automatic VBat switching for safety to prevent
	   Q7 from accidently turning on and burning out. */
	axgcom_framer_out(wc, card, 67, 0x07);  /* Note, if pulse dialing has problems at high REN loads   change this to 0x17 */
	/* Turn off Q7 */
	axgcom_framer_out(wc, card, 66, 1);
	/* Flush ProSLIC digital filters by setting to clear, while saving old values */
	for (x=0;x<5;x++)
	{
		tmp[x] = axgcom_proslic_getreg_indirect(wc, card, x + 35);
		axgcom_proslic_setreg_indirect(wc, card, x + 35, 0x8000);
	}
	/* Power up the DC-DC converter */
	if (axgcom_powerup_proslic(wc, card, fast))
	{
		printk(KERN_NOTICE "Unable to do INITIAL ProSLIC powerup on module %d\n", card);
		return -1;
	}
	if (!fast)
	{
		/* Check for power leaks */
		if (axgcom_proslic_powerleak_test(wc, card))
			printk(KERN_NOTICE "ProSLIC module %d failed leakage test.  Check for short circuit\n", card);
		/* Power up again */
		if (axgcom_powerup_proslic(wc, card, fast))
		{
			printk(KERN_NOTICE "Unable to do FINAL ProSLIC powerup on module %d\n", card);
			return -1;
		}
		/* Perform calibration */
		if(manual)
		{
			if (axgcom_proslic_manual_calibrate(wc, card))
			{
				if (axgcom_proslic_manual_calibrate(wc, card))
				{
					printk(KERN_NOTICE "Proslic Failed on Second Attempt to Calibrate Manually. (Try -DNO_CALIBRATION in Makefile)\n");
					return -1;
				}
				printk(KERN_NOTICE "Proslic Passed Manual Calibration on Second Attempt\n");
			}
		}
		else
		{
			if(axgcom_proslic_calibrate(wc, card))
			{
				if (axgcom_proslic_calibrate(wc, card))
				{
					printk(KERN_NOTICE "Proslic Failed on Second Attempt to Auto Calibrate\n");
					return -1;
				}
				printk(KERN_NOTICE "Proslic Passed Auto Calibration on Second Attempt\n");
			}
		}
		/* Perform DC-DC calibration */
		axgcom_framer_out(wc, card, 93, 0x99);
		r19 = axgcom_framer_in(wc, card, 107);
		if ((r19 < 0x2) || (r19 > 0xd))
		{
			printk(KERN_NOTICE "DC-DC cal has a surprising direct 107 of 0x%02x!\n", r19);
			axgcom_framer_out(wc, card, 107, 0x8);
		}
		/* Save calibration vectors */
		for (x=0;x<NUM_CAL_REGS;x++)
			fxs->calregs.vals[x] = axgcom_framer_in(wc, card, 96 + x);
	}
	else
	{
		/* Restore calibration registers */
		for (x=0;x<NUM_CAL_REGS;x++)
			axgcom_framer_out(wc, card, 96 + x, fxs->calregs.vals[x]);
	}
	/* Calibration complete, restore original values */
	for (x=0;x<5;x++)
		axgcom_proslic_setreg_indirect(wc, card, x + 35, tmp[x]);
	if (axgcom_proslic_verify_indirect_regs(wc, card))
	{
		printk(KERN_INFO "Indirect Registers failed verification.\n");
		return -1;
	}
    if (alawoverride)
    	axgcom_framer_out(wc, card, 1, 0x20);
    else
    	axgcom_framer_out(wc, card, 1, 0x28);
    axgcom_framer_out(wc, card, 2, card*8+64*4);//is card,card first recv first
    axgcom_framer_out(wc, card, 4, card*8+64*4);
    axgcom_framer_out(wc, card, 3, 0);    // Tx Start count high byte 0
    axgcom_framer_out(wc, card, 5, 0);    // Rx Start count high byte 0
    axgcom_framer_out(wc, card, 18, 0xff);     // clear all interrupt
    axgcom_framer_out(wc, card, 19, 0xff);
    axgcom_framer_out(wc, card, 20, 0xff);
    axgcom_framer_out(wc, card, 73, 0x04);
	if (fxshonormode)
	{
		fxsmode = acim2tiss[fxo_modes[_opermode].acim];
		axgcom_framer_out(wc, card, 10, 0x08 | fxsmode);
	}
    if (lowpower)
    	axgcom_framer_out(wc, card, 72, 0x10);
	if (axgcom_init_ring_generator_mode(wc, card))
		return -1;
	if(fxstxgain || fxsrxgain)
	{
		r9 = axgcom_framer_in(wc, card, 9);
		switch (fxstxgain)
		{
			case 35:
				r9+=8;
				break;
			case -35:
				r9+=4;
				break;
			case 0: 
				break;
		}
		switch (fxsrxgain)
		{
			case 35:
				r9+=2;
				break;
			case -35:
				r9+=1;
				break;
			case 0:
				break;
		}
		axgcom_framer_out(wc,card,9,r9);
	}
	if(debug)
			printk(KERN_DEBUG "DEBUG: fxstxgain:%s fxsrxgain:%s\n",((axgcom_framer_in(wc, card, 9)/8) == 1)?"3.5":(((axgcom_framer_in(wc,card,9)/4) == 1)?"-3.5":"0.0"),((axgcom_framer_in(wc, card, 9)/2) == 1)?"3.5":((axgcom_framer_in(wc,card,9)%2)?"-3.5":"0.0"));
	fxs->lasttxhook = fxs->idletxhookstate;
	axgcom_framer_out(wc, card, LINE_STATE, fxs->lasttxhook);
	return 0;
 }

 static int axgcom_ioctl(struct dahdi_chan *chan, unsigned int cmd, unsigned long data)
 {
	struct wctdm_stats stats;
	struct wctdm_regs regs;
	struct wctdm_regop regop;
	struct wctdm_echo_coefs echoregs;
	struct dahdi_hwgain hwgain;
	struct axgcom_span *span = chan->pvt;
    struct axgcom *wc = NULL;
    struct fxs *fxs;
    int x;
    if(span)
        wc = span->owner;
    else
        return 0;
    fxs = &wc->mod[chan->chanpos - 1].fxs;
	switch (cmd)
	{
	  case DAHDI_ONHOOKTRANSFER:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		fxs->ohttimer = x << 3;
		/* Active mode when idle */
		fxs->idletxhookstate = POLARITY_XOR ?SLIC_LF_ACTIVE_REV : SLIC_LF_ACTIVE_FWD;
		if (fxs->neonringing)
		{
			/* keep same Forward polarity */
			fxs->lasttxhook = SLIC_LF_OHTRAN_FWD;
			axgcom_framer_out(wc, chan->chanpos - 1,LINE_STATE, fxs->lasttxhook);
		}
		else if (fxs->lasttxhook == SLIC_LF_ACTIVE_FWD ||  fxs->lasttxhook == SLIC_LF_ACTIVE_REV)
		{
			/* Apply the change if appropriate */
			fxs->lasttxhook = POLARITY_XOR ?SLIC_LF_OHTRAN_REV : SLIC_LF_OHTRAN_FWD;
			axgcom_framer_out(wc, chan->chanpos - 1,LINE_STATE, fxs->lasttxhook);
		}
		break;
	case DAHDI_SETPOLARITY:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		/* Can't change polarity while ringing or when open */
		if ((fxs->lasttxhook == SLIC_LF_RINGING) || (fxs->lasttxhook == SLIC_LF_OPEN))
			return -EINVAL;
		fxs->reversepolarity = x;
		if (POLARITY_XOR)
			fxs->lasttxhook |= SLIC_LF_REVMASK;
		else
			fxs->lasttxhook &= ~SLIC_LF_REVMASK;
		axgcom_framer_out(wc, chan->chanpos - 1,LINE_STATE, fxs->lasttxhook);
		break;
	case DAHDI_VMWI_CONFIG:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (copy_from_user(&(fxs->vmwisetting), (__user void *) data,sizeof(fxs->vmwisetting)))
			return -EFAULT;
		set_vmwi(wc, chan->chanpos - 1);
		break;
	case DAHDI_VMWI:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		if (0 > x)
			return -EFAULT;
		fxs->vmwi_active_messages = x;
		set_vmwi(wc, chan->chanpos - 1);
		break;
	case WCTDM_GET_STATS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS)
		{
			stats.tipvolt = axgcom_framer_in(wc, chan->chanpos - 1, 80) * -376;
			stats.ringvolt = axgcom_framer_in(wc, chan->chanpos - 1, 81) * -376;
			stats.batvolt = axgcom_framer_in(wc, chan->chanpos - 1, 82) * -376;
		}
		else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO)
		{
			stats.tipvolt = (signed char)axgcom_framer_in(wc, chan->chanpos - 1, 29) * 1000;
			stats.ringvolt = (signed char)axgcom_framer_in(wc, chan->chanpos - 1, 29) * 1000;
			stats.batvolt = (signed char)axgcom_framer_in(wc, chan->chanpos - 1, 29) * 1000;
		}
		else
			return -EINVAL;
		if (copy_to_user((__user void *)data, &stats, sizeof(stats)))
			return -EFAULT;
		break;
	case WCTDM_GET_REGS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS)
		{
			for (x=0;x<NUM_INDIRECT_REGS;x++)
				regs.indirect[x] = axgcom_proslic_getreg_indirect(wc, chan->chanpos -1, x);
			for (x=0;x<NUM_REGS;x++)
				regs.direct[x] = axgcom_framer_in(wc, chan->chanpos - 1, x);
		}
		else
		{
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<NUM_FXO_REGS;x++)
				regs.direct[x] = axgcom_framer_in(wc, chan->chanpos - 1, x);
		}
		if (copy_to_user((__user void *)data, &regs, sizeof(regs)))
			return -EFAULT;
		break;
	case WCTDM_SET_REG:
		if(copy_from_user(&regop, (__user void *) data, sizeof(regop)))
			return -EFAULT;
		if (regop.indirect)
		{
		    if(wc->modtype[chan->chanpos - 1]!=MOD_TYPE_FXS)
				return -EINVAL;
			printk(KERN_INFO "Setting indirect %d to 0x%04x on %d\n", regop.reg, regop.val, chan->chanpos);
			axgcom_proslic_setreg_indirect(wc, chan->chanpos - 1, regop.reg, regop.val);
		}
		else
		{
			regop.val &= 0xff;
			printk(KERN_INFO "Setting direct %d to %04x on %d\n", regop.reg, regop.val, chan->chanpos);
			axgcom_framer_out(wc, chan->chanpos - 1, regop.reg, regop.val);
		}
		break;
	case WCTDM_SET_ECHOTUNE:
		printk(KERN_INFO "-- Setting echo registers: \n");
		if(copy_from_user(&echoregs, (__user void *)data, sizeof(echoregs)))
			return -EFAULT;
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO)
		{
			/* Set the ACIM register */
			axgcom_framer_out(wc, chan->chanpos - 1, 30, echoregs.acim);
			/* Set the digital echo canceller registers */
			axgcom_framer_out(wc, chan->chanpos - 1, 45, echoregs.coef1);
			axgcom_framer_out(wc, chan->chanpos - 1, 46, echoregs.coef2);
			axgcom_framer_out(wc, chan->chanpos - 1, 47, echoregs.coef3);
			axgcom_framer_out(wc, chan->chanpos - 1, 48, echoregs.coef4);
			axgcom_framer_out(wc, chan->chanpos - 1, 49, echoregs.coef5);
			axgcom_framer_out(wc, chan->chanpos - 1, 50, echoregs.coef6);
			axgcom_framer_out(wc, chan->chanpos - 1, 51, echoregs.coef7);
			axgcom_framer_out(wc, chan->chanpos - 1, 52, echoregs.coef8);
			printk(KERN_INFO "-- Set echo registers successfully\n");
			break;
		}
		else
			return -EINVAL;
		break;
	case DAHDI_SET_HWGAIN:
		if (copy_from_user(&hwgain, (__user void *) data, sizeof(hwgain)))
			return -EFAULT;
		axgcom_set_hwgain(wc, chan->chanpos-1, hwgain.newgain, hwgain.tx);
		if (debug)
			printk(KERN_DEBUG "Setting hwgain on channel %d to %d for %s direction\n",chan->chanpos-1, hwgain.newgain, hwgain.tx ? "tx" : "rx");
		break;
	default:
		return -ENOTTY;
   }
   return 0;
 }


 static inline struct axgcom_span *axgcom_from_span(struct dahdi_span *span)
 {
        return container_of(span, struct axgcom_span, span);
 }

 static int axgcom_open(struct dahdi_chan *chan)
 {
	struct axgcom_span *span = chan->pvt;
    struct axgcom *wc = NULL;
    if(span)
          wc = span->owner;
    else
          return 0;
	if (wc->dead)
	    return -ENODEV;
	wc->usecount++;
	return 0;
 }

 static int axgcom_watchdog(struct dahdi_span *span, int event)
 {
       struct axgcom_span *axspan = NULL;
       struct dahdi_chan *chan = NULL;
       int spanno;
       spanno = span->offset;
       chan = span->chans[0];
       if(chan)
            axspan = chan->pvt;
       printk(KERN_INFO "TDM: Restarting DMA\n");
       return 0;
 }

 static int axgcom_close(struct dahdi_chan *chan)
 {
	struct axgcom_span *span = chan->pvt;
    struct axgcom *wc = NULL;
    struct fxs *fxs ;
    if(span)
         wc = span->owner;
    else
        return 0;
    fxs = &wc->mod[chan->chanpos - 1].fxs;
	wc->usecount--;
 	if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS)
 	{
		int idlehookstate;
		idlehookstate = POLARITY_XOR ?SLIC_LF_ACTIVE_REV :SLIC_LF_ACTIVE_FWD;
		fxs->idletxhookstate = idlehookstate;
	}
	/* If we're dead, release us now */
	if (!wc->usecount && wc->dead) 
    {
	     printk("axgcom close and release\n");
	     axgcom_release(wc);
    }
    return 0;
 }


 static int axgcom_chanconfig(struct file *file, struct dahdi_chan *chan, int sigtype)
 {
	return 0;
 }

 static int axgcom_spanconfig(struct file *file, struct dahdi_span *span, struct dahdi_lineconfig *lc)
 {

	return 0;
 }

 static int axgcom_startup(struct file *file, struct dahdi_span *span)
 {
     unsigned long flags;
     int run;
     long ms;
     unsigned long origjiffies = jiffies;
     struct axgcom *wc = (axgcom_from_span(span))->owner;
     if(!span || !wc)
        return -1;
     spin_lock_irqsave(&(span->lock),flags);
     run = span->flags & DAHDI_FLAG_RUNNING;
     spin_unlock_irqrestore(&span->lock,flags);
     if(!run)
     {
        spin_lock_irqsave(&(span->lock),flags);
        span->flags |= DAHDI_FLAG_RUNNING;
        spin_unlock_irqrestore(&span->lock,flags);
        //use for dahdi_cfg -vv
        //wait for power
        while(1)
        {
			if (1 == wc->queue_power_on_flag) //RECV SYNC OK NOW
				break;
			if (jiffies_to_msecs(jiffies - origjiffies) >= 11000)
				break;
			AXGCOM_SLEEP_MILLI_SEC(100, ms);
		}
     }
     return 0;
 }

 static int axgcom_shutdown(struct dahdi_span *span)
 {
    printk("axgcom_shutdown\n");
    return 0;
 }


 static void axgcom_hdlc_hard_xmit(struct dahdi_chan *sigchan)
 {
	struct axgcom_span *myspan = sigchan->pvt;
 	atomic_inc(&myspan->hdlc_pending);
 }

 static int axgcom_init_ring_generator_mode(struct axgcom *wc, int card)
 {
	axgcom_framer_out(wc, card, 34, 0x00);	/* Ringing Osc. Control */
	/* neon trapezoid timers */
	axgcom_framer_out(wc, card, 48, 0xe0);	/* Active Timer low byte */
	axgcom_framer_out(wc, card, 49, 0x01);	/* Active Timer high byte */
	axgcom_framer_out(wc, card, 50, 0xF0);	/* Inactive Timer low byte */
	axgcom_framer_out(wc, card, 51, 0x05);	/* Inactive Timer high byte */
	axgcom_set_ring_generator_mode(wc, card, 0);
	return 0;
 }

 static int axgcom_set_ring_generator_mode(struct axgcom *wc, int card, int mode)
 {
	int reg20, reg21, reg74; /* RCO, RNGX, VBATH */
	struct fxs *const fxs = &wc->mod[card].fxs;
	fxs->neonringing = mode;	/* track ring generator mode */
	if (mode)
	{ /* Neon */
		if (debug)
			printk(KERN_DEBUG "NEON ring on chan %d, lasttxhook was 0x%x\n", card, fxs->lasttxhook);
		/* Must be in FORWARD ACTIVE before setting ringer */
		fxs->lasttxhook = SLIC_LF_ACTIVE_FWD;
		axgcom_framer_out(wc, card, LINE_STATE, fxs->lasttxhook);
		axgcom_proslic_setreg_indirect(wc, card, 22,NEON_MWI_RNGY_PULSEWIDTH);
		axgcom_proslic_setreg_indirect(wc, card, 21,0x7bef);	/* RNGX (91.5Vpk) */
		axgcom_proslic_setreg_indirect(wc, card, 20,0x009f);	/* RCO (RNGX, t rise)*/
		axgcom_framer_out(wc, card, 34, 0x19); /* Ringing Osc. Control */
		axgcom_framer_out(wc, card, 74, 0x3f); /* VBATH 94.5V */
		axgcom_proslic_setreg_indirect(wc, card, 29, 0x4600); /* RPTP */
		/* A write of 0x04 to register 64 will turn on the VM led */
	}
	else
	{
		axgcom_framer_out(wc, card, 34, 0x00); /* Ringing Osc. Control */
		/* RNGY Initial Phase */
		axgcom_proslic_setreg_indirect(wc, card, 22, 0x0000);
		axgcom_proslic_setreg_indirect(wc, card, 29, 0x3600); /* RPTP */
		/* A write of 0x04 to register 64 will turn on the ringer */
		if (fastringer)
		{
			/* Speed up Ringer */
			reg20 =  0x7e6d;
			reg74 = 0x32;	/* Default */
			/* Beef up Ringing voltage to 89V */
			if (boostringer)
			{
				reg74 = 0x3f;
				reg21 = 0x0247;	/* RNGX */
				if (debug)
					printk(KERN_DEBUG "Boosting fast ringer on chan %d (89V peak)\n",card);
			}
			else if (lowpower)
			{
				reg21 = 0x014b;	/* RNGX */
				if (debug)
					printk(KERN_DEBUG "Reducing fast ring power on chan %d (50V peak)\n",card);
			}
			else if (fxshonormode && fxo_modes[_opermode].ring_x)
			{
				reg21 = fxo_modes[_opermode].ring_x;
				if (debug)
					printk(KERN_DEBUG "fxshonormode: fast ring_x power on chan %d\n",card);
			}
			else
			{
				reg21 = 0x01b9;
				if (debug)
					printk(KERN_DEBUG "Speeding up ringer on chan %d (25Hz)\n",card);
			}
			/* VBATH */
			axgcom_framer_out(wc, card, 74, reg74);
			/*RCO*/
			axgcom_proslic_setreg_indirect(wc, card, 20, reg20);
			/*RNGX*/
			axgcom_proslic_setreg_indirect(wc, card, 21, reg21);
		} 
        else
        {
			/* Ringer Speed */
			if (fxshonormode && fxo_modes[_opermode].ring_osc)
            {
				reg20 = fxo_modes[_opermode].ring_osc;
				if (debug)
					printk(KERN_DEBUG "fxshonormode: ring_osc speed on chan %d\n",card);
			}
			else
				reg20 = 0x7ef0;	/* Default */
			reg74 = 0x32;	/* Default */
			/* Beef up Ringing voltage to 89V */
			if (boostringer) 
            {
				reg74 = 0x3f;
				reg21 = 0x1d1;
				if (debug)
					printk(KERN_DEBUG "Boosting ringer on chan %d (89V peak)\n",card);
		    }
            else if (lowpower)
            {
				reg21 = 0x108;
				if (debug)
					printk(KERN_DEBUG "Reducing ring power on chan %d (50V peak)\n",card);
		    }
            else if (fxshonormode && fxo_modes[_opermode].ring_x)
		    {
				reg21 = fxo_modes[_opermode].ring_x;
				if (debug)
					printk(KERN_DEBUG "fxshonormode: ring_x power on chan %d\n",card);
			}
            else
			{
				reg21 = 0x160;
				if (debug)
					printk(KERN_DEBUG "Normal ring power on chan %d\n",card);
			}
			/* VBATH */
			axgcom_framer_out(wc, card, 74, reg74);
			/* RCO */
			axgcom_proslic_setreg_indirect(wc, card, 20, reg20);
 		    /* RNGX */
			axgcom_proslic_setreg_indirect(wc, card, 21, reg21);
		}
	}
	return 0;
 }

static int axgcom_hooksig(struct dahdi_chan *chan, enum dahdi_txsig txsig)
{
	struct axgcom_span *span = chan->pvt;
    struct axgcom *wc;
	int chan_entry = chan->chanpos - 1;
    if(span)
        wc = span->owner;
    else
       return 0;
    if(wc->span_type == 0)
      chan_entry = chan->chanpos - 1;
    else if(wc->span_type==2)
	   chan_entry = chan->chanpos -1 ; 
    else
	   return 0;
	if (wc->modtype[chan_entry] == MOD_TYPE_FXO) 
    {
		/* XXX Enable hooksig for FXO XXX */
		switch(txsig)
		{
				case DAHDI_TXSIG_START:
				case DAHDI_TXSIG_OFFHOOK:
					wc->mod[chan_entry].fxo.offhook = 1;
					axgcom_framer_out(wc, chan_entry, 5, 0x9);
					break;
				case DAHDI_TXSIG_ONHOOK:
					wc->mod[chan_entry].fxo.offhook = 0;
					axgcom_framer_out(wc, chan_entry, 5, 0x8);
					break;
				default:
					printk(KERN_NOTICE "wcfxo: Can't set tx state to %d\n", txsig);
					break;
     	}
    }
    else if(wc->modtype[chan_entry] == MOD_TYPE_GSM)
    {}
	else//FXS ��
    {
		struct fxs *const fxs = &wc->mod[chan_entry].fxs;
		switch(txsig)
		{
		    case DAHDI_TXSIG_ONHOOK:
		    	switch(chan->sig)
		    	{
			        case DAHDI_SIG_FXOKS:
			        case DAHDI_SIG_FXOLS:
				    /* Can't change Ring Generator during OHT */
			        		if (!fxs->ohttimer)
                            {
					           axgcom_set_ring_generator_mode(wc,
						       chan_entry, fxs->vmwi_hvac);
					           fxs->lasttxhook = fxs->vmwi_hvac ?SLIC_LF_RINGING :fxs->idletxhookstate;
				            }
			        		else
					           fxs->lasttxhook = fxs->idletxhookstate;
                			break;
			        case DAHDI_SIG_EM:
				            fxs->lasttxhook = fxs->idletxhookstate;
				            break;
			        case DAHDI_SIG_FXOGS:
				            fxs->lasttxhook = SLIC_LF_TIP_OPEN;
				            break;
			     }
			     break;
		    case DAHDI_TXSIG_OFFHOOK:
			     switch(chan->sig)
			     {
			          case DAHDI_SIG_EM:
			            	fxs->lasttxhook = SLIC_LF_ACTIVE_REV;
				            break;
			          default:
				            fxs->lasttxhook = fxs->idletxhookstate;
				            break;
			     }
			     break;
		    case DAHDI_TXSIG_START:
		          printk("    fsx%d: start ring\n",chan_entry);
		          axgcom_set_ring_generator_mode(wc, chan_entry, 0); /* Set ringer mode */
       			  fxs->lasttxhook = SLIC_LF_RINGING;
			      break;
		    case DAHDI_TXSIG_KEWL:
			      fxs->lasttxhook = SLIC_LF_OPEN;
			      break;
		    default:
			      printk(KERN_NOTICE "axgcom: Can't set tx state to %d\n", txsig);
		}
		if (debug) 
			printk(KERN_DEBUG  "Setting FXS hook state to %d (%02x)\n",txsig, fxs->lasttxhook);
		printk("^^^^ fxs%d set state0x%x\n",chan_entry,fxs->lasttxhook);
		axgcom_framer_out(wc, chan_entry, LINE_STATE, fxs->lasttxhook);
	}
	return 0;
 }

 static const struct dahdi_span_ops axgcom_span_ops =
 {
        .owner = THIS_MODULE,
        .hooksig = axgcom_hooksig,
        .open = axgcom_open,
        .close = axgcom_close,
        .ioctl = axgcom_ioctl,
        .watchdog = axgcom_watchdog,
        .shutdown= axgcom_shutdown,
        .startup= axgcom_startup,
        .chanconfig= axgcom_chanconfig,
        .spanconfig=axgcom_spanconfig,
        .hdlc_hard_xmit = axgcom_hdlc_hard_xmit,
        
 };

 static int axgcom_initialize(struct axgcom *wc)
 {
       int y =0;//spannum
       int z = 0;
       int channum = 0;
       struct dahdi_chan *mychans = NULL;
       if(wc->span_type == 0)
            wc->sumspan = 2;
       else if(wc->span_type == 1)
            wc->sumspan = 4;
       else
             wc->sumspan = 3;
       for( y=0; y<wc->sumspan; y++)//register span
       {
        memset(&wc->axspan[y].span,0,sizeof(struct dahdi_span));
        snprintf(wc->axspan[y].span.name, sizeof(wc->axspan[y].span.name)-1,"AXGCOM/%d/%d",wc->pos,y+1 );
	    snprintf(wc->axspan[y].span.desc, sizeof(wc->axspan[y].span.desc) - 1, "ATCOM GSM/WCDMA %d", wc->pos);
	    if (alawoverride)
	    {
		   printk(KERN_INFO "ALAW override parameter detected.  Device will be operating in ALAW\n");
		   wc->axspan[y].span.deflaw = DAHDI_LAW_ALAW;
	    }
	    else
  		    wc->axspan[y].span.deflaw = DAHDI_LAW_MULAW;
       if((wc->sumspan == 2))
       {
            sprintf(wc->axspan[y].span.name, "AXFXSO/%d/%d",wc->pos,y+1 );
            snprintf(wc->axspan[y].span.desc, sizeof(wc->axspan[y].span.desc) - 1, "ATCOM GSM/WCDMA %d", wc->pos);
            channum =  8;    //4
            wc->axspan[y].span.channels = channum;//4//2
 		    wc->axspan[y].span.flags = DAHDI_FLAG_RBS;
	   }
       else if ((wc->sumspan == 3) && (y==2)) //2//ax2g4a
       {
                sprintf(wc->axspan[y].span.name, "AXFXSO/%d/%d",wc->pos,y+1 );
                snprintf(wc->axspan[y].span.desc, sizeof(wc->axspan[y].span.desc) - 1, "ATCOM GSM/WCDMA %d", wc->pos);
                channum =  4;    //4
                wc->axspan[y].span.channels = channum;//4//2
   		       wc->axspan[y].span.flags = DAHDI_FLAG_RBS;
       }
       else//ax4g
       {
            if(wc->spanflag & (1<<y))
            {
                 sprintf(wc->axspan[y].span.name, "AXGSM/%d/%d",wc->pos,y+1 );
                 snprintf(wc->axspan[y].span.desc, sizeof(wc->axspan[y].span.desc) - 1, "ATCOM GSM/WCDMA %d", wc->pos);
            }
            else
            {
                 sprintf(wc->axspan[y].span.name, "UNAXGSM/%d/%d",wc->pos,y+1 );
                 snprintf(wc->axspan[y].span.desc, sizeof(wc->axspan[y].span.desc) - 1, "ATCOM GSM/WCDMA %d", wc->pos);
            }
            channum = 2;
            wc->axspan[y].span.channels = channum;
            wc->axspan[y].span.spantype = SPANTYPE_GSM;
            atomic_set(&wc->axspan[y].hdlc_pending, 0);
     }
     if (!(wc->chans[y] = kmalloc( channum*sizeof(*wc->chans[y]), GFP_KERNEL)))
     {
		   printk(KERN_ERR "axgcom: Not enough memory for span[%d] chans\n", y);
		   return -1;
     }
     for (z = 0; z < channum; z++)
     {
		if (!(wc->chans[y][z] = kmalloc(sizeof(*wc->chans[y][z]), GFP_KERNEL)))
        {
   		    printk(KERN_ERR "axgcom: Not enough memory for chans[%d][%d]\n",y, z);
	        return -1;
	    }
		memset(wc->chans[y][z], 0, sizeof(*wc->chans[y][z]));
        mychans = wc->chans[y][z];
        snprintf(mychans->name, sizeof(mychans->name)-1,"AXGCOM/%d/%d/%d", wc->pos, y+1,z+1);
        if((wc->span_type==0) && (y == 0))
              mychans->sigcap =  0;
        else
        {
              mychans->sigcap = DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
              mychans->sigcap |= DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
        }
        mychans->chanpos = z+1;
        mychans->pvt = &wc->axspan[y];
     }
     if((wc->span_type == 1) || ((wc->span_type==2) && (y<2)))
     {
         wc->chans[y][0]->sigcap = DAHDI_SIG_CLEAR;
         wc->chans[y][1]->sigcap = DAHDI_SIG_HARDHDLC;
         wc->sigchan[y] = wc->chans[y][1];
         memset(wc->rx_buf[y],0x0,sizeof(wc->rx_buf[y]));
         wc->chans[y][0]->readchunk = wc->rx_buf[y];
	     memset(wc->tx_buf[y],0x0,sizeof(wc->tx_buf[y]));
	     wc->chans[y][0]->writechunk = wc->tx_buf[y];
         memset(wc->sig_tx_buf[y],0x0,sizeof(wc->sig_tx_buf[y]));
         memset(wc->sig_rx_buf[y],0x0,sizeof(wc->sig_rx_buf[y]));
         memset(wc->tx_buf_linear[y],0x0,sizeof(wc->tx_buf_linear[y]));
         memset(wc->rx_buf_linear[y],0x0,sizeof(wc->rx_buf_linear[y]));
         wc->sig_rx_idex[y] = 0;
         wc->sig_tx_idex[y] = 0;
     }
     wc->axspan[y].owner= wc;
     wc->axspan[y].writechunk = (void *)(wc->writechunk + y * 16);//64 byte
     wc->axspan[y].readchunk = (void *)(wc->readchunk + y * 16);
     wc->axspan[y].span.ops    = &axgcom_span_ops;
     wc->axspan[y].span.chans = wc->chans[y];//
     wc->axspan[y].span.offset = y;
     wc->axspan[y].span.linecompat = DAHDI_CONFIG_CCS | DAHDI_CONFIG_AMI;
   }
   return 0;
 }

 static void axgcom_post_initialize(struct axgcom *wc)
 {
	int x;
	/* Finalize signalling  */
    int y;
    int chan;
    int z = 0;
	for (x = 0; x < wc->sumspan; x++) 
    {
      if(wc->spanflag & (1<<x))
      {
          if(wc->span_type==1)
          {
		    chan = 0;
		    continue;
          }
          else if( (wc->span_type == 2) && (x<2))
          {
             chan = 0;
             continue;
          }
          else if((wc->span_type == 2) &&  (x==2))
            chan = 4;
          else
            chan = 8;
          for(y =0; y<chan; y++)
          {
		    z=y;
	        if(wc->cardflag & (1 << (y) ))
            {
			    if (wc->modtype[y] == MOD_TYPE_FXO)
 				    wc->chans[x][z]->sigcap = DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
			    else if(wc->modtype[y] == MOD_TYPE_FXS)
				    wc->chans[x][z]->sigcap = DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
		    }
            else if (!(wc->chans[x][z]->sigcap & DAHDI_SIG_BROKEN))
			     wc->chans[x][z]->sigcap = 0;
		  }
       }
    }
 }



static int axgcom_hardware_init(struct axgcom *wc)
{
	/* Hardware stuff */
	unsigned char x =0;
    unsigned char y = 0;
    unsigned char gsm_num = 0;
    unsigned char analog_num = 0;
    int ledstat = 0;
    int timeslot;
    wc->dmactrl = 0x0;
	axgcom_pci_out(wc, WC_DMACTRL, wc->dmactrl);
    __axgcom_pci_out(wc, WC_SPI_STAT, 0);
    axgcom_pci_out(wc, WC_INTR, 0x00000000);
    axgcom_pci_out(wc, WC_MOD_RESET, 0x00000001);
	for (x = 0; x < 4; x++) //test 0-3 port
	{     //may be 0,2,4,6 is valued
		int sane=0,ret=0;
        y = 2*x ;
        if (!(ret = axgcom_init_GSM(wc, y, 0, 0, sane)))
        {
		  	wc->cardflag |= (1 << y);
            wc->spanflag |= (1 << x);
            wc->modtype[y] = MOD_TYPE_GSM;
            axgcom_framer_out(wc,y, AXGCOM_TDM_TIMESLOT, x*8);
		    printk("Module %d: Installed -- AUTO GSM(version 0x%x)\n", x, wc->GSM_BOARD_version[y]);
            timeslot = axgcom_framer_in(wc,y,AXGCOM_TDM_TIMESLOT);
            printk("read  car%d small board timeslot =%0x\n",y, timeslot );
        }
		else
		{
		    wc->modtype[y] = MOD_TYPE_UNDEF;
		    printk("Module %d: Not installed gsm\n", x);
        }
	}
	wait_just_a_bit(HZ/2);
    axgcom_pci_out(wc, WC_MOD_RESET, 0x00000000);
    wait_just_a_bit(HZ/2);
    axgcom_pci_out(wc, WC_MOD_RESET, 0x00000001);
    wait_just_a_bit(HZ/2);
    wait_just_a_bit(HZ/2);
    for (x = 0; x < wc->num_card; x++) //test 0-7
    {
       int sane=0,ret=0,readi=0;
       if(wc->modtype[x] == MOD_TYPE_GSM)
                 continue;
       wc->modtype[x] = MOD_TYPE_FXS;
				/* Init with Auto Calibration */
 	   if (!(ret=axgcom_init_proslic(wc, x, 0, 0, sane)))
       {
		     wc->cardflag |= (1 << x);
             wc->modtype[x] = MOD_TYPE_FXS;
             if (debug)
             {
               		readi = axgcom_framer_in(wc,x,LOOP_I_LIMIT);
               		printk(KERN_DEBUG "Proslic module %d loop current is %dmA\n",x,((readi*3)+20));
             }
			 printk(KERN_INFO "Module %d: Installed -- AUTO FXS/DPO\n",x);
	  }
      else
      {
         wc->modtype[x] = MOD_TYPE_FXO;
         if(ret!=-2)
         {
			sane=1;
            wc->modtype[x] = MOD_TYPE_FXS;
		    /* Init with Manual Calibration */
			if (!axgcom_init_proslic(wc, x, 0, 1, sane))
            {
			      wc->cardflag |= (1 << x);
                  wc->modtype[x] = MOD_TYPE_FXS;
                  if (debug)
                  {
               		    readi = axgcom_framer_in(wc,x,LOOP_I_LIMIT);
             		    printk(KERN_DEBUG "Proslic module %d loop current is %dmA\n",x,((readi*3)+20));
       		      }
			      printk(KERN_INFO "Module %d: Installed -- MANUAL FXS\n",x);
			}
            else
				printk(KERN_NOTICE "Module %d: FAILED FXS (%s)\n", x, fxshonormode ? fxo_modes[_opermode].name : "FCC");
	     }
         else if (!(ret = axgcom_init_voicedaa(wc, x, 0, 0, sane)))//fxo
		 {
		   wc->cardflag |= (1 << x);
           wc->modtype[x] = MOD_TYPE_FXO;
		   printk(KERN_INFO "Module %d: Installed -- AUTO FXO (%s mode)\n",x, fxo_modes[_opermode].name);
	     }
		 else
			printk(KERN_NOTICE "Module %d: Not installed\n", x);
	   }
	   printk("card flag%d = 0x%x\n",x,wc->cardflag);
    }
	for (x = 0;x < wc->num_card;x++)
	{//gsm 0,2,4,6 analog 1,3,5,7
    	if(wc->cardflag & (1 << x))
		{
		    printk("card%d modtype = %d\n",x,wc->modtype[x]);//jwj change 2012 5-23
		    if(wc->modtype[x] == MOD_TYPE_GSM)
            {
                axgcom_framer_out(wc,x, AXGCOM_TDM_TIMESLOT, x*4);
                axgcom_framer_in(wc,x, AXGCOM_TDM_TIMESLOT);
                gsm_num++;
		    }
            else
            {
			    ledstat |= (1<<x);
                analog_num++;
            }
		}
	}
    axgcom_set_led( wc, ledstat);
    if((gsm_num==0 )&& (analog_num >0))
    {
        wc->span_type = 0;
        wc->offset_span=0;
        wc->gsm_port_num = 0;
        wc->spanflag |= 2;
        wc->analog_chans = 8;
        wc->realspan = 1;
    }
    else if((analog_num==0) && (gsm_num >0))
    {
        wc->span_type = 1;//ax4g
        wc->offset_span=0;
        wc->gsm_port_num = 4;
    }
    else if((analog_num > 0) && (gsm_num >0)&&((analog_num <=4)&&(gsm_num<=2)))
    {
        wc->span_type = 2;
        wc->offset_span=4;
        wc->gsm_port_num = 2;
        wc->spanflag = 0;
        if(wc->modtype[4] == MOD_TYPE_GSM )
           wc->spanflag |= 1<<0;
        if(wc->modtype[6] == MOD_TYPE_GSM )
           wc->spanflag |= 1<<1;
        wc->spanflag |= 1<<2;
        wc->analog_chans = 4;
        wc->realspan = 2;
    }
    else
	  wc->spanflag = 0;
    for (x = 0;x < wc->num_card;x++)
    {
        switch(wc->span_type)
        {
            case 0://800pe
                if(wc->modtype[x] == MOD_TYPE_FXS)
                {
                    axgcom_framer_out(wc, x, 2, 64+x*8+1);//is card,card first recv first
                    axgcom_framer_out(wc, x, 3, 0x00);//is card,card first recv first
                    axgcom_framer_out(wc, x, 4, 64+x*8+1);//bit count
                    axgcom_framer_out(wc, x, 5, 0x00);//is card,card first recv first
                }
                else if(wc->modtype[x] == MOD_TYPE_FXO)
                {
                    axgcom_framer_out(wc, x, 34, 64+x*8+1);
                    axgcom_framer_out(wc, x, 35, 0x00);
                    axgcom_framer_out(wc, x, 36, 64+x*8+1);
                    axgcom_framer_out(wc, x, 37, 0x00);
                }
                break;
            case 2://ax2g4a
                if(wc->modtype[x] == MOD_TYPE_FXS)
                {
                    axgcom_framer_out(wc, x, 2, x*8+64*2+1);//is card,card first recv first
                    axgcom_framer_out(wc, x, 3, 0x0);//is card,card first recv first
                    axgcom_framer_out(wc, x, 4, x*8+64*2+1);//bit count
                    axgcom_framer_out(wc, x, 5, 0x0);//bit count
                }
                else if(wc->modtype[x] == MOD_TYPE_FXO)
                {
                    axgcom_framer_out(wc, x, 34, x*8+64*2+1);
                    axgcom_framer_out(wc, x, 35,  0x0);
                    axgcom_framer_out(wc, x, 36, x*8+64*2+1);
                    axgcom_framer_out(wc, x, 37, 0x0);
                }
                else if(wc->modtype[x] == MOD_TYPE_GSM)
                {
	 	            if(x==4)
		   	            axgcom_framer_out(wc,x, AXGCOM_TDM_TIMESLOT, 0);
	                else
		   	            axgcom_framer_out(wc,x, AXGCOM_TDM_TIMESLOT, 8);
                }
            default:
                break;
        }
    }
	/* Return error if nothing initialized okay. */
	if (!wc->cardflag && !timingonly)
		return -1;
	return 0;
 }

 static void axgcom_start_dma(struct axgcom *wc)
 {
    axgcom_pci_out(wc, WC_RDADDR, wc->readdma);
    axgcom_pci_out(wc, WC_WRADDR, wc->writedma);
    wc->dmactrl =  0xc0000000;
    axgcom_pci_out(wc, WC_DMACTRL, wc->dmactrl);
    printk("axgcom dma start\n");
 }

 static void axgcom_stop_dma(struct axgcom *wc)
 {
    wc->dmactrl =  0x0;
    axgcom_pci_out(wc, WC_RDADDR, 0x00);
    axgcom_pci_out(wc, WC_WRADDR, 0x00);
    axgcom_pci_out(wc, WC_DMACTRL, wc->dmactrl);
    axgcom_pci_out(wc, WC_MOD_RESET, 0x00000001);
    axgcom_set_led( wc,  0x00);
    __axgcom_pci_in(wc, WC_SPI_STAT);
    __axgcom_pci_out(wc, WC_SPI_STAT, 0);
    printk("axgcom dma stop\n");
 }

 static void axgcom_power_on_all(struct axgcom *wc, int delay, int flag)
 {
	long ms;
    int i = 0;
    int offset = 0;
    AXGCOM_SLEEP_MILLI_SEC(1000, ms);
    for(i=0; i<4; i++)
    {
       offset = 2*i;
       if((wc->cardflag & (1<<offset) )  && (wc->modtype[offset] == MOD_TYPE_GSM))
       {
           if((flag==0) || ((flag ==1) &&(wc->check_start[i] == 1)))
           {
                printk("jwj axgcom power on all mod i=%d flag=%d\n",i,flag);
                axgcom_framer_out(wc, offset, AXGCOM_POWER_DATA_GSM, 0x00 );
                axgcom_framer_out(wc, offset, AXGCOM_POWER_CTRL_GSM,  MOD_CTRL_PWR);
                AXGCOM_SLEEP_MILLI_SEC(delay, ms);
                axgcom_framer_out(wc, offset, AXGCOM_POWER_DATA_GSM, 0x00);
                axgcom_framer_out(wc, offset, AXGCOM_POWER_CTRL_GSM, MOD_CTRL_DTR);
                axgcom_framer_out(wc, offset, AXGCOM_POWER_DATA_GSM, MOD_CTRL_PWR);
                axgcom_framer_out(wc, offset, AXGCOM_POWER_CTRL_GSM, MOD_CTRL_PWR);
            }
          }
          if(flag == 0)
            wc->check_start[i] = 1; 
      }
      AXGCOM_SLEEP_MILLI_SEC(4000, ms); 
      printk("jwj axgcom power on all mod up now\n");
 }

 static int axgcom_poweron_thread(void *data)
 {
	struct axgcom *wc = (struct axgcom *)data;
	axgcom_power_on_all(wc, 2800,0);
	do_exit(0); 
	return 0;
 }

 static int axgcom_poweron_thread_reset(void *data)
 {
    int i =0;
	struct axgcom *wc = (struct axgcom*)data;
 	wait_event_interruptible(Queue_poweron_reset, wc->queue_power_reset_flag!= 0);
	wc->queue_power_reset_flag  = 0;
	axgcom_power_on_all(wc, 2800,1);
	wc->power_on_flag = 1;
    for(i=0; i<wc->gsm_port_num; i++)
	{
	    wc->poweron[i] = 1;
        wc->check_start[i] = 0;
    }
    wc->queue_power_on_flag = 1;
    if(debug)
       printk("reset and write run flag\n");
    axgcom_pci_out(wc, WC_MOD_START_FLAG,0x01);
	wake_up_interruptible(&Queue_poweron);
	do_exit(0); 
	return 0;
 }

static int axgcom_power_on_modules(struct axgcom *wc)
{
    int i=0;
    int err;
    if((wc->poweron[0]==0)||(wc->poweron[1]==0)||(wc->poweron[2]==0)||(wc->poweron[3]==0))
    {
        if(wc->already_run || burn)//read from small reg 
        {//awake module,restart dahdi
            for(i = 0; i<wc->gsm_port_num; i++)
            {
                wc->poweron[i] = 1;
                wc->check_start[i] = 1 ;
                wc->power_on_flag = 0;         
            }
         }
         else
         {
           //set reset reg poweron all modules
           //this is early
              wc->already_run = 1;
           //set reg already run
           task_poweron = kthread_create(axgcom_poweron_thread, (void*)wc, "axgcom_poweron_task");
	       if(IS_ERR(task_poweron))
	       {
		      err = PTR_ERR(task_poweron);
		      task_poweron = NULL;
		      return err;
	       }
	       wake_up_process(task_poweron);
        }
        task_poweron_reset = kthread_create(axgcom_poweron_thread_reset, (void*)wc, "axgcom_poweron_task_reset");
	    if(IS_ERR(task_poweron_reset))
	    {
		   err = PTR_ERR(task_poweron_reset);
		   task_poweron_reset = NULL;
		   return err;
	    }
	    wake_up_process(task_poweron_reset);
     }
    return 0;
 }

 static void axgcom_free(struct axgcom *wc)
 {
    int x = 0;
    int y = 0;
    int numchans = 0;
    for (x = 0; x < wc->sumspan; x++) 
    {        
       if(wc->span_type == 2)
       {
          if(x<2)
             numchans = 2;
          else
             numchans = 4;
       }
       else if(wc->span_type == 1)
           numchans = 2;
       else
           numchans = 8;
       for (y = 0; y <numchans;  y++)
       {
	      if (wc->chans[x][y])
       		    kfree(wc->chans[x][y]);
	   }
	   if (wc->chans[x])
			kfree(wc->chans[x]);
    }
 }

 static void axgcom_unregister(struct axgcom *wc)
 {
          dahdi_unregister_device(wc->ddev);
 }

 static int __devinit axgcom_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
 {
	int res;
	struct axgcom *wc;
 	int x;
    int y;
	unsigned int hw_version;
    unsigned int fireware_version;
    struct axgcom_desc *d = (struct axgcom_desc *)ent->driver_data;
    printk(KERN_INFO "AX4G/AX2G4A: init one start\n");
	for (x=0;x<WC_MAX_IFACES;x++)
		if (!ifaces[x])
			break;
	if (x >= WC_MAX_IFACES)
	{
		printk(KERN_NOTICE "Too many interfaces\n");
		return -EIO;
	}
    if (pci_enable_device(pdev))
		return  -EIO;
    wc = kmalloc(sizeof(struct axgcom), GFP_KERNEL);
    if(!wc)
        return -ENOMEM;
    memset(wc, 0, sizeof(struct axgcom));
    printk(KERN_INFO "AX4G/AX2G4A: dahdi_create_device start\n");
    wc->ddev = dahdi_create_device();
    if (!wc->ddev)
    {
       kfree(wc);
	   return -ENOMEM;
	}
    wc->devtype = (const struct devtype *)(ent->driver_data);
   	if (wc)
   	{
		int cardcount = 0;
		ifaces[x] = wc;
		spin_lock_init(&wc->lock);
		wc->curcard = -1;
        wc->memaddr = pci_resource_start(pdev, 0);
        wc->memlen = pci_resource_len(pdev, 0);
        wc->membase = ioremap(wc->memaddr, wc->memlen);//4
 	    wc->pos = x;
        if (pci_request_regions(pdev, wc->devtype->desc))
           printk(KERN_INFO "AX4G/AX2G4A: Unable to request regions\n");
        printk(KERN_INFO "Found AX4G/AX2G4A at base address %08lx, remapped to %p\n",  wc->memaddr, wc->membase);
        wc->dev = pdev;
        for(y = 0; y<1; y++)
        {
      	    hw_version =axgcom_pci_in(wc, WC_VERSION);
	        fireware_version = axgcom_pci_in(wc, WC_FPGA_FIREWARE_VER);
		    wc->HW_VER = hw_version;
            wc->FPGA_FIREWARE_VER = fireware_version;
            printk("@@Hardware_Version = 0x%x fpga firewire version=0x%x y=%d\n",wc->HW_VER,wc->FPGA_FIREWARE_VER,y); // test by ly
       }
	   if( (wc->HW_VER == 0xa1c000e8) ||   (wc->HW_VER == 0xa1c100e8) )	//axe4g old version
	   {
			wc->num_card = 8;
            wc->sumspan = 4;//every gsm card is one span
            wc->numchans = 8; //jwj will change sync chan+dchans+bchans
            wc->analog_chans = 0;//one gsm span 64 byte 8 chans dma is 32bit first 8byte is span1
            wc->gsm_port_num = 4;//send 8*8*4,send seq is last
            mallocsize = DAHDI_MAX_CHUNKSIZE *(32);//32chans
            wc->writechunk = pci_alloc_consistent(pdev, mallocsize*2, &wc->writedma); //512bytes   DAHDI_MAX_CHUNKSIZE * 2 * 2 * 4
            if (!wc->writechunk)
            {
				printk(KERN_NOTICE "AX4G/AX2G4A: Unable to allocate DMA-able memory\n");
                pci_iounmap(wc->dev, (void*)wc->membase);
                pci_release_regions(wc->dev);
                kfree(wc);
				return -ENOMEM;
		    }
            wc->readchunk = wc->writechunk + mallocsize/4;	/* in doublewords */
		    wc->readdma = wc->writedma + mallocsize;		/* in bytes */
		}
        else
            return -ENOMEM;
        for (y=0;y<wc->num_card;y++)
		  wc->flags[y] = wc->devtype->flags;
       	/* Enable bus mastering */
		pci_set_master(pdev);
		/* Keep track of which device we are */
		pci_set_drvdata(pdev, wc);
        memset((void *)wc->writechunk,0x0,mallocsize);//4//4+4+2+4
        memset((void *)wc->readchunk,0x0,mallocsize);//4//4+4+2+4
        axgcom_hardware_init(wc);
        if( (wc->spanflag == 0 ) || (!wc->cardflag))
		{
		   pci_free_consistent(pdev,  mallocsize*2, (void *)wc->writechunk, wc->writedma);  //in byte
           pci_iounmap(wc->dev, (void*)wc->membase);
           pci_set_drvdata(pdev, NULL);
           pci_release_regions(wc->dev);
	       return -EIO;
        }
        if(axgcom_initialize(wc))
        {
		  printk(KERN_NOTICE "axgcom: Unable to intialize span\n");
          pci_free_consistent(pdev,  mallocsize*2, (void *)wc->writechunk, wc->writedma);  //in byte
          pci_iounmap(wc->dev, (void*)wc->membase);
          pci_set_drvdata(pdev, NULL);
          pci_release_regions(wc->dev);
          axgcom_free(wc);  //free chans[][]
		  kfree(wc);
		  return -EIO;
		}
        axgcom_env_init(wc);
        if (request_irq(pdev->irq, axgcom_interrupt, IRQF_SHARED, "axe4gn", wc))
        {
		 printk(KERN_NOTICE "axe2g/axe2g4a: Unable to request IRQ %d\n", pdev->irq);
         pci_free_consistent(pdev,  mallocsize*2, (void *)wc->writechunk, wc->writedma);  //in byte
         pci_iounmap(wc->dev, (void*)wc->membase);
         pci_set_drvdata(pdev, NULL);
         axgcom_free(wc);
	 	 kfree(wc);
	 	 return -EIO;
		}
		axgcom_post_initialize(wc);
		for (x = 0; x < wc->num_card; x++)
		{
			if (wc->cardflag & (1 << x))
				cardcount++;
		}
        printk(KERN_INFO "Found a  GSM Card: ATCOM AXE4GN (%d modules)\n",cardcount);
		res = 0;
        wc->ddev->manufacturer = "ATCOM";
        wc->ddev->devicetype = wc->variety;
        wc->ddev->location = kasprintf(GFP_KERNEL, "PCI Bus %02d Slot %02d",wc->dev->bus->number,PCI_SLOT(wc->dev->devfn) + 1);
        if (!wc->ddev->location)
        {
			pci_free_consistent(pdev,  mallocsize*2, (void *)wc->writechunk, wc->writedma);  //in byte
		    kfree(wc);
		    dahdi_free_device(wc->ddev);
		    pci_disable_device(pdev);
		    return -ENOMEM;
	   }
       for (x = 0; x < wc->sumspan; x++)
       {
           if((wc->span_type == 0) && (x==0))
             continue;
           else
             list_add_tail(&wc->axspan[x].span.device_node,&wc->ddev->spans);
	    }
	    if (dahdi_register_device(wc->ddev, &wc->dev->dev))
	    {
		    dev_err(&wc->dev->dev, "Unable to register device.\n");
		    pci_free_consistent(pdev,  mallocsize*2, (void *)wc->writechunk, wc->writedma);  //in byte
		    kfree(wc->ddev->location);
            kfree(wc);
		    dahdi_free_device(wc->ddev);
		    pci_disable_device(pdev);
		    return -1;
	   }
       wc->already_run = axgcom_pci_in(wc, WC_MOD_START_FLAG);
       axgcom_power_on_modules(wc);
       axgcom_init_addactrol(wc);
       axgcom_start_dma(wc);
 	}
	else
		res = -ENOMEM;
	return res;
 }

 static void axgcom_release(struct axgcom *wc)
 {
    axgcom_free(wc);
	kfree(wc);
	printk(KERN_INFO "Freed a Wildcard\n");
 }

 static void __devexit axgcom_remove_one(struct pci_dev *pdev)
 {
    long ms;
    unsigned long origjiffies = jiffies;
	struct axgcom *wc = pci_get_drvdata(pdev);
	if (wc)
	{
       printk("jwj axgcom remove one\n");
       axgcom_stop_dma(wc);
		/* Stop any DMA */
       while(1)
       {
          if(wc->queue_power_on_flag == 1)
		      break;
          if (jiffies_to_msecs(jiffies - origjiffies) >= 12000 )
          {
              wc->queue_power_on_flag = 1;
		      break;
	      }
		  AXGCOM_SLEEP_MILLI_SEC(100, ms);
        }
        wait_event_interruptible(Queue_poweron, wc->queue_power_on_flag!= 0);
        wc->queue_power_on_flag = 0;//sure poweron and poweroff
        free_irq(pdev->irq, wc);
        axgcom_unregister(wc);
        pci_free_consistent(pdev,  mallocsize*2, (void *)wc->writechunk, wc->writedma);  //in byte
        if (wc->membase)
	        iounmap((void *)wc->membase);
        pci_release_regions(pdev);
    	/* Reset PCI chip and registers */
        pci_set_drvdata(pdev, NULL);
		/* Release span, possibly delayed */
        axgcom_release(wc);
	}
 }

 static struct pci_device_id axgcom_pci_tbl[] =
 {
	{ 0xd161, 0xb200, PCI_ANY_ID,PCI_ANY_ID, 0, 0, (unsigned long) &axgcomi },
	{ 0 }
 };

 MODULE_DEVICE_TABLE(pci, axgcom_pci_tbl);

 static struct pci_driver axgcom_driver =
 {
	.name = "axgcom",
	.probe = axgcom_init_one,
	.remove =__devexit_p(axgcom_remove_one),
	.suspend = NULL,
	.resume = NULL,
	.id_table = axgcom_pci_tbl,
 };

 static int __init axgcom_init(void)
 {
	int res;
	int x;
	for (x = 0; x < (sizeof(fxo_modes) / sizeof(fxo_modes[0])); x++)
	{
		if (!strcmp(fxo_modes[x].name, opermode))
			break;
	}
	if (x < sizeof(fxo_modes) / sizeof(fxo_modes[0]))
		_opermode = x;
	else
	{
		printk(KERN_NOTICE "Invalid/unknown operating mode '%s' specified.  Please choose one of:\n", opermode);
		for (x = 0; x < sizeof(fxo_modes) / sizeof(fxo_modes[0]); x++)
			printk(KERN_INFO "  %s\n", fxo_modes[x].name);
		printk(KERN_INFO "Note this option is CASE SENSITIVE!\n");
		return -ENODEV;
	}
	if (!strcmp(opermode, "AUSTRALIA"))
	{
		boostringer = 1;
		fxshonormode = 1;
	}
	/* for the voicedaa_check_hook defaults, if the user has not overridden
	   them by specifying them as module parameters, then get the values
	   from the selected operating mode
	*/
	if (battdebounce == 0)
		battdebounce = fxo_modes[_opermode].battdebounce;
	if (battalarm == 0)
		battalarm = fxo_modes[_opermode].battalarm;
	if (battthresh == 0)
		battthresh = fxo_modes[_opermode].battthresh;
	res = dahdi_pci_module(&axgcom_driver);
	if (res)
		return -ENODEV;
	return 0;
 }

 static void __exit axgcom_cleanup(void)
 {
	pci_unregister_driver(&axgcom_driver);
       printk("Unregistered axgcom\n");
 }

 module_param(pcm_mode, int, 0600);
 module_param(burn, int, 0600);
 module_param(debug, int, 0600);
 module_param(fxovoltage, int, 0600);
 module_param(loopcurrent, int, 0600);
 module_param(reversepolarity, int, 0600);
 module_param(robust, int, 0600);
 module_param(opermode, charp, 0600);
 module_param(timingonly, int, 0600);
 module_param(lowpower, int, 0600);
 module_param(boostringer, int, 0600);
 module_param(fastringer, int, 0600);
 module_param(fxshonormode, int, 0600);
 module_param(battdebounce, uint, 0600);
 module_param(battalarm, uint, 0600);
 module_param(battthresh, uint, 0600);
 module_param(ringdebounce, int, 0600);
 module_param(fwringdetect, int, 0600);
 module_param(alawoverride, int, 0600);
 module_param(fastpickup, int, 0600);
 module_param(fxotxgain, int, 0600);
 module_param(fxorxgain, int, 0600);
 module_param(fxstxgain, int, 0600);
 module_param(fxsrxgain, int, 0600);
 module_param(dtmf, int, 0600);

 MODULE_DESCRIPTION("ATCOM GSM AX2G4A Driver");
 MODULE_ALIAS("ATCOMGXXP");
 MODULE_LICENSE("GPL v2");

 module_init(axgcom_init);
 module_exit(axgcom_cleanup);
