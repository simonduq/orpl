CONTIKI=../contiki
CONTIKI_PROJECT = app-collect-only app-down-only app-down-only-full app-any-to-any
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
TARGET=sky
CONTIKI_TOOLS=/home/user/contiki-tools

WITH_UIP6=1
UIP_CONF_IPV6=1
CFLAGS+= -DUIP_CONF_IPV6_RPL -I-
PROJECT_SOURCEFILES += bloom.c simple-energest.c rpl-tools.c

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
all: $(CONTIKI_PROJECT) curr_contiki_head

include Makefile.include

curr_contiki_head:
	git --git-dir=$(CONTIKI)/.git log -1 | head -n 1 > curr_contiki_head

%.exe: %.sky
	cp $< $@
