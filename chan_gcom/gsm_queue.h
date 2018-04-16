
#ifndef _ATCOM_QUEUE_H
#define _ATCOM_QUEUE_H

#include <pthread.h>
#include <time.h>

#define GSM_MAX_SMS_QUEUE_LEN 4
#define GSM_MAX_CALL_QUEUE_LEN  4
#define GSM_MAX_CMD_QUEUE_LEN 5
#define GSM_MAX_EVENT_QUEUE_LEN 4
#define GSM_MAX_SCHED_QUEUE_LEN  4

#define GSM_LIST_HEAD(name, type)			\
struct name                                 \
{					 			            \
	struct type *first;						\
	struct type *last;						\
	pthread_mutex_t lock;					\
}

#define GSM_LIST_HEAD_NOLOCK(name, type)   \
struct name                                \
{								           \
	struct type *first;					   \
	struct type *last;					   \
}

#define GSM_LIST_ENTRY(type)				\
struct                                      \
{								            \
	struct type *next;						\
}

#define	GSM_LIST_FIRST(head)	((head)->first)
#define	GSM_LIST_LAST(head)	((head)->last)
#define GSM_LIST_NEXT(elm, field)	((elm)->field.next)
#define	GSM_LIST_EMPTY(head)	(GSM_LIST_FIRST(head) == NULL)

#define GSM_LIST_TRAVERSE(head,var,field) 				\
	for((var) = (head)->first; (var); (var) = (var)->field.next)

#define GSM_LIST_TRAVERSE_SAFE_BEGIN(head, var, field)      \
{			                                             	\
	typeof((head)) __list_head = head;						\
	typeof(__list_head->first) __list_next;					\
	typeof(__list_head->first) __list_prev = NULL;			\
	typeof(__list_head->first) __new_prev = NULL;			\
	for ((var) = __list_head->first, __new_prev = (var),	\
	      __list_next = (var) ? (var)->field.next : NULL;	\
	     (var);									            \
	     __list_prev = __new_prev, (var) = __list_next,		\
	     __new_prev = (var),							    \
	     __list_next = (var) ? (var)->field.next : NULL,	\
	     (void) __list_prev								    \
	    )
#define GSM_LIST_TRAVERSE_SAFE_END  }

#define GSM_LIST_HEAD_INIT(head)                \
{					                            \
	(head)->first = NULL;						\
	(head)->last = NULL;						\
	gsm_mutex_init(&(head)->lock);				\
}


#define GSM_LIST_HEAD_INIT_NOLOCK(head)         \
{	                                			\
	(head)->first = NULL;						\
	(head)->last = NULL;						\
}

#define GSM_LIST_HEAD_DESTROY(head)             \
{					                            \
	(head)->first = NULL;						\
	(head)->last = NULL;						\
	gsm_mutex_destroy(&(head)->lock);			\
}

#define GSM_LIST_INSERT_AFTER(head, listelm, elm, field)       \
do {		                                                       \
	(elm)->field.next = (listelm)->field.next;			       \
	(listelm)->field.next = (elm);					           \
	if ((head)->last == (listelm))					           \
		(head)->last = (elm);					               \
} while (0)


#define GSM_LIST_INSERT_HEAD(head, elm, field)      \
do {	                                     		\
		(elm)->field.next = (head)->first;			\
		(head)->first = (elm);				     	\
		if (!(head)->last)					        \
			(head)->last = (elm);				    \
} while (0)


#define GSM_LIST_INSERT_TAIL(head, elm, field)  \
do {			                                \
      if (!(head)->first) {						\
		(head)->first = (elm);					\
		(head)->last = (elm);					\
      } else {								    \
		(head)->last->field.next = (elm);		\
		(head)->last = (elm);					\
      }									        \
} while (0)

#define GSM_LIST_APPEND_LIST(head, list, field)   \
do {			                                  \
	if (!(list)->first) {						  \
		break;			         				  \
	}							            	  \
	if (!(head)->first) {					 	  \
		(head)->first = (list)->first;			  \
		(head)->last = (list)->last;			  \
	} else {						          	  \
		(head)->last->field.next = (list)->first; \
		(head)->last = (list)->last;			  \
	}								              \
	(list)->first = NULL;						  \
	(list)->last = NULL;						  \
} while (0)


#define GSM_LIST_REMOVE_CURRENT(head, field)     \
do {					                         \
	__new_prev->field.next = NULL;				 \
	__new_prev = __list_prev;					 \
	if (__list_prev)							 \
		__list_prev->field.next = __list_next;	 \
	else										 \
		(head)->first = __list_next;			 \
	if (!__list_next)							 \
		(head)->last = __list_prev;				 \
	} while (0)

#define GSM_LIST_INSERT_BEFORE_CURRENT(head, elm, field)    \
do {	                                                	\
	if (__list_prev) {						                \
		(elm)->field.next = __list_prev->field.next;		\
		__list_prev->field.next = elm;				        \
	} else {							                    \
		(elm)->field.next = (head)->first;			        \
		(head)->first = (elm);					            \
	}								                        \
	__new_prev = (elm);						                \
} while (0)

#define GSM_LIST_REMOVE_HEAD(head, field) ({				\
		typeof((head)->first) cur = (head)->first;		    \
		if (cur) {						                    \
			(head)->first = cur->field.next;		        \
			cur->field.next = NULL;				            \
			if ((head)->last == cur)			            \
				(head)->last = NULL;			            \
		}							                        \
		cur;							                    \
	})

#define GSM_LIST_REMOVE(head, elm, field) ({			    \
	__typeof(elm) __res = NULL;                             \
	__typeof(elm) __tmp = elm;                              \
	if (!__tmp) {                                           \
		__res = NULL;                                       \
	} else if ((head)->first == (elm)) {				 	\
		__res = (head)->first;                              \
		(head)->first = (elm)->field.next;			        \
		if ((head)->last == (elm))			                \
			(head)->last = NULL;			                \
	} else {								                \
		typeof(elm) curelm = (head)->first;			        \
		while (curelm && (curelm->field.next != (elm)))		\
			curelm = curelm->field.next;			        \
		if (curelm) {                                       \
			__res = (elm);                                  \
			curelm->field.next = (elm)->field.next;			\
			if ((head)->last == (elm))				        \
				(head)->last = curelm;				        \
		}                                                   \
	}								                        \
	if (__res) {                                            \
		(__res)->field.next = NULL;                         \
	}                                                       \
	(__res);                                                \
})

 typedef struct
 {
   char data[GSM_MAX_EVENT_QUEUE_LEN][1024];
   int rd_idx;
   int wr_idx; 
   int len;
   pthread_mutex_t lock;
  }event_fifo_queue;

 int write_to_event_queue(event_fifo_queue *queue,char *wr_buf);
 int read_from_event_queue(event_fifo_queue *queue,char *rd_buf, int len);

#endif /* _ATCOM_QUEUE_H */
