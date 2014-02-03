CONTIKI=/home/user/contiki-2.7
CONTIKI_PROJECT = app-collect-only
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
TARGET=sky

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL
PROJECT_SOURCEFILES += orpl.c orpl-anycast.c orpl-of-edc.c orpl-routing-set.c contikimac-orpl.c cc2420-softack.c

BLOOM_M ?= 512
CFLAGS += -DCMD_BLOOM_M=$(BLOOM_M)

BLOOM_K ?= 4 # 0 means bitmap instead of Bloom Filter
CFLAGS += -DCMD_BLOOM_K=$(BLOOM_K)

BLOOM_FP_RECOVERY ?= 1
CFLAGS += -DCMD_BLOOM_FP_RECOVERY=$(BLOOM_FP_RECOVERY)

EDC_W ?= 64
CFLAGS += -DCMD_EDC_W=$(EDC_W)

.PHONY: curr_contiki_head
all: $(CONTIKI_PROJECT)

include Makefile.include

%.exe: %.sky
	cp $< $@
