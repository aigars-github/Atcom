##################################################################
##################################################################

CC=gcc
GREP=grep
AWK=awk

OSARCH=$(shell uname -s)
PROC?=$(shell uname -m)

DYNAMIC_LIBRARY:=chan_gcom.so
DYNAMIC_OBJS=gsm.o gsm_sched.o  gsm_sms_pdu.o gsm_sms.o gsm_event.o gsm_cmd.o gsm_call.o gsm_internal.o gsm_queue.o sig_gsm.o chan_gcom.o
CFLAGS=-Wall -Wstrict-prototypes -Wmissing-prototypes -g3 -O0 -fPIC  -g -I/usr/include/libxml2 -Wl,-unresolved-symbols=ignore-all  -m64 -march=native -pipe  -pthread -shared
#-Werror -Wstrict-prototypes -Wmissing-prototypes -g3 -O0 -fPIC $(ALERTING) $(LIBEXTEND_COUNTERS)

libdir?=/lib64/asterisk/modules
SOFLAGS:=-Wl,-h$(DYNAMIC_LIBRARY) -ltonezone -lpthread
LDCONFIG = /sbin/ldconfig


all: $(DYNAMIC_LIBRARY)

install: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)
	mkdir -p $(INSTALL_PREFIX)$(libdir)
	mkdir -p $(INSTALL_PREFIX)$(INSTALL_BASE)/include
ifneq (${OSARCH},SunOS)
	install -m 644 libgsmcom.h $(INSTALL_PREFIX)$(INSTALL_BASE)/include
	install -m 755 $(DYNAMIC_LIBRARY) $(INSTALL_PREFIX)$(libdir)
	if [ -x /usr/sbin/sestatus ] && ( /usr/sbin/sestatus | grep "SELinux status:" | grep -q "enabled"); then /sbin/restorecon -v $(INSTALL_PREFIX)$(libdir)/$(DYNAMIC_LIBRARY); fi
	( cd $(INSTALL_PREFIX)$(libdir) ; ln -sf libgsmcom.so.$(SONAME) libgsmcom.so)
	install -m 644 $(STATIC_LIBRARY) $(INSTALL_PREFIX)$(libdir)
	if test $$(id -u) = 0; then $(LDCONFIG) $(LDCONFIG_FLAGS) $(INSTALL_PREFIX)$(libdir); fi
else
	install -f $(INSTALL_PREFIX)$(INSTALL_BASE)/include -m 644 libgsmcom.h
	install -f $(INSTALL_PREFIX)$(libdir) -m 755 $(DYNAMIC_LIBRARY)
	( cd $(INSTALL_PREFIX)$(libdir) ; ln -sf libgsmcom.so.$(SONAME) libgsmcom.so)
	install -f $(INSTALL_PREFIX)$(libdir) -m 644 $(STATIC_LIBRARY)
endif

uninstall:
	@echo "Removing Libgsm"
	rm -f /lib64/asterisk/modules/chan_gcom.so


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


$(DYNAMIC_LIBRARY): $(DYNAMIC_OBJS)
	$(CC) -shared $(SOFLAGS) -o $@ $(DYNAMIC_OBJS) -lm


clean:
	rm -f *.o *.so 
	rm -f  $(DYNAMIC_LIBRARY)
	rm -f .*.d

.PHONY:

FORCE:

ifneq ($(wildcard .*.d),)
   include .*.d
endif
