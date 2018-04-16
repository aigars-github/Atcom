#include <stdlib.h>
#include <stdio.h>
#include <iconv.h>
#include "gsm_internal.h"
#include "libgsmcom.h"
#include "gsm_sms.h"

static int  gsmDecode7bit(const unsigned char* pSrc, char* pDst, int nSrcLength);
static int sms_pdu_7bit_package(char *data,unsigned char *udata,int send_len, int  calledlen, char *conv_called);
static int gsmBytes2String(const unsigned char* pSrc, char* pDst, int nSrcLen);
static  int sms_pdu_invert_num(char *src_num, char *dest_num, unsigned char *num_len);
static unsigned long gsm_hex_to_dec(char *src_hex, int len);
static int unicode_2_gb18030(char *inbuf,size_t inlen,char *outbuf,size_t outlen);

static void gsm_reverse_string(char *dest, char *src);

 int octet_to_septet(int octet)
 {
        return ((octet * 8) / 7) + (((octet * 8) % 7) ? 1 : 0);
 }

 int septet_to_octet(int septet)
 {
        return (septet * 7) / 8 + (((septet * 7) % 8) ? 1 : 0);
 }

 static unsigned long gsm_hex_to_dec(char *src_hex, int len)
 {
    int i = 0;
    int j  =0;
    unsigned long res = 0;
    for (i=0; i<len; i++)
    {
	 j = 0;
	 if ((src_hex[i] >= 48) && (src_hex[i] <=57))  /* '0' - '9' */
	    j = src_hex[i] - 48;
	 else if ((src_hex[i] >= 65) && (src_hex[i] <=70))  /* 'A' - 'F' */
		j = src_hex[i] - 55;
	 else if ((src_hex[i] >= 97) && (src_hex[i] <=102))  /* 'a' - 'f' */
		j = src_hex[i] - 87;
 	 res += j * (unsigned long)pow(16, len - i - 1);
    }
   return res;
 }

 char *pdu_get_smsc_len(char *src, char *len)
 {
	char *p_start = NULL;
    char *p_end = NULL;
	p_start = src;
	p_end = p_start + 2;
	*len = (char)gsm_hex_to_dec(p_start, 2);
    return p_end;
 }

 char *pdu_get_first_oct(char *src, unsigned char *deliver)
 {
	char *p_start = NULL;
       char *p_end = NULL;
	p_start = src;
	p_end = p_start + 2;
	*deliver= (char)gsm_hex_to_dec(p_start, 2);
	return p_end;	
 }

 char *pdu_get_oa_len(char *src, char *len)
 {
	char *p_start = NULL;
    char *p_end = NULL;
	p_start = src;
	p_end = p_start + 2;
	*len = (char)gsm_hex_to_dec(p_start, 2);
	return p_end;	
 }

 char *pdu_get_pid(char *src, char *pid)
 {
	char *p_start = NULL;
    char  *p_end = NULL;
	p_start = src;
	p_end = p_start + 2;
	*pid = (char)gsm_hex_to_dec(p_start, 2);
	return p_end;	
 }

 char *pdu_decode_ud_dcs(char *src,  unsigned char *dcs)
 {
	char *p_start = NULL;
    char *p_end = NULL;
	unsigned char dcs_group = 0;
	p_start = src;
	p_end = p_start + 2;//0x08
 	dcs_group = gsm_hex_to_dec(p_start, 1);
    *dcs = gsm_hex_to_dec(p_start+1, 1);
    return p_end;
 }

 char *pdu_get_timestamp(char *src, struct gsm_sms_pdu_timestamp *tstamp)
 {
	char *p_start = NULL;
    char *p_end = NULL;
	int len = 14;
	int i = 0;
 	p_start = src;
	p_end = p_start + len;
    for(i = 0; i< len; i+=2)
    {
      char tmp = p_start[i] - 0x30;
      char tmp1 = p_start[i+1] -0x30 ;
      char val = tmp1*10 + tmp;
      switch(i)
      {
          case 0:
             tstamp->year = val;
             break;
          case 2:
             tstamp->month= val;
             break;
          case 4:
             tstamp->day = val;
             break;
          case 6:
             tstamp->hour= val;
             break;
          case 8:
             tstamp->minute= val;
             break;
          case 10:
             tstamp->second= val;
             break;
         case 12:
             tstamp->timezone= val;
             break;
     }
   }
   return p_end;
 }

 char *pdu_get_ud_len(char *src, unsigned char  *len)
 {
	char *p_start = NULL;
    char *p_end =NULL;
	p_start = src;
	p_end = p_start + 2;
	*len = (unsigned char)gsm_hex_to_dec(p_start, 2);
	return p_end;	
 }

 char *pdu_decode_udh(char *src, unsigned char *udhl, char *seq)
 {
   char *p_start = NULL;
   char *p_end = NULL;
   p_start = src;
   *udhl = (unsigned char)gsm_hex_to_dec(p_start, 2);
   *udhl +=1;
   p_end = p_start + 2;
   p_end = p_start + 4;
   p_end = p_start + 6;
   p_end = p_start + 8;
   p_end = p_start + 10;
   *seq = (unsigned char)gsm_hex_to_dec(p_end, 2);
   p_end = p_start + 12;
   return p_end;
 }

 void gsm_string2byte(char *inbuf, int len, unsigned char *outbuf)
 {
	int i = 0;
	for (i = 0; i < len ; i++)
	  outbuf[i] = (unsigned char)gsm_hex_to_dec(&inbuf[i*2], 2);
	outbuf[2*len] = '\0';
 }

 static void gsm_reverse_string(char *dest, char *src)
 {
	int i;
	int len = strlen(src);
    for(i=0; i < len; i+=2)
    {
		dest[i] = src[i+1];
		if ((src[i] != 'F') && (src[i] != 'f'))
			dest[i+1] = src[i];
		else
		{
			dest[i+1] = '\0';
			return;
		} 
	}
	if (i >= len)
		dest[i] = '\0';
 }

 void gsm_reverse_num(char *src, char *dest, char  type)
 {
	if (type == 1)
	{
	   dest[0] = '+';
	   gsm_reverse_string(dest+1,src);
	}
	else
		gsm_reverse_string(dest,src);
 }


 char *pdu_get_addr_num(char *src, int len, char *dest, int flag)
 {
	char *p_start = NULL;
    char *p_end = NULL;
	int num_len = 0;
    if(!flag)
        num_len = (len - 1) * 2;
    else
    {
      if(len%2)
          num_len = len+1;
      else
         num_len = len;
    }
	p_start = src;
	p_end = p_start + num_len;
	strncpy(dest, p_start, num_len);
	dest[num_len] = '\0';
	return p_end;
 }

 char *pdu_get_addr_type(char *src, char *type, char *plan)
 {
	char *p_start = NULL;
    char *p_end = NULL;
    if(!src)
       return NULL;
    if( !type || !plan)
       return src;
	p_start = src;
	p_end = p_start + 1;
    p_end = p_start + 2;
	return p_end;
 }

 int gsmBytes2String(const unsigned char* pSrc, char* pDst, int nSrcLen)
 {
	const char tab[]="0123456789ABCDEF";
	int i =0;
	for(i=0; i<nSrcLen; i++)
	{
		*pDst++ = tab[*pSrc >> 4];
		*pDst++ = tab[*pSrc & 0x0f];
		pSrc++;
	}
	*pDst = '\0';
	return nSrcLen * 2;
 }

 static int sms_pdu_7bit_package(char *data,unsigned char *udata,int send_len, int  calledlen, char *conv_called)
 {
    return 0;
 }

 static int  gsmDecode7bit(const unsigned char* pSrc, char* pDst, int nSrcLength)
 {
  int   nSrc = 0;
  int   nDst = 0;
  int   nByte = 0;
  unsigned char nLeft = 0;
  while(nSrc < nSrcLength)
  {
    *pDst = ((*pSrc << nByte)| nLeft) & 0x7f;
    nLeft = *pSrc >> (7-nByte);
    pDst++;
    nDst++;
    nByte++;
    if(nByte == 7)
    {
       *pDst = nLeft;
       pDst++;
       nDst++;
       nByte = 0;
       nLeft = 0;
    }
    pSrc++;
    nSrc++;
  }
  *pDst   =   '\0';   
  return   nDst;
 }

 void gsm_to8Bit(unsigned char *in, unsigned char *out, int len, int flag)
 {
	int i = 0;
	int inputOffset = 0;
	int outputOffset = 0;
    if(flag)
    {
      out[outputOffset] = in[inputOffset] >>1;
      inputOffset++;
      outputOffset++;
    }
    while (inputOffset < len )
    {
       out[outputOffset] = (in[inputOffset] & (unsigned char)(pow(2, 7-i)-1));
       if (i == 8)
       {
          out[outputOffset] = in[inputOffset-1] & 127;
	      i = 1;
	   }
       else
       {
	      out[outputOffset] = out[outputOffset] << i;
		  out[outputOffset] |= in[inputOffset-1] >> (8-i);
		  inputOffset++;
		  i++;
	   }
	   outputOffset++;
	}
	out[len] = '\0';
 }


 int gsmEncodeUCS2( unsigned char* pSrc, unsigned char** pDst, size_t  nSrcLength, int *out_len, int offset)
 {
    unsigned char *pLen = *pDst;
    size_t  outlen = 1024;
    iconv_t cd;
    unsigned char **pin = &pSrc;
    unsigned char *pout = NULL;
    pout = *pDst+offset+1;
    if(!pSrc || !pLen)
        return 1;
    cd = iconv_open( "UCS-2BE","UTF-8");
    if (cd==0)
    {
        gsm_log(GSM_LOG_WARNING, "gsmEncodeUCS2 iconv open fail\n");
        return 1;
    }
    if (iconv(cd, (char**)pin, &nSrcLength, (char**)&pout, &outlen)==-1) 
    {
        gsm_log(GSM_LOG_WARNING, "gsmEncodeUCS2 iconv fail errno=%d\n",errno);
        iconv_close(cd);
	    return 1;
    }
    *pLen = 1024 - outlen + offset;
    *out_len += *pLen + 1;
    *pout = '\0';
    *pDst = pout;
    iconv_close(cd);
    gsm_log(GSM_LOG_WARNING, "gsmEncodeUCS2 iconv ok and outlen=%d  plen=%d offset=%d\n",outlen,*pLen,offset);
    return 0;
 }

 int  gsmEncode7bit(unsigned  char* pSrc, unsigned char** pDst, int nSrcLength, int offset)
 {
  int   nSrc = 0;
  int   nDst = 0;
  int   nChar = 0;
  unsigned char nLeft = 0;
  unsigned char *pbuf = *pDst+offset-1;
  if(!pSrc || !pDst)
       return 1;
  nSrc += offset;
  nSrcLength +=offset;
  while(nSrc < nSrcLength)
  {
     nChar = nSrc & 7;//0-7
     if(nChar == 0)
       nLeft   =   *pSrc;
     else
     {
       *pbuf = (*pSrc << (8-nChar))|nLeft;
       nLeft = *pSrc >> nChar;
       pbuf++;
       nDst++;
     }
     pSrc++;
     nSrc++;
  }
  *pDst = pbuf;
  return 0;
 }

 int code_convert(char *from_charset,char *to_charset, char *inbuf,size_t  inlen,char *outbuf,size_t outlen)
 {
	iconv_t cd;
	char **pin = &inbuf;
	char **pout = &outbuf;
	cd = iconv_open(to_charset,from_charset);
	if (cd==0)
	{
       gsm_log(GSM_LOG_WARNING,"iconv_open error return\n");
	   return -1;
	}
    gsm_log(GSM_LOG_WARNING,"iconv_open start inlen=%d outlen=%d\n",inlen,outlen);
	memset(outbuf,0,outlen);
	if (iconv(cd,pin,&inlen,pout,&outlen)==-1)
	{
      gsm_log(GSM_LOG_WARNING,"iconv error and return errno=%d outlen=%d\n",errno,outlen);
	  iconv_close(cd);
	  return -1;
	}
	iconv_close(cd);
	return outlen;
 }

 static int unicode_2_gb18030(char *inbuf,size_t inlen,char *outbuf,size_t outlen)
 {
  return code_convert("unicode","gb18030",inbuf,inlen,outbuf,outlen);
 }

 int sms_decode_language(char *in, size_t inlen, char *out, size_t outlen, int id)
 {
    char tmp[512] = {0};
	int outleft = 0;
	int i = 0;
	int j = 0;
	tmp[0] = 0xFF;
	tmp[1] = 0xFE;//file format utf-16
	j = 2 + inlen;
	for (i = 0; ((i < inlen)); i += 2)
	{
		tmp[i + 2] = in[i + 1];
		tmp[i + 3] = in[i];
	}
    switch(id)
    {
        case 0://cn
	       outleft = unicode_2_gb18030(tmp, j, out, outlen);
           break;
       default://en ascii
          outleft = unicode_2_gb18030(tmp, j, out, outlen);
          break;
    }
	if (outleft <= 0)
		return -1;
	out[outlen-outleft] = '\0';
	return outlen - outleft;
 }

 static  int sms_pdu_invert_num(char *src_num, char *dest_num, unsigned char *num_len)
 {
    unsigned char len = strlen(src_num);
    int i = 0;
    char *data = dest_num;
    int odd = len%2;
    if(len > GSM_CALL_NUM_LEN)
       return 1;
    for(i = 0; i < (len-odd); i+=2)
    {
        char low_ch[2];
        char high_ch[2];
        low_ch[0] = *src_num++;
        low_ch[1]= '\0';
        high_ch[0] = *src_num++;
        high_ch[1]= '\0';
        
        *data++ = (atoi(high_ch)<<4)|(atoi(low_ch));
        *num_len += 1;
    }
    if(odd)
    {
      *data++=(0xF0)|((*src_num));
      *num_len += 1 ;//
    }
    return 0;
 }

 int  gsm_decode_pdu_content(unsigned char *out_data, int *out_data_len, struct gsm_sms_content *content)
 {
        char *buf = NULL;
        size_t out_len = 1024;
        size_t in_len;
        iconv_t cd;
        if(!content)
            return 1;
       switch (content->encrypt)
       {
          case GSM_SMS_CONTENT_ENCRYPT_NONE:
            buf = content->message;//
            in_len = content->msg_len;
            break;
         default:
             return 1;
       }
       switch (content->charset)
       {
          case GSM_SMS_CONTENT_CHARSET_ASCII:
             cd = iconv_open("UTF-8", "ASCII");
             break;
          case GSM_SMS_CONTENT_CHARSET_UTF8:
             cd = iconv_open("UTF-8", "UTF-8");
             break;
          case GSM_SMS_CONTENT_CHARSET_GB18030://ISO_8859-2
             cd = iconv_open("UTF-8", "GB18030");
             break;
          case  GSM_SMS_CONTENT_CHARSET_EEUR:
             cd = iconv_open("UTF-8", "ISO_8859-2");
             break;
          case  GSM_SMS_CONTENT_CHARSET_WEUR:
             cd = iconv_open("UTF-8", "ISO_8859-15");
             break;
          default:
             return 1;
        }
        if (cd < 0) 
        {
            gsm_log(GSM_LOG_WARNING, "gsm_decode_pdu_content iconv open fail\n");
           return 1;
        }
        if (iconv(cd, &buf, &in_len, (char**)&out_data, &out_len) < 0) 
        {
                gsm_log(GSM_LOG_WARNING, " gsm_decode_pdu_content iconv  fail\n");
                iconv_close(cd);
                return 1;
        }
        *out_data_len = 1024 - out_len;
        *out_data = '\0';
        iconv_close(cd);
        return 0;
 }

 int gsm_encode_pdu_to_num(char *to_num, unsigned char **dest, int  *len)
 {
        unsigned char *buf = *dest;
        char numbuf[50] = {0};
        char *nums;
        unsigned char num_len = 0;
        unsigned msg_len = 0;
        char flag = 0;
        memcpy(numbuf, to_num, sizeof(numbuf));
        nums = numbuf;
        if (nums[0] == '+')
        {
                nums++;
                flag = 1;
        }
        num_len = strlen(numbuf);
        if(num_len < 3)
           return 1;
        /* Address-Length. Length of phone number */
         //only strlen tonum not include 91 and f
        buf[msg_len] = (num_len);
        msg_len++;
        if(flag) /* Destination Type-of-Address */
            buf[msg_len] = 0x91;
        else
            buf[msg_len] = 0x81;
        msg_len++;
        num_len = 0;
        if (sms_pdu_invert_num(nums, (char *)&buf[msg_len], &num_len))
           return 1;//fail
        *dest = &buf[msg_len + num_len];
        *len = *len + msg_len + num_len;
        return 0;
 }

 int gsm_encode_pdu_vp(unsigned char vp, unsigned char **outdata, int *outdata_len)
 {
        **outdata = vp;
        *outdata = *outdata + 1;
        *outdata_len = *outdata_len + 1;
        return 0;
 }

 int gsm_encode_pdu_dcs(unsigned char dcs, unsigned char **outdata, int  *outdata_len)
 {
        **outdata =  dcs;
        *outdata = *outdata + 1;
        *outdata_len = *outdata_len + 1;
        return 0;
 }

 int  gsm_encode_pdu_pid(unsigned char pid, unsigned char **dest, int *len)
 {
        /* TP-PID - Protocol Identifier */
        **dest = pid;
        *dest = *dest + 1;
        *len = *len + 1;
        return 0;
 }

 int gsm_encode_pdu_submit_ref(unsigned char submit, unsigned char ref, unsigned char **dest, int *len )
 {
        unsigned char *buf = *dest;
        *(buf++) = 0x31;
        *(buf++) = 0x00;
        *dest = *dest + 2;
        *len = *len + 2;
        return 0;
 }

 int  gsm_encode_pdu_smsc(char *smsc, unsigned char **dest, int *len)
 {
    unsigned  char *buf = *dest;//buf---*dest----p  *buf----**dest---*p
    int smsc_len = 0;
    int msg_len = 0;
    int odd = 0;
    char numbuf[50] = {0};
    char *num;
    unsigned char num_len = 0;
    memcpy(numbuf,smsc,sizeof(numbuf));
    num = numbuf;
    smsc_len = strlen(numbuf);
    if( num[0] == '+' )
        num++;
    if(smsc_len > 0)
    {
        if(smsc_len & 0x1)
        {
            buf[0] = (smsc_len + 1)/2 + 1;
            odd = 1;
        }
        else
            buf[0] = (smsc_len)/2 + 1;//why add 1 num_type 81/91
    }
    else
        buf[0] = 0x00;
    msg_len++;
    if(smsc_len > 0) //invert
    {
        buf[msg_len] =  0x91;
        msg_len++;
        if (sms_pdu_invert_num(num, (char*)&buf[msg_len], &num_len))
            return 1;//fail
         *dest = &buf[msg_len + num_len];//if ok
    }
    *len = *len + msg_len + num_len;
    gsm_log(GSM_LOG_WARNING, "gsm_encode_pdu_smsc num=%s len=%d\n",numbuf,smsc_len);
    return 0;
 }
