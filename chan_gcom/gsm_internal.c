#include <stdlib.h>
#include <stdio.h>

#include "libgsmcom.h"
#include "gsm_internal.h"


 struct timeval tvfix(struct timeval a)
 {
	if (a.tv_usec >= GSM_TIME_ONE_MILLION)
	{
		a.tv_sec += a.tv_usec / GSM_TIME_ONE_MILLION;
		a.tv_usec %= GSM_TIME_ONE_MILLION;
	}
	else if (a.tv_usec < 0)
		a.tv_usec = 0;
	return a;
 }

 struct timeval gsm_tvadd(struct timeval a, struct timeval b)
 {
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec += b.tv_sec;
	a.tv_usec += b.tv_usec;
	if (a.tv_usec >= GSM_TIME_ONE_MILLION)
	{
		a.tv_sec++;
		a.tv_usec -= GSM_TIME_ONE_MILLION;
	}
	return a;
 }

 struct timeval gsm_tvsub(struct timeval a, struct timeval b)
 {
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec -= b.tv_sec;
	a.tv_usec -= b.tv_usec;
	if (a.tv_usec < 0)
	{
		a.tv_sec-- ;
		a.tv_usec += GSM_TIME_ONE_MILLION;
	}
	return a;
 }

 int gsm_tvzero(const struct timeval t)
 {
	return (t.tv_sec == 0 && t.tv_usec == 0);
 }

 int gsm_tvdiff_ms(struct timeval end, struct timeval start)
 {
	return  ((end.tv_sec - start.tv_sec) * 1000) +
		(((1000000 + end.tv_usec - start.tv_usec) / 1000) - 1000);
 }

 struct timeval gsm_tv(unsigned long sec, unsigned long usec)
 {
    struct timeval t;
    t.tv_sec = sec ;
    t.tv_usec = usec;
    return t;
 }

 struct timeval gsm_samp2tv(unsigned int nsamp, unsigned int rate)
 {
	return gsm_tv(nsamp / rate, (nsamp % rate) * (1000000 / rate));
 }

 struct timeval gsm_tvnow(void)
 {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t;
 }

 int gsm_tvcmp(struct timeval a, struct timeval b)
 {
	if (a.tv_sec < b.tv_sec)
		return 1;//small exec
	if (a.tv_sec > b.tv_sec)
		return -1;//over
	/* now seconds are equal */
	if (a.tv_usec < b.tv_usec)
		return 1;//small
	if (a.tv_usec > b.tv_usec)
		return -1;//over
	return 0;//equal
 }

 int gsm_mutex_init(pthread_mutex_t *lock )
 {
    int res = 0;
    res =pthread_mutex_init(lock,NULL);
    return res;
 }

 int gsm_mutex_destroy(pthread_mutex_t *lock)
 {
    int res = 0;
    res = pthread_mutex_destroy(lock);
    return res;
 }

 char *gsm_strdup(const char *str)
 {
    unsigned int  len = strlen(str) + 1;
    void *dest = gsm_calloc(1, len);
    if (!dest)
           return NULL;
    return (char *) memcpy(dest, str, len);
 }

 int gsm_cmd_entry_tokenize(char *entry, char *tokens[], int len)
 {
    int count = 0;
    char *p = NULL;
    char *previous_token = NULL;
    if(len <0 || !entry)
        return  0;
    memset(tokens, 0, (len * sizeof(tokens[0])));
    if (entry[0] == ',')
      tokens[count++] = gsm_strdup("");
    if(count == (len - 1))
      return count;
    for (p = strtok(entry, ","); p; p = strtok(NULL, ","))
    {
       if(count == (len-1))
         break;
       if (count > 0 && p[strlen(p)-1] == '\"' && p[0] != '\"')
       {
          previous_token = tokens[count - 1];
          if (previous_token[strlen(previous_token)-1] != '\"' &&  previous_token[0] == '\"')
          {
             char *new_token = NULL;
             new_token = (char *)gsm_calloc(1, strlen(previous_token) + strlen(p) + 1);
             if(!new_token)
                  return 0;
             sprintf(new_token, "%s,%s", previous_token, p);
             tokens[count - 1] = new_token;
             gsm_free(previous_token);
             continue;
          }
        }
        tokens[count] = gsm_strdup(p);
        count++;
     }
     return count;
 }

 int gsm_match_prefix(char *string, const char *prefix)
 {
    int prefix_len = strlen(prefix);
    if (!strncmp(string, prefix, prefix_len))
    {
       int len = strlen(&string[prefix_len]);
       memmove(string, &string[prefix_len], len);
       memset(&string[len], 0, strlen(&string[len]));
      return 0;
    }
    return 1;
 }

 void gsm_free_tokens(char *tokens[])
 {
	unsigned i;
	for (i = 0; tokens[i]; i++)
	   gsm_free(tokens[i]);
 }
