#CONTIKI=../../contiki
CONTIKI=/home/user/contiki-2.7
CONTIKI_PROJECT = app-collect-only
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
TARGET=sky

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL
PROJECT_SOURCEFILES += bloom.c simple-energest.c rpl-tools.c cc2420-softack.c contikimac-orpl.c anycast.c rpl-of-edc.c

ifdef NO_WARNINGS
	CFLAGS += -w
endif

CYCLE_TIME ?= 500
CFLAGS += -DCMD_CYCLE_TIME=$(CYCLE_TIME)

BLOOM_M ?= 512
CFLAGS += -DCMD_BLOOM_M=$(BLOOM_M)

BLOOM_K ?= 4 # 0 means bitmap instead of Bloom Filter
CFLAGS += -DCMD_BLOOM_K=$(BLOOM_K)

BLOOM_FP_RECOVERY ?= 1
CFLAGS += -DCMD_BLOOM_FP_RECOVERY=$(BLOOM_FP_RECOVERY)

EDC_W ?= 64
CFLAGS += -DCMD_EDC_W=$(EDC_W)

ACK_WITH_ADDR ?= 1
CFLAGS += -DCMD_ACK_WITH_ADDR=$(ACK_WITH_ADDR)

.PHONY: curr_contiki_head
all: $(CONTIKI_PROJECT)

include Makefile.include

%.exe: %.sky
	cp $< $@
