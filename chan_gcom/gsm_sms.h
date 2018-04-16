/*
 /*! \file
 * \brief TTY/TDD Generation support
 * \note Includes code and algorithms from the Zapata library.
 */

#ifndef _ATCOM_SMS_H
#define _ATCOM_SMS_H

 int check_num_is_digit(const char *num);
 int gsm_encode_pdu_body(struct gsm_span *span, struct gsm_sms_event_data *sms);
 int octet_to_septet(int octet);
 int septet_to_octet(int septet);
 int gsmEncodeUCS2(unsigned char* pSrc, unsigned char** pDst, size_t  nSrcLength, int *out_len, int offset);
 int  gsmEncode7bit(unsigned  char* pSrc, unsigned char** pDst, int nSrcLength, int offset);
 void gsm_to8Bit(unsigned char *in, unsigned char *out, int len, int flag);
 char *pdu_get_smsc_len(char *src, char *len);
 char *pdu_get_first_oct(char *src, unsigned char *deliver);
 char *pdu_get_oa_len(char *src, char *len);
 char *pdu_get_oa_type(char *src, char *type);
 char *pdu_get_oa_num(char *src, int len, char *num);
 char *pdu_get_pid(char *src, char *pid);
 char *pdu_decode_ud_dcs(char *src,  unsigned char *dcs);
 char *pdu_get_timestamp(char *src, struct gsm_sms_pdu_timestamp *tstamp);
 char *pdu_get_ud_len(char *src, unsigned char *len);
 char *pdu_decode_udh(char *src, unsigned char *udhl, char *seq);
 void gsm_string2byte(char *inbuf, int len, unsigned char *outbuf);
 char *pdu_get_addr_num(char *src, int len, char *dest, int flag);
 char *pdu_get_addr_type(char *src, char *type, char *plan);
 void gsm_reverse_num(char *src, char *dest, char  type);
 int sms_decode_language(char *in, size_t inlen, char *out, size_t outlen, int id);
 int gsm_parse_cmd(char * buf, char token,char *cmd[],  int len);
 int gsm_handle_incoming_txt_sms(struct gsm_span *span, char *from, char *scts,char* timestamp, char *content);
 int  gsm_handle_incoming_pdu_sms(struct gsm_span *span, char *content, int len);
 char *gsm_get_dot_string(char *in, char *out, char flag);
 int code_convert(char *from_charset,char *to_charset, char *inbuf,size_t  inlen,char *outbuf,size_t outlen);
 int  gsm_decode_pdu_content(unsigned char *out_data, int *out_data_len, struct gsm_sms_content *content);
 int gsm_encode_pdu_to_num(char *to_num, unsigned char **dest, int  *len);
 int gsm_encode_pdu_vp(unsigned char vp, unsigned char **outdata, int *outdata_len);
 int gsm_encode_pdu_dcs(unsigned char dcs, unsigned char **outdata, int  *outdata_len);
 int  gsm_encode_pdu_pid(unsigned char pid, unsigned char **dest, int *len);
 int gsm_encode_pdu_submit_ref(unsigned char submit, unsigned char ref, unsigned char **dest, int *len );
 int  gsm_encode_pdu_smsc(char *smsc, unsigned char **dest, int *len);

#endif /* _ATCOM_SMS_H */
