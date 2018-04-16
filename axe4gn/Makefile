PWD=$(shell pwd)
RM      = @rm -rf
JUNK	= *~ *.bak DEADJOE

KVER	?= $(shell uname -r)
KNAME	?= $(shell uname -s)
KDIR	=/lib/modules/$(shell uname -r)/build
KVERBOSE=0
MODTYPE=ko

VERBOSE		=0
AUSB_DIR	=$(PWD)
MOD_DIR		=$(PWD)
INCLUDE_DIR =$(PWD)/include 
OS_DIR		=$(PWD)/include/os

TARGETS=check-dahdi check-kernel compile
CLEANTARGETS=clean  
INSTALLTARGETS=install

EXTRA_CFLAGS=$(GLOBAL_CFLAGS) -I$(KDIR)/include/linux -I$(INCLUDE_DIR) -I$(OS_DIR) -I/usr/include

DEBFLAGS = -O

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -w

EXTRA_CFLAGS += $(TESTFLAGS)

EXTRA_UTIL_FLAGS=$(GLOBAL_CFLAGS)

obj-m += axe4gn.o

all: $(TARGETS)

compile:
	$(MAKE) KBUILD_VERBOSE=$(KVERBOSE) -C $(KDIR) SUBDIRS=$(MOD_DIR) EXTRA_FLAGS="$(EXTRA_CFLAGS)" modules

clean: 	
	$(MAKE) -C $(KDIR) SUBDIRS=$(MOD_DIR) clean
	@find . -name '*symvers*' | xargs rm -f

install: 
	$(MAKE) -C $(KDIR) SUBDIRS=$(MOD_DIR) modules_install
#	$(shell depmod -a)

#Check for linux headers
check-kernel: 
	@if [ ! -e $(KDIR) ]; then \
		echo "   Error linux headers/source not found: $(KDIR) !"; \
		echo ; \
		exit 1; \
	fi 
	@if [ ! -e $(KDIR)/.config ]; then \
		echo "   Error linux headers/source not configured: missing $(KDIR)/.config !"; \
		echo ; \
		exit 1; \
	fi 
	@if [ ! -e $(KDIR)/include ]; then \
		echo "   Error linux headers/source incomplete: missing $(KDIR)/include dir !"; \
		echo ; \
		exit 1; \
	fi

#Check for dahdi
check-dahdi:

	@if [ -e /usr/include/dahdi/kernel.h ]; then \
		if [ -f /usr/include/dahdi/Module.symvers ]; then \
		  	cp -f /usr/include/dahdi/Module.symvers $(MOD_DIR)/; \
		elif [ -f $(DAHDI_DIR)/src/dahdi-headers/drivers/dahdi/Module.symvers ]; then \
		  	cp -f $(DAHDI_DIR)/src/dahdi-headers/drivers/dahdi/Module.symvers $(MOD_DIR)/; \
		else \
			echo "Error: Dahdi source not compiled, missing Module.symvers file"; \
			echo "	     Please recompile Dahdi directory first"; \
			exit 1; \
		fi; \
	else \
		echo "Error: Dahdi source not found"; \
		echo "       Please check Dahdi Dir ($(DAHDI_DIR)) "; \
		echo ; \
		exit 1; \
	fi; 
	@sleep 1;              

boot:
	@./mkboot.sh


